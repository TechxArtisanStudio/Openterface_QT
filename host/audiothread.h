#ifndef AUDIOTHREAD_H
#define AUDIOTHREAD_H

#include <QThread>
#include <QAudioSource>
#include <QAudioSink>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QMutex>

class AudioThread : public QThread {
    Q_OBJECT

public:
    AudioThread(const QAudioDevice& inputDevice, 
               const QAudioDevice& outputDevice,
               const QAudioFormat& format,
               QObject* parent = nullptr);
    ~AudioThread();

    void stop();
    void setVolume(qreal volume);
    qreal volume() const;

signals:
    void error(const QString& message);

protected:
    void run() override;

private:
    QAudioDevice m_inputDevice;
    QAudioDevice m_outputDevice;
    QAudioFormat m_format;
    QAudioSource* m_audioSource;
    QScopedPointer<QAudioSink> m_audioSink;
    QIODevice* m_audioIODevice;    // For reading from source
    QIODevice* m_sinkIODevice;     // For writing to sink
    bool m_running;
    mutable QMutex m_mutex;  // Make the mutex mutable so it can be locked in const functions
    qreal m_volume;
};

#endif // AUDIOTHREAD_H
