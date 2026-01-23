#ifndef FIRMWAREWRITER_H
#define FIRMWAREWRITER_H

#include <QObject>
#include <QByteArray>

class VideoHid;

class FirmwareWriter : public QObject 
{
    Q_OBJECT
    
public:
    explicit FirmwareWriter(VideoHid* videoHid, quint16 address, const QByteArray& firmware, QObject* parent = nullptr);
    
public slots:
    void process();
    void onChunkWritten(int writtenBytes); // slot invoked when VideoHid reports chunk written
    
signals:
    void progress(int percent);
    void finished(bool success);
    
private:
    VideoHid* m_videoHid;
    quint16 m_address;
    QByteArray m_firmware;

    // Progress tracking state
    int m_totalSize{0};
    std::atomic_int m_lastPercent{0};
};

#endif // FIRMWAREWRITER_H
