#ifdef __linux__

#include "LinuxHIDTransport.h"
#include "../videohid.h"


#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include "log/opflogging.h"
#include "../../ui/globalsetting.h"

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <sys/ioctl.h>
#include <linux/hid.h>
#include <linux/hidraw.h>

OPF_LOGGING_CATEGORY(log_linux_transport, "opf.host.linux_transport")

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
        fprintf(stderr, "[DEBUG-HID] open: no HID device path found\n");
        qCDebug(log_linux_transport) << "open: no HID device path found";
        return false;
    }

    fprintf(stderr, "[DEBUG-HID] Opening device: %s\n", devicePath.toUtf8().constData());
    m_hidFd = ::open(devicePath.toStdString().c_str(), O_RDWR);
    if (m_hidFd < 0) {
        fprintf(stderr, "[DEBUG-HID] open failed: %s, error: %s\n",
                devicePath.toUtf8().constData(), strerror(errno));
        qCDebug(log_linux_transport) << "open: failed to open" << devicePath
                                     << "error:" << strerror(errno);
        return false;
    }
    fprintf(stderr, "[DEBUG-HID] Device opened successfully, fd=%d\n", m_hidFd);
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
    if (!isOpen() && !open()) return false;

    std::vector<uint8_t> buffer(buf, buf + len);
    int res = ioctl(m_hidFd, HIDIOCSFEATURE(buffer.size()), buffer.data());
    if (res < 0) {
        qCDebug(log_linux_transport) << "sendFeatureReport failed:" << strerror(errno);
        return false;
    }
    return true;
}

bool LinuxHIDTransport::getFeatureReport(uint8_t* buf, size_t len)
{
    if (!isOpen() && !open()) return false;

    std::vector<uint8_t> buffer(len, 0);

    if (len > 0) {
        // copy at least Report ID
        buffer[0] = buf[0];
    }
    int res = ioctl(m_hidFd, HIDIOCGFEATURE(buffer.size()), buffer.data());
    if (res < 0) {
        qCDebug(log_linux_transport) << "getFeatureReport failed:" << strerror(errno);
        return false;
    }
    std::copy(buffer.begin(), buffer.end(), buf);
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
    // Use the device VideoHid is bound to. It decides the path once, at
    // start() / switchToHIDDeviceByPortChain(); re-deriving it here from the
    // global "current port chain" on every open is wrong with several units
    // attached: that setting is rewritten by the serial/composite switch steps
    // and, when it names no HID device, the name-based fallback below returns
    // the FIRST capture chip -- so polling and EEPROM reads silently drifted
    // to other units after a switch.
    if (m_owner && !m_owner->getCurrentHIDDevicePath().isEmpty()) {
        return m_owner->getCurrentHIDDevicePath();
    }
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
