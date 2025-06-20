#include "audiothread.h"
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
    , m_volume(1.0)
{
}

AudioThread::~AudioThread()
{
    stop();
    wait();
}

void AudioThread::stop()
{
    m_running = false;
}

void AudioThread::setVolume(qreal volume)
{
    m_mutex.lock();
    m_volume = volume;
    if (m_audioSink) {
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
        while (m_running) {
            // Check if there's data available to read
            if (m_audioIODevice->bytesAvailable() > 0) {
                // Read audio data from source
                qint64 bytesRead = m_audioIODevice->read(buffer, bufferSize);
                
                if (bytesRead > 0) {                    
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

        // Cleanup

        if (m_audioIODevice) {
            qDebug() << "Closing audio IO device.";
            m_audioIODevice->close();
            m_audioIODevice = nullptr;
        }

        if (m_audioSource) {
            qDebug() << "Stopping audio source.";
            m_audioSource->stop();
            delete m_audioSource;
            m_audioSource = nullptr;
        }

        if (m_sinkIODevice) {
            m_sinkIODevice->close();
            m_sinkIODevice = nullptr;
        }

        m_audioSink.reset();

    } catch (const std::exception& e) {
        emit error(QString("Audio thread exception: %1").arg(e.what()));
    }
}
