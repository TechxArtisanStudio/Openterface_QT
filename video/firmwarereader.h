#ifndef FIRMWARE_READER_H
#define FIRMWARE_READER_H

#include <QObject>
#include <QByteArray>
#include <QString>

class VideoHid;

class FirmwareReader : public QObject
{
    Q_OBJECT

public:
    explicit FirmwareReader(VideoHid* videoHid, quint16 address, quint32 size, const QString& outputFilePath, QObject* parent = nullptr);

public slots:
    void process();

signals:
    void progress(int percent);
    void finished(bool success);
    void error(const QString& errorMessage);

private:
    VideoHid* m_videoHid;
    quint16 m_address;
    quint32 m_size;
    QString m_outputFilePath;
};

#endif // FIRMWARE_READER_H