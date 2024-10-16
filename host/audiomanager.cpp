#include "audiomanager.h"
#include <QDebug>

AudioManager::AudioManager(QObject *parent)
    : QObject(parent) {
    qDebug() << "AudioManager init...";
}

AudioManager::~AudioManager() {

}

QAudioDevice AudioManager::findUvcCameraAudioDevice(QString deviceName) {
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    for (const QAudioDevice &device : devices) {
        qDebug() << "Audio Input Device name:" << device.description() << ", ID:" << device.id();

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
        qDebug() << "Audio Output Device name:" << device.description() << ", ID:" << device.id();

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
        qWarning() << "No audio output device found.";
        return;
    }

    try {

        QAudioFormat format = outputDevice.preferredFormat();

        // Log the input device and format details
        qDebug() << "Initializing audio with input device:" << inputDevice.description();
        qDebug() << "Audio format details:";
        qDebug() << "Sample rate:" << format.sampleRate();
        qDebug() << "Channel count:" << format.channelCount();
        qDebug() << "Sample size:" << format.bytesPerSample();

        audioSource = new QAudioSource(inputDevice, format);
        audioIODevice = audioSource->start();

        if (!audioIODevice) {
            qWarning() << "Failed to start audio source.";
            delete audioSource;
            return;
        }

        m_audioSink.reset(new QAudioSink(outputDevice, format));
        m_audioSink->start(audioIODevice);

        qDebug() << "Audio sink started successfully with test audio.";

        // Initialize volume to 0 and start fade-in
        fadeInVolume(100, 3);

    } catch (const std::exception &e) {
        qWarning() << "Exception occurred during audio initialization:" << e.what();
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
    qDebug() << "Disconnecting audio source and sink.";
    if (audioSource) {
        qDebug() << "Stopping audio source.";
        //audioSource->stop();
        // delete audioSource;
        // audioSource = nullptr;
    }

    if (audioIODevice) {
        qDebug() << "Closing audio IO device.";
        //audioIODevice->close();
        qDebug() << "Closing audio IO device.222";
        // delete audioIODevice;
        // qDebug() << "Closing audio IO device.333";
        // audioIODevice = nullptr;
    }
    qDebug() << "Resetting audio sink.";
    // m_audioSink.reset();
}
