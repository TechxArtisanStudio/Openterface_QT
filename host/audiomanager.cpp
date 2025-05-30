#include "audiomanager.h"
#include "audiothread.h"
#include <QDebug>

Q_LOGGING_CATEGORY(log_core_host_audio, "opf.core.host.audio");

AudioManager::AudioManager(QObject *parent)
    : QObject(parent), m_audioThread(nullptr) {
    qCDebug(log_core_host_audio) << "AudioManager init...";
}

AudioManager::~AudioManager() {
    disconnect();
}

QAudioDevice AudioManager::findUvcCameraAudioDevice(QString deviceName) {
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    for (const QAudioDevice &device : devices) {
        qCDebug(log_core_host_audio) << "Audio Input Device name:" << device.description() << ", ID:" << device.id();

        // Check if the device name or other properties match the UVC camera's audio device
        if (device.description().contains(deviceName)) {
            return device;
        }
    }
    return QAudioDevice(); // Return default device if not found
}

QAudioDevice AudioManager::findSystemAudioOuptutDevice(QString deviceName) {
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    for (const QAudioDevice &device : devices) {
        qCDebug(log_core_host_audio) << "Audio Output Device name:" << device.description() << ", ID:" << device.id();

        if (device.description().contains(deviceName)) {
            return device;
        }
    }
    return QAudioDevice(); // Return default device if not found
}

void AudioManager::initializeAudio() {
    qDebug() << "Initializing audio...";
    QAudioDevice inputDevice = findUvcCameraAudioDevice("OpenterfaceA");
    QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();

    if (outputDevice.isNull()) {
        qCWarning(log_core_host_audio) << "No audio output device found.";
        return;
    }

    try {
        QAudioFormat format = outputDevice.preferredFormat();

        // Log the input device and format details
        qCDebug(log_core_host_audio) << "Initializing audio with input device:" << inputDevice.description();
        qCDebug(log_core_host_audio) << "Audio format details:";
        qCDebug(log_core_host_audio) << "Sample rate:" << format.sampleRate();
        qCDebug(log_core_host_audio) << "Channel count:" << format.channelCount();
        qCDebug(log_core_host_audio) << "Sample size:" << format.bytesPerSample();

        // Create and start the audio thread
        m_audioThread = new AudioThread(inputDevice, outputDevice, format, this);
        connect(m_audioThread, &AudioThread::error, this, &AudioManager::handleAudioError);
        m_audioThread->start();

        // Initialize volume to 0 and start fade-in
        m_audioThread->setVolume(0.0);
        fadeInVolume(100, 3);

    } catch (const std::exception &e) {
        qCWarning(log_core_host_audio) << "Exception occurred during audio initialization:" << e.what();
        return;
    }
}

void AudioManager::fadeInVolume(int timeout, int durationInSeconds) {
    if (!m_audioThread) return;

    // Calculate the number of steps and the increment per step
    int steps = (durationInSeconds * 1000) / timeout;
    qreal increment = 1.0 / steps;

    // Create a QTimer to handle the volume fade-in
    QTimer *volumeTimer = new QTimer(this);
    connect(volumeTimer, &QTimer::timeout, [this, volumeTimer, increment]() {
        if (!m_audioThread) return;

        qreal currentVolume = m_audioThread->volume();

        if (currentVolume < 1.0) {
            m_audioThread->setVolume(currentVolume + increment);
        } else {
            m_audioThread->setVolume(1.0);
            volumeTimer->stop();
            volumeTimer->deleteLater();
        }
    });

    // Start the timer with the given interval
    volumeTimer->start(timeout);
}

void AudioManager::disconnect() {
    qCDebug(log_core_host_audio) << "Disconnecting audio thread.";
    if (m_audioThread) {
        m_audioThread->stop();
        m_audioThread->wait();
        delete m_audioThread;
        m_audioThread = nullptr;
    }
}

void AudioManager::handleAudioError(const QString& error) {
    qCWarning(log_core_host_audio) << "Audio error:" << error;
}
