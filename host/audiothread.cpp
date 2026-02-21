#include "audiothread.h"
#include "../global.h"
#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(log_core_audio, "opf.core.audio");

AudioThread::AudioThread(const QAudioDevice& inputDevice, 
                       const QAudioDevice& outputDevice,
                       const QAudioFormat& format,
                       QObject* parent)
    : QThread(parent)
    , m_inputDevice(inputDevice)
    , m_outputDevice(outputDevice)
    , m_format(format)
    , m_audioSource(nullptr)
    , m_audioIODevice(nullptr)
    , m_sinkIODevice(nullptr)
    , m_running(false)
    , m_cleanupStarted(false)
    , m_volume(1.0)
{
    qCDebug(log_core_audio) << "AudioThread constructor called";
    qCDebug(log_core_audio) << "Input device:" << inputDevice.description();
    qCDebug(log_core_audio) << "Output device:" << outputDevice.description();
    qCDebug(log_core_audio) << "Format - Sample rate:" << format.sampleRate() 
                           << "Channels:" << format.channelCount()
                           << "Bytes per sample:" << format.bytesPerSample();
}

AudioThread::~AudioThread()
{
    // Always stop the thread to prevent "destroyed while running" error
    qCDebug(log_core_audio) << "AudioThread destructor called";
    
    if (g_applicationShuttingDown.loadAcquire() == 1) {
        qCDebug(log_core_audio) << "AudioThread destructor: Application shutting down - forcing thread stop";
        
        // CRITICAL: During shutdown, do minimal cleanup to prevent Qt Multimedia crashes
        m_running = false;
        m_cleanupStarted = true;
        requestInterruption();
        
        // Force terminate the thread during shutdown - don't wait long
        // Do this BEFORE touching any Qt Multimedia objects
        if (isRunning()) {
            if (!wait(10)) {  // Very short wait - 10ms
                qCDebug(log_core_audio) << "AudioThread: Terminating thread forcefully during shutdown";
                terminate();
                wait(10);  // Short wait after terminate
            }
        }
        
        // After thread is stopped, nullify pointers without calling any cleanup methods
        // This prevents cross-thread timer warnings
        m_sinkIODevice = nullptr;
        m_audioIODevice = nullptr;
        
        // Don't call reset() or delete - Qt Multimedia objects may have internal timers
        // that would cause "cannot be stopped from another thread" warnings
        // Just leak these objects during shutdown - the OS will clean up
        (void)m_audioSink.take(); // Take ownership without calling destructor, then leak it
        m_audioSource = nullptr; // Just nullify, don't delete
        
        qCDebug(log_core_audio) << "AudioThread destructor: Thread forcefully stopped during shutdown";
        return;
    }
    
    // Normal cleanup path (not during application shutdown)
    // Ensure proper cleanup order
    stop();
    
    // Give the thread time to finish cleanup
    if (!wait(2000)) {
        // Force terminate if it doesn't finish in 2 seconds
        qCWarning(log_core_audio) << "AudioThread taking too long to finish, forcing termination";
        terminate();
        wait(1000);
    }
    
    // After thread is properly stopped, it's safe to clean up Qt Multimedia objects
    // because their run() method has already cleaned them up in the correct thread
}

void AudioThread::stop()
{
    qCDebug(log_core_audio) << "AudioThread::stop() called";
    
    // Check if application is shutting down
    if (g_applicationShuttingDown.loadAcquire() == 1) {
        qCDebug(log_core_audio) << "AudioThread::stop() - Application shutting down, minimal stop";
        m_mutex.lock();
        m_running = false;
        m_cleanupStarted = true;
        m_mutex.unlock();
        
        // Still need to interrupt the thread to make it exit
        requestInterruption();
        return;
    }
    
    // Normal stop procedure
    m_mutex.lock();
    m_running = false;
    m_cleanupStarted = true;  // Immediately mark cleanup as started
    m_mutex.unlock();
    
    // Request thread interruption to break the event loop
    requestInterruption();
}

void AudioThread::setVolume(qreal volume)
{
    m_mutex.lock();
    m_volume = volume;
    if (m_audioSink && !m_cleanupStarted) {
        m_audioSink->setVolume(volume);
    }
    m_mutex.unlock();
}

qreal AudioThread::volume() const
{
    m_mutex.lock();
    qreal vol = m_volume;
    m_mutex.unlock();
    return vol;
}

void AudioThread::cleanupMultimediaObjects()
{
    qCDebug(log_core_audio) << "AudioThread::cleanupMultimediaObjects() - skipping to prevent crashes";
    // This method is now a no-op to prevent Qt Multimedia crashes during shutdown
}

