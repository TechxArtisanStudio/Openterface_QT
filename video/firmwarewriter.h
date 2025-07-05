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
    
signals:
    void progress(int percent);
    void finished(bool success);
    
private:
    VideoHid* m_videoHid;
    quint16 m_address;
    QByteArray m_firmware;
};

#endif // FIRMWAREWRITER_H
