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
    qCDebug(log_core_audio) << "AudioThread created for device:" << inputDevice.description();
}

AudioThread::~AudioThread()
{
    // Always stop the thread to prevent "destroyed while running" error
    qCDebug(log_core_audio) << "AudioThread destructor called";
    
    if (g_applicationShuttingDown.loadAcquire() == 1) {
        qCDebug(log_core_audio) << "Application shutting down - forcing thread stop";
        
        // CRITICAL: During shutdown, do minimal cleanup to prevent Qt Multimedia crashes
        m_running = false;
        m_cleanupStarted = true;
        requestInterruption();
        
        // Nullify Qt Multimedia pointers immediately without calling their methods
        m_sinkIODevice = nullptr;
        m_audioIODevice = nullptr;
        
        // Don't call any Qt Multimedia cleanup methods during shutdown
        if (m_audioSink) {
            // Don't call stop() or any methods - just reset the pointer
            m_audioSink.reset(); // Reset the pointer and let it clean up properly
        }
        m_audioSource = nullptr; // Don't delete, just nullify
        
        // Force terminate the thread during shutdown - don't wait long
        if (isRunning()) {
            if (!wait(10)) {  // Very short wait - 10ms
                qCDebug(log_core_audio) << "AudioThread: Terminating thread forcefully during shutdown";
                terminate();
                wait(10);  // Short wait after terminate
            }
        }
        
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
    qCDebug(log_core_audio) << "AudioThread starting";
    m_running = true;

    try {
        qCDebug(log_core_audio) << "Creating audio source and sink";
        qCDebug(log_core_audio) << "Format:" << m_format.sampleRate() << "Hz," << m_format.channelCount() << "ch," << m_format.sampleFormat();
        
        // Check if the input device supports the format
        if (!m_inputDevice.isFormatSupported(m_format)) {
            qCWarning(log_core_audio) << "Input device does not support format - using preferred format";
        }
        
        m_audioSource = new QAudioSource(m_inputDevice, m_format);
        
        // Try to force the audio source to start actively
        m_audioIODevice = m_audioSource->start();

        if (!m_audioIODevice) {
            qCWarning(log_core_audio) << "Failed to start audio source";
            emit error("Failed to start audio source");
            return;
        }
        
        // Give the device a moment to initialize
        QThread::msleep(100);
        
        qCDebug(log_core_audio) << "Audio source state:" << m_audioSource->state();
        
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
        m_audioSink->setVolume(m_volume);
        m_sinkIODevice = m_audioSink->start();  // Get the IO device for writing

        if (!m_sinkIODevice) {
            qCWarning(log_core_audio) << "Failed to start audio sink";
            emit error("Failed to start audio sink");
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
            // Log every 50000 iterations to reduce spam
            if (loopCount % 50000 == 0) {
                qCDebug(log_core_audio) << "Audio loop running, iteration:" << loopCount;
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
            
            // Check if there's data available to read
            if (m_audioIODevice) {
                qint64 bytesAvailable = m_audioIODevice->bytesAvailable();
                
                // Log audio device status less frequently
                if (loopCount % 25000 == 0) {
                    qCDebug(log_core_audio) << "Audio status - available:" << bytesAvailable 
                                           << "state:" << (m_audioSource ? m_audioSource->state() : 0);
                }
                
                // Try to read even when bytesAvailable is 0 - Qt might be buffering
                if (bytesAvailable > 0 || (loopCount % 100 == 0)) {
                    // Read audio data from source (try even if bytesAvailable is 0)
                    qint64 bytesRead = m_audioIODevice->read(buffer, bufferSize);
                    
                    // Log any successful reads
                    if (bytesRead > 0) {
                        qCDebug(log_core_audio) << "Audio data: read" << bytesRead << "bytes from" << bytesAvailable << "available";
                        
                        // Double-check we haven't started cleanup between reads
                        m_mutex.lock();
                        bool safeToWrite = !m_cleanupStarted && m_sinkIODevice;
                        m_mutex.unlock();
                        
                        if (safeToWrite) {                    
                            // Write processed data to sink's IO device
                            qint64 bytesWritten = m_sinkIODevice->write(buffer, bytesRead);
                            
                            // Log successful audio write
                            qCDebug(log_core_audio) << "Audio data written:" << bytesWritten << "bytes";
                            
                            if (bytesWritten != bytesRead) {
                                qCDebug(log_core_audio) << "Audio write mismatch:" << bytesWritten << "vs" << bytesRead;
                            }
                        }
                    } else if (loopCount % 100000 == 0 && bytesAvailable == 0) {
                        qCDebug(log_core_audio) << "No audio data - check input signal";
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

        qCDebug(log_core_audio) << "Audio thread cleanup starting";
        
        // Check if application is shutting down
        if (g_applicationShuttingDown.loadAcquire() == 1) {
            qCDebug(log_core_audio) << "Application shutdown - minimal cleanup";
            
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
