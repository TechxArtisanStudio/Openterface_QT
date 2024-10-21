#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <QObject>
#include <QAudioSource>
#include <QAudioSink>
#include <QIODevice>
#include <QTimer>
#include <QMediaDevices>
#include <QLoggingCategory>

class AudioManager : public QObject {
    Q_OBJECT

public:
    explicit AudioManager(QObject *parent = nullptr);
    ~AudioManager();

    void initializeAudio();
    void disconnect();
    
    QAudioDevice findUvcCameraAudioDevice(QString deviceName);
    QAudioDevice findSystemAudioOuptutDevice(QString deviceName);
    void fadeInVolume(int timeout, int durationInSeconds);

private:
    QScopedPointer<QAudioSink> m_audioSink;
    QAudioSource *audioSource = nullptr;
    QIODevice *audioIODevice = nullptr;
};

#endif // AUDIOMANAGER_H
