#include "audiomanager.h"
#include <QDebug>

Q_LOGGING_CATEGORY(log_core_host_audio, "opf.core.host.audio");

AudioManager::AudioManager(QObject *parent)
    : QObject(parent) {
    qCDebug(log_core_host_audio) << "AudioManager init...";
}

AudioManager::~AudioManager() {

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

        audioSource = new QAudioSource(inputDevice, format);
        audioIODevice = audioSource->start();

        if (!audioIODevice) {
            qCWarning(log_core_host_audio) << "Failed to start audio source.";
            delete audioSource;
            return;
        }

        m_audioSink.reset(new QAudioSink(outputDevice, format));
        m_audioSink->start(audioIODevice);

        qCDebug(log_core_host_audio) << "Audio sink started successfully with test audio.";

        // Initialize volume to 0 and start fade-in
        fadeInVolume(100, 3);

    } catch (const std::exception &e) {
        qCWarning(log_core_host_audio) << "Exception occurred during audio initialization:" << e.what();
        delete audioSource;
        return;
    }
}

void AudioManager::fadeInVolume(int timeout, int durationInSeconds) {
    // Initialize volume to 0
    m_audioSink->setVolume(0.0);

    // Calculate the number of steps and the increment per step
    int steps = (durationInSeconds * 1000) / timeout;
    qreal increment = 1.0 / steps;

    // Create a QTimer to handle the volume fade-in
    QTimer *volumeTimer = new QTimer(this);
    connect(volumeTimer, &QTimer::timeout, [this, volumeTimer, increment]() {
        qreal currentVolume = m_audioSink->volume();

        if (currentVolume < 1.0) {
            m_audioSink->setVolume(currentVolume + increment);
        } else {
            m_audioSink->setVolume(1.0);
            volumeTimer->stop();
            volumeTimer->deleteLater();
        }
    });

    // Start the timer with the given interval
    volumeTimer->start(timeout);
}

void AudioManager::disconnect() {
    qCDebug(log_core_host_audio)<< "Disconnecting audio source and sink.";
    if (audioSource) {
        qCDebug(log_core_host_audio) << "Stopping audio source.";
        //audioSource->stop();
        // delete audioSource;
        // audioSource = nullptr;
    }

    if (audioIODevice) {
        qCDebug(log_core_host_audio) << "Closing audio IO device.";
        //audioIODevice->close();
        qCDebug(log_core_host_audio) << "Closing audio IO device.222";
        // delete audioIODevice;
        // qDebug() << "Closing audio IO device.333";
        // audioIODevice = nullptr;
    }
    qCDebug(log_core_host_audio) << "Resetting audio sink.";
    // m_audioSink.reset();
}
