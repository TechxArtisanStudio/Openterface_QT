#ifndef MF_DEVICE_ENUMERATOR_H
#define MF_DEVICE_ENUMERATOR_H

#include <QString>
#include <QList>

struct MfDeviceInfo {
    QString friendlyName;
    QString symbolicLink;
    int index;
};

class MfDeviceEnumerator {
public:
    MfDeviceEnumerator();
    ~MfDeviceEnumerator();

    QList<MfDeviceInfo> enumerateVideoDevices();
    QString getDeviceSymbolicLink(int index) const;
    int getDeviceCount() const;

private:
    QList<MfDeviceInfo> deviceList_;
};

#endif // MF_DEVICE_ENUMERATOR_H
