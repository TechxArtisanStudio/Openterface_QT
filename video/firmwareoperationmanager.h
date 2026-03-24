#ifndef FIRMWAREOPERATIONMANAGER_H
#define FIRMWAREOPERATIONMANAGER_H

#include <QObject>
#include <QThread>
#include <QPointer>

class FirmwareReader;
class FirmwareWriter;
class VideoHid;

class FirmwareOperationManager : public QObject
{
    Q_OBJECT

public:
    explicit FirmwareOperationManager(VideoHid* videoHid, quint16 address, QObject* parent = nullptr);
    ~FirmwareOperationManager() override;

    void readFirmware(quint32 firmwareSize, const QString& tempFirmwarePath);
    void writeFirmware(const QByteArray& firmwareData, const QString& tempFirmwarePath);
    void cancel();

signals:
    void progress(int percent);
    void readFinished(bool success, const QByteArray &firmwareData, const QString& errorMessage);
    void readCompleted(bool success, const QByteArray &firmwareData, const QString &errorMessage);
    void writeFinished(bool success, const QString& errorMessage);
    void writeCompleted(bool success);

private slots:
    void onReaderProgress(int percent);
    void onReaderFinished(bool success, const QByteArray &firmwareData = QByteArray());
    void onReaderError(const QString& message);
    void onWriterProgress(int percent);
    void onWriterFinished(bool success);
    void onThreadFinished();

private:
    void cleanupWorker();

    VideoHid* m_videoHid;
    quint16 m_address;
    QThread* m_thread;
    FirmwareReader* m_reader;
    FirmwareWriter* m_writer;
    QString m_tempFirmwarePath;
    QByteArray m_readFirmwareData;
    bool m_writeSuccess;
    bool m_readSuccess;
    QString m_readError;
    bool m_isWriteOperation;
};

#endif // FIRMWAREOPERATIONMANAGER_H
