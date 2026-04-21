#ifdef __linux__

#include "LinuxHIDTransport.h"
#include "../videohid.h"


#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QLoggingCategory>
#include "../../ui/globalsetting.h"

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <sys/ioctl.h>
#include <linux/hid.h>
#include <linux/hidraw.h>

Q_LOGGING_CATEGORY(log_linux_transport, "opf.host.linux_transport")

LinuxHIDTransport::LinuxHIDTransport(VideoHid* owner)
    : m_owner(owner)
{
}

LinuxHIDTransport::~LinuxHIDTransport()
{
    close();
}

bool LinuxHIDTransport::isOpen() const
{
    return m_hidFd >= 0;
}

bool LinuxHIDTransport::open()
{
    if (m_hidFd >= 0) return true;   // already open

    QString devicePath = getHIDDevicePath();
    if (devicePath.isEmpty()) {
        qCDebug(log_linux_transport) << "open: no HID device path found";
        return false;
    }

    m_hidFd = ::open(devicePath.toStdString().c_str(), O_RDWR);
    if (m_hidFd < 0) {
        qCDebug(log_linux_transport) << "open: failed to open" << devicePath
                                     << "error:" << strerror(errno);
        return false;
    }
    return true;
}

void LinuxHIDTransport::close()
{
    if (m_hidFd >= 0) {
        ::close(m_hidFd);
        m_hidFd = -1;
    }
}

bool LinuxHIDTransport::sendFeatureReport(uint8_t* buf, size_t len)
{
    bool opened = false;
    if (!isOpen()) {
        if (!open()) return false;
        opened = true;
    }

    std::vector<uint8_t> buffer(buf, buf + len);
    int res = ioctl(m_hidFd, HIDIOCSFEATURE(buffer.size()), buffer.data());
    if (res < 0) {
        qCDebug(log_linux_transport) << "sendFeatureReport failed:" << strerror(errno);
        if (opened) close();
        return false;
    }
    if (opened) close();
    return true;
}

bool LinuxHIDTransport::getFeatureReport(uint8_t* buf, size_t len)
{
    bool opened = false;
    if (!isOpen()) {
        if (!open()) return false;
        opened = true;
    }

    std::vector<uint8_t> buffer(len, 0);
    int res = ioctl(m_hidFd, HIDIOCGFEATURE(buffer.size()), buffer.data());
    if (res < 0) {
        qCDebug(log_linux_transport) << "getFeatureReport failed:" << strerror(errno);
        if (opened) close();
        return false;
    }
    std::copy(buffer.begin(), buffer.end(), buf);
    if (opened) close();
    return true;
}

bool LinuxHIDTransport::sendDirect(uint8_t* buf, size_t len)
{
    return sendFeatureReport(buf, len);
}

bool LinuxHIDTransport::getDirect(uint8_t* buf, size_t len)
{
    return getFeatureReport(buf, len);
}

QString LinuxHIDTransport::getHIDDevicePath()
{
    if (m_owner) {
        QString portChain = GlobalSetting::instance().getOpenterfacePortChain();
        QString hidPath = m_owner->findMatchingHIDDevice(portChain);
        if (!hidPath.isEmpty())
            return hidPath;
    }

    // Fallback: enumerate /sys/class/hidraw
    qCDebug(log_linux_transport) << "Falling back to device name enumeration for HID discovery";

    QDir dir("/sys/class/hidraw");
    QStringList devices = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    if (devices.isEmpty()) {
        qCDebug(log_linux_transport) << "No hidraw devices found";
        return {};
    }

    for (const QString& device : devices) {
        QString ueventPath = "/sys/class/hidraw/" + device + "/device/uevent";
        QFile file(ueventPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.isEmpty()) break;
            if (line.contains("HID_NAME")) {
                if (line.contains("Openterface") || line.contains("MACROSILICON")) {
                    return "/dev/" + device;
                }
            }
        }
    }
    return {};
}

#endif // __linux__
