#include "audiothread.h"
#include "../global.h"
#include <QDebug>

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
}

AudioThread::~AudioThread()
{
    // Always stop the thread to prevent "destroyed while running" error
    qDebug() << "AudioThread destructor called";
    
    if (g_applicationShuttingDown.loadAcquire() == 1) {
        qDebug() << "AudioThread destructor: Application shutting down - forcing thread stop";
        
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
            m_audioSink.take(); // Take ownership and let it leak during shutdown
        }
        m_audioSource = nullptr; // Don't delete, just nullify
        
        // Force terminate the thread during shutdown - don't wait long
        if (isRunning()) {
            if (!wait(10)) {  // Very short wait - 10ms
                qDebug() << "AudioThread: Terminating thread forcefully during shutdown";
                terminate();
                wait(10);  // Short wait after terminate
            }
        }
        
        qDebug() << "AudioThread destructor: Thread forcefully stopped during shutdown";
        return;
    }
    
    // Normal cleanup path (not during application shutdown)
    // Ensure proper cleanup order
    stop();
    
    // Give the thread time to finish cleanup
    if (!wait(2000)) {
        // Force terminate if it doesn't finish in 2 seconds
        qWarning() << "AudioThread taking too long to finish, forcing termination";
        terminate();
        wait(1000);
    }
}

void AudioThread::stop()
{
    qDebug() << "AudioThread::stop() called";
    
    // Check if application is shutting down
    if (g_applicationShuttingDown.loadAcquire() == 1) {
        qDebug() << "AudioThread::stop() - Application shutting down, minimal stop";
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
    qDebug() << "AudioThread::cleanupMultimediaObjects() - skipping to prevent crashes";
    // This method is now a no-op to prevent Qt Multimedia crashes during shutdown
}

void AudioThread::run()
{
    m_running = true;

    try {
        m_audioSource = new QAudioSource(m_inputDevice, m_format);
        m_audioIODevice = m_audioSource->start();

        if (!m_audioIODevice) {
            emit error("Failed to start audio source");
            return;
        }

        m_audioSink.reset(new QAudioSink(m_outputDevice, m_format));
        m_audioSink->setVolume(m_volume);
        m_sinkIODevice = m_audioSink->start();  // Get the IO device for writing

        if (!m_sinkIODevice) {
            emit error("Failed to start audio sink");
            return;
        }

        // Buffer for audio data
        const int bufferSize = 4096;  // Adjust buffer size based on your needs
        char buffer[bufferSize];
        
        // Main audio processing loop
        while (true) {
            // CRITICAL: Check shutdown first before any other operations
            if (g_applicationShuttingDown.loadAcquire() == 1) {
                qDebug() << "AudioThread: Application shutdown detected in main loop - exiting immediately";
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
                qDebug() << "AudioThread: Interruption requested, exiting loop";
                break;
            }
            
            // Check if there's data available to read
            if (m_audioIODevice && m_audioIODevice->bytesAvailable() > 0) {
                // Read audio data from source
                qint64 bytesRead = m_audioIODevice->read(buffer, bufferSize);
                
                // Double-check we haven't started cleanup between reads
                m_mutex.lock();
                bool safeToWrite = !m_cleanupStarted && m_sinkIODevice;
                m_mutex.unlock();
                
                if (bytesRead > 0 && safeToWrite) {                    
                    // Write processed data to sink's IO device
                    qint64 bytesWritten = m_sinkIODevice->write(buffer, bytesRead);
                    
                    if (bytesWritten != bytesRead) {
                        qDebug() << "Audio write mismatch:" << bytesWritten << "vs" << bytesRead;
                    }
                }
            } else {
                // No data available, sleep briefly to prevent CPU hogging
                QThread::usleep(100);  // Sleep for 100 microseconds
            }
        }

        // Mark cleanup as started to prevent any further access to IO devices
        m_mutex.lock();
        m_cleanupStarted = true;
        m_mutex.unlock();

        // Cleanup - Use main thread for Qt Multimedia cleanup to prevent crashes

        qDebug() << "AudioThread cleanup starting...";
        
        // Check if application is shutting down
        if (g_applicationShuttingDown.loadAcquire() == 1) {
            qDebug() << "Application is shutting down - skipping ALL Qt Multimedia cleanup";
            
            // During application shutdown, don't touch Qt Multimedia objects at all
            // Just nullify our references and let the OS handle cleanup
            m_sinkIODevice = nullptr;
            m_audioIODevice = nullptr;
            
            // Don't call any Qt Multimedia methods - just nullify pointers
            if (m_audioSink) {
                m_audioSink.take(); // Take ownership and let it leak during shutdown
            }
            m_audioSource = nullptr; // Don't delete
            
            qDebug() << "AudioThread shutdown cleanup completed successfully";
            return; // Exit early
        }
        
        // Normal cleanup path (not during application shutdown)
        // 1. First, nullify the IO devices to prevent any further access
        // These are owned by Qt Multimedia, not by us
        qDebug() << "Nullifying IO device pointers...";
        m_sinkIODevice = nullptr;
        m_audioIODevice = nullptr;
        
        // 2. For normal shutdown, use conservative cleanup to prevent crashes
        qDebug() << "Using conservative Qt Multimedia cleanup...";
        
        // Don't call stop() methods - just reset pointers
        if (m_audioSink) {
            m_audioSink.reset(); // This should be safe as it just nullifies
        }
        m_audioSource = nullptr; // Don't call stop() or delete
        
        qDebug() << "AudioThread cleanup completed successfully";

    } catch (const std::exception& e) {
        emit error(QString("Audio thread exception: %1").arg(e.what()));
    }
}
