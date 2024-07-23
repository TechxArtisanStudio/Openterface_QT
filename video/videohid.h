#ifndef VIDEOHID_H
#define VIDEOHID_H

#include <QObject>
#include <windows.h> 

class VideoHid : public QObject
{
public:
    static VideoHid& getInstance()
    {
        static VideoHid instance; // Guaranteed to be destroyed.
            // Instantiated on first use.
        return instance;
    }


    VideoHid(VideoHid const&) = delete;             // Copy construct
    void operator=(VideoHid const&)  = delete; // Copy assign

    void start();
    void stop();
    //get resolution
    QPair<int, int> getResolution();
    bool getGpio0();


private:
    explicit VideoHid(QObject *parent = nullptr);

    QString extractPortNumberFromPath(const QString& path);
    bool sendFeatureReport(BYTE reportId, BYTE* reportBuffer, DWORD bufferSize);
    bool getFeatureReport(BYTE reportId, BYTE* reportBuffer, DWORD bufferSize);
    QPair<QByteArray, bool> usbXdataRead4Byte(quint16 u16_address);
    std::wstring GetHIDDevicePath();
    QString devicePath;
};

#endif // VIDEOHID_H
