#ifndef PLATFORMHIDADAPTER_H
#define PLATFORMHIDADAPTER_H

#include <QString>
#include <cstddef>
#include <cstdint>

class VideoHid;

class PlatformHidAdapter {
public:
    explicit PlatformHidAdapter(VideoHid* owner) : m_owner(owner) {}
    virtual ~PlatformHidAdapter() {}

    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool sendFeatureReport(uint8_t* buffer, size_t bufferLength) = 0;
    virtual bool getFeatureReport(uint8_t* buffer, size_t bufferLength) = 0;
    virtual QString getHIDDevicePath() = 0;

    static PlatformHidAdapter* create(VideoHid* owner);

protected:
    VideoHid* m_owner{nullptr};
};

#ifdef _WIN32
class WindowsHidAdapter : public PlatformHidAdapter {
public:
    explicit WindowsHidAdapter(VideoHid* owner) : PlatformHidAdapter(owner) {}
    bool open() override;
    void close() override;
    bool sendFeatureReport(uint8_t* buffer, size_t bufferLength) override;
    bool getFeatureReport(uint8_t* buffer, size_t bufferLength) override;
    QString getHIDDevicePath() override;
};
#elif defined(__linux__)
class LinuxHidAdapter : public PlatformHidAdapter {
public:
    explicit LinuxHidAdapter(VideoHid* owner) : PlatformHidAdapter(owner) {}
    bool open() override;
    void close() override;
    bool sendFeatureReport(uint8_t* buffer, size_t bufferLength) override;
    bool getFeatureReport(uint8_t* buffer, size_t bufferLength) override;
    QString getHIDDevicePath() override;
};
#endif

#endif // PLATFORMHIDADAPTER_H
