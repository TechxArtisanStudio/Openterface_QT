#ifndef VIDEOHID_H
#define VIDEOHID_H

#include <QObject>

#ifdef _WIN32
#include <windows.h> 
#elif __linux__
#include <linux/hid.h>
#endif

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
    float getFps();
    bool getGpio0();


private:
    explicit VideoHid(QObject *parent = nullptr);

    QString extractPortNumberFromPath(const QString& path);
    QPair<QByteArray, bool> usbXdataRead4Byte(quint16 u16_address);
    std::wstring GetHIDDevicePath();
    QString devicePath;
    
#ifdef _WIN32
    bool sendFeatureReport(BYTE reportId, BYTE* reportBuffer, DWORD bufferSize);
    bool getFeatureReport(BYTE reportId, BYTE* reportBuffer, DWORD bufferSize);
#elif __linux__
    bool sendFeatureReport(int fd, unsigned char* reportBuffer, int bufferSize);
    bool getFeatureReport(int fd, unsigned char* reportBuffer, int bufferSize);
#endif

};

#endif // VIDEOHID_H