void AudioThread::run()
{
    qCDebug(log_core_audio) << "AudioThread::run() starting";
    m_running = true;

    try {
        qCDebug(log_core_audio) << "Creating QAudioSource with input device:" << m_inputDevice.description();
        qCDebug(log_core_audio) << "Audio format - Sample rate:" << m_format.sampleRate() 
                               << "Channels:" << m_format.channelCount()
                               << "Sample format:" << m_format.sampleFormat()
                               << "Bytes per frame:" << m_format.bytesPerFrame();
        
        // Check if the input device supports the format
        if (!m_inputDevice.isFormatSupported(m_format)) {
            qCWarning(log_core_audio) << "Input device does not support the specified format!";
            QAudioFormat nearestFormat = m_inputDevice.preferredFormat();
            qCDebug(log_core_audio) << "Input device preferred format - Sample rate:" << nearestFormat.sampleRate() 
                                   << "Channels:" << nearestFormat.channelCount()
                                   << "Sample format:" << nearestFormat.sampleFormat();
        }
        
        // Give newly plugged device time to initialize before trying to use it
        qCDebug(log_core_audio) << "Waiting for audio device to be ready...";
        QThread::msleep(200);
        
        m_audioSource = new QAudioSource(m_inputDevice, m_format);
        
        // Connect to state changed signal to monitor device health
        connect(m_audioSource, &QAudioSource::stateChanged, this, [this](QAudio::State state) {
            if (state == QAudio::StoppedState && m_audioSource->error() != QAudio::NoError) {
                QString errorMsg = QString("Audio source error: %1").arg(m_audioSource->error());
                qCWarning(log_core_audio) << errorMsg;
                
                // Check for device invalidation error
                if (m_audioSource->error() == QAudio::UnderrunError || 
                    m_audioSource->error() == QAudio::IOError) {
                    qCWarning(log_core_audio) << "Audio device may have been disconnected or invalidated";
                    emit error(errorMsg + " (device may have been disconnected)");
                    
                    // Stop the thread gracefully
                    m_mutex.lock();
                    m_running = false;
                    m_mutex.unlock();
                }
            }
        });
        
        // Try to force the audio source to start actively
        qCDebug(log_core_audio) << "Starting QAudioSource...";
        m_audioIODevice = m_audioSource->start();

        if (!m_audioIODevice) {
            qCWarning(log_core_audio) << "Failed to start audio source";
            QString errorMsg = QString("Failed to start audio source - Error: %1").arg(m_audioSource->error());
            emit error(errorMsg);
            delete m_audioSource;
            m_audioSource = nullptr;
            return;
        }
        
        // Check for immediate errors after starting
        if (m_audioSource->error() != QAudio::NoError) {
            QString errorMsg = QString("Audio source error immediately after start: %1").arg(m_audioSource->error());
            qCWarning(log_core_audio) << errorMsg;
            emit error(errorMsg);
            delete m_audioSource;
            m_audioSource = nullptr;
            return;
        }
        
        // Give the device a moment to initialize
        QThread::msleep(100);
        
        qCDebug(log_core_audio) << "Audio source started successfully, state:" << m_audioSource->state();
        qCDebug(log_core_audio) << "QAudioSource error:" << m_audioSource->error();
        qCDebug(log_core_audio) << "QAudioSource format in use:" << m_audioSource->format().sampleRate() 
                               << "Hz," << m_audioSource->format().channelCount() << "ch";
        
        // Try to manually trigger the source to become active
        if (m_audioSource->state() == QAudio::IdleState) {
            qCDebug(log_core_audio) << "AudioSource is idle, trying to activate...";
            
            // Try reading a small amount to trigger active state
            if (m_audioIODevice->isReadable()) {
                char testBuffer[64];
                qint64 testRead = m_audioIODevice->read(testBuffer, sizeof(testBuffer));
                qCDebug(log_core_audio) << "Test read result:" << testRead << "bytes";
                
                // Check state after test read
                QThread::msleep(50);
                qCDebug(log_core_audio) << "AudioSource state after test read:" << m_audioSource->state();
            }
        }

        qCDebug(log_core_audio) << "Creating QAudioSink with output device:" << m_outputDevice.description();
        
        // Check if the output device supports the format
        if (!m_outputDevice.isFormatSupported(m_format)) {
            qCWarning(log_core_audio) << "Output device does not support the specified format!";
            QAudioFormat nearestFormat = m_outputDevice.preferredFormat();
            qCDebug(log_core_audio) << "Output device preferred format - Sample rate:" << nearestFormat.sampleRate() 
                                   << "Channels:" << nearestFormat.channelCount()
                                   << "Sample format:" << nearestFormat.sampleFormat();
        }
        
        m_audioSink.reset(new QAudioSink(m_outputDevice, m_format));
        
        // Connect to state changed signal to monitor sink health
        connect(m_audioSink.data(), &QAudioSink::stateChanged, this, [this](QAudio::State state) {
            if (state == QAudio::StoppedState && m_audioSink && m_audioSink->error() != QAudio::NoError) {
                QString errorMsg = QString("Audio sink error: %1").arg(m_audioSink->error());
                qCWarning(log_core_audio) << errorMsg;
                
                if (m_audioSink->error() == QAudio::UnderrunError || 
                    m_audioSink->error() == QAudio::IOError) {
                    qCWarning(log_core_audio) << "Audio output device may have an issue";
                }
            }
        });
        
        m_audioSink->setVolume(m_volume);
        m_sinkIODevice = m_audioSink->start();  // Get the IO device for writing

        if (!m_sinkIODevice) {
            qCWarning(log_core_audio) << "Failed to start audio sink";
            QString errorMsg = QString("Failed to start audio sink - Error: %1").arg(m_audioSink->error());
            emit error(errorMsg);
            
            // Clean up audio source
            if (m_audioSource) {
                delete m_audioSource;
                m_audioSource = nullptr;
            }
            return;
        }
        
        // Check for immediate errors after starting sink
        if (m_audioSink->error() != QAudio::NoError) {
            QString errorMsg = QString("Audio sink error immediately after start: %1").arg(m_audioSink->error());
            qCWarning(log_core_audio) << errorMsg;
            emit error(errorMsg);
            
            // Clean up
            if (m_audioSource) {
                delete m_audioSource;
                m_audioSource = nullptr;
            }
            return;
        }
        
        qCDebug(log_core_audio) << "Audio sink started successfully, state:" << m_audioSink->state();

        // Buffer for audio data
        const int bufferSize = 4096;  // Adjust buffer size based on your needs
        char buffer[bufferSize];
        
        qCDebug(log_core_audio) << "Entering main audio processing loop";
        int loopCount = 0;
        
        // Main audio processing loop
        while (true) {
            // Log every 10000 iterations to avoid spam but confirm the loop is running
            if (loopCount % 10000 == 0) {
                qCDebug(log_core_audio) << "Audio processing loop iteration:" << loopCount;
            }
            loopCount++;
            // CRITICAL: Check shutdown first before any other operations
            if (g_applicationShuttingDown.loadAcquire() == 1) {
                qCDebug(log_core_audio) << "AudioThread: Application shutdown detected in main loop - exiting immediately";
                // Don't touch any Qt Multimedia objects, just exit
                m_running = false;
                return;
            }
            
            // Thread-safe check of running state and cleanup status
            m_mutex.lock();
            bool shouldContinue = m_running && !m_cleanupStarted;
            m_mutex.unlock();
            
            if (!shouldContinue) {
                break;
            }
            
            // Check for thread interruption
            if (isInterruptionRequested()) {
                qCDebug(log_core_audio) << "AudioThread: Interruption requested, exiting loop";
                break;
            }
            
            // Check audio source health periodically
            if (m_audioSource && loopCount % 5000 == 0) {
                if (m_audioSource->error() != QAudio::NoError) {
                    qCWarning(log_core_audio) << "Audio source error detected in loop:" << m_audioSource->error();
                    
                    // If device is invalidated, stop gracefully
                    if (m_audioSource->error() == QAudio::IOError || 
                        m_audioSource->error() == QAudio::UnderrunError) {
                        qCWarning(log_core_audio) << "Audio device appears to be disconnected, stopping audio thread";
                        emit error("Audio input device disconnected or invalidated");
                        break;
                    }
                }
            }
            
            // Check audio sink health periodically
            if (m_audioSink && loopCount % 5000 == 0) {
                if (m_audioSink->error() != QAudio::NoError) {
                    qCWarning(log_core_audio) << "Audio sink error detected in loop:" << m_audioSink->error();
                    
                    if (m_audioSink->error() == QAudio::IOError || 
                        m_audioSink->error() == QAudio::UnderrunError) {
                        qCWarning(log_core_audio) << "Audio output device has an issue";
                    }
                }
            }
            
            // Check if there's data available to read
            if (m_audioIODevice) {
                qint64 bytesAvailable = m_audioIODevice->bytesAvailable();
                
                // Log audio device status occasionally
                if (loopCount % 5000 == 0) {
                    qCDebug(log_core_audio) << "Audio input status - bytesAvailable:" << bytesAvailable 
                                           << "isOpen:" << m_audioIODevice->isOpen()
                                           << "isReadable:" << m_audioIODevice->isReadable();
                    if (m_audioSource) {
                        qCDebug(log_core_audio) << "AudioSource state:" << m_audioSource->state()
                                               << "error:" << m_audioSource->error();
                    }
                }
                
                // Try to read even when bytesAvailable is 0 - Qt might be buffering
                if (bytesAvailable > 0 || (loopCount % 100 == 0)) {
                    // Read audio data from source (try even if bytesAvailable is 0)
                    qint64 bytesRead = m_audioIODevice->read(buffer, bufferSize);
                    
                    // Log any successful reads
                    if (bytesRead > 0) {
                        // qCDebug(log_core_audio) << "Audio data: read" << bytesRead << "bytes from" << bytesAvailable << "available";
                        
                        // Double-check we haven't started cleanup between reads
                        m_mutex.lock();
                        bool safeToWrite = !m_cleanupStarted && m_sinkIODevice;
                        m_mutex.unlock();
                        
                        if (safeToWrite) {                    
                            // Write processed data to sink's IO device
                            qint64 bytesWritten = m_sinkIODevice->write(buffer, bytesRead);
                            
                            // Log successful audio write
                            // qCDebug(log_core_audio) << "Audio data written:" << bytesWritten << "bytes";
                            
                            if (bytesWritten != bytesRead) {
                                qCDebug(log_core_audio) << "Audio write mismatch:" << bytesWritten << "vs" << bytesRead;
                            }
                        }
                    } else if (loopCount % 10000 == 0 && bytesAvailable == 0) {
                        qCDebug(log_core_audio) << "No audio data available - check if audio input signal is present";
                    }
                } else {
                    // No data available, sleep briefly to prevent CPU hogging
                    QThread::usleep(100);  // Sleep for 100 microseconds
                }
            } else {
                // Audio device is null - this shouldn't happen
                if (loopCount % 10000 == 0) {
                    qCWarning(log_core_audio) << "Audio input device is null!";
                }
                QThread::usleep(100);
            }
        }

        qCDebug(log_core_audio) << "Exited main audio processing loop";

        // Mark cleanup as started to prevent any further access to IO devices
        m_mutex.lock();
        m_cleanupStarted = true;
        m_mutex.unlock();

        // Cleanup - Use main thread for Qt Multimedia cleanup to prevent crashes

        qCDebug(log_core_audio) << "AudioThread cleanup starting...";
        
        // Check if application is shutting down
        if (g_applicationShuttingDown.loadAcquire() == 1) {
            qCDebug(log_core_audio) << "Application is shutting down - skipping ALL Qt Multimedia cleanup";
            
            // During application shutdown, don't touch Qt Multimedia objects at all
            // Just nullify our references and let the OS handle cleanup
            m_sinkIODevice = nullptr;
            m_audioIODevice = nullptr;
            
            // Don't call any Qt Multimedia methods - just nullify pointers
            if (m_audioSink) {
                m_audioSink.reset(); // Reset the pointer and let it clean up properly
            }
            m_audioSource = nullptr; // Don't delete
            
            qCDebug(log_core_audio) << "AudioThread shutdown cleanup completed successfully";
            return; // Exit early
        }
        
        // Normal cleanup path (not during application shutdown)
        // 1. First, nullify the IO devices to prevent any further access
        // These are owned by Qt Multimedia, not by us
        qCDebug(log_core_audio) << "Nullifying IO device pointers...";
        m_sinkIODevice = nullptr;
        m_audioIODevice = nullptr;
        
        // 2. For normal shutdown, use conservative cleanup to prevent crashes
        qCDebug(log_core_audio) << "Using conservative Qt Multimedia cleanup...";
        
        // Don't call stop() methods - just reset pointers
        if (m_audioSink) {
            m_audioSink.reset(); // This should be safe as it just nullifies
        }
        m_audioSource = nullptr; // Don't call stop() or delete
        
        qCDebug(log_core_audio) << "AudioThread cleanup completed successfully";

    } catch (const std::exception& e) {
        qCWarning(log_core_audio) << "Audio thread exception:" << e.what();
        emit error(QString("Audio thread exception: %1").arg(e.what()));
    } catch (...) {
        qCWarning(log_core_audio) << "Unknown exception in AudioThread";
        emit error("Unknown exception in AudioThread");
    }
    
    qCDebug(log_core_audio) << "AudioThread::run() exiting";
}
