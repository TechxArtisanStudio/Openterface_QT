#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <QObject>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QLoggingCategory>
#include <QTimer>

class AudioThread;

Q_DECLARE_LOGGING_CATEGORY(log_core_host_audio)

class AudioManager : public QObject {
    Q_OBJECT

public:
    explicit AudioManager(QObject *parent = nullptr);
    ~AudioManager();

    void initializeAudio();
    void disconnect();

private slots:
    void handleAudioError(const QString& error);

private:
    QAudioDevice findUvcCameraAudioDevice(QString deviceName);
    QAudioDevice findSystemAudioOuptutDevice(QString deviceName);
    void fadeInVolume(int timeout, int durationInSeconds);

    AudioThread* m_audioThread;
};

#endif // AUDIOMANAGER_H
