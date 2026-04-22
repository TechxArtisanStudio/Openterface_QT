// ChipDetector.cpp  platform-specific VID/PID detection and chip factory.
//
// Detection strategy:
//   Windows : parse VID/PID tokens from the HID device path string.
//   Linux   : walk the sysfs tree from the hidraw device node; fall back to
//             path string matching if sysfs is unavailable.

#include "ChipDetector.h"
// videohid.h is included here (not in the header) because Ms2130sChip's
// constructor takes a VideoHid*  a Phase 1 compromise until the Windows
// synchronous flash handle is moved into the platform transport layer.
#include "../videohid.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

// Reuse the HID logging category defined in videohid.cpp.
Q_DECLARE_LOGGING_CATEGORY(log_host_hid)

// 
//  detect()
// 
VideoChipType ChipDetector::detect(const QString& devicePath, const QString& /*portChain*/)
{
    if (devicePath.isEmpty()) {
        qCDebug(log_host_hid) << "ChipDetector: empty device path";
        return VideoChipType::UNKNOWN;
    }

    bool isMS2130S = false;
    bool isMS2109S = false;
    bool isMS2109  = false;

#ifdef _WIN32
    auto hasVidPid = [&](const QString& vid, const QString& pid) -> bool {
        const QString vidTag = QStringLiteral("vid_") + vid;
        const QString pidTag = QStringLiteral("pid_") + pid;
        return (devicePath.contains(vidTag, Qt::CaseInsensitive) ||
                devicePath.contains(vid,    Qt::CaseInsensitive))
            && (devicePath.contains(pidTag, Qt::CaseInsensitive) ||
                devicePath.contains(pid,    Qt::CaseInsensitive));
    };

    if      (hasVidPid(QLatin1String("345F"), QLatin1String("2132"))) { isMS2130S = true; qCDebug(log_host_hid) << "ChipDetector: MS2130S (345F:2132)"; }
    else if (hasVidPid(QLatin1String("345F"), QLatin1String("2109"))) { isMS2109S = true; qCDebug(log_host_hid) << "ChipDetector: MS2109S (345F:2109)"; }
    else if (hasVidPid(QLatin1String("534D"), QLatin1String("2109"))) { isMS2109  = true; qCDebug(log_host_hid) << "ChipDetector: MS2109  (534D:2109)"; }

#elif defined(__linux__)
    {
        QString hidrawName = devicePath;
        if (hidrawName.startsWith(QLatin1String("/dev/")))
            hidrawName = QFileInfo(hidrawName).fileName();

        QString sysBase = QStringLiteral("/sys/class/hidraw/") + hidrawName + QStringLiteral("/device");
        QString cur = QFileInfo(sysBase).canonicalFilePath();
        if (cur.isEmpty()) cur = sysBase;

        for (int up = 0; up < 6 && !cur.isEmpty(); ++up) {
            const QString vidFile = cur + QStringLiteral("/idVendor");
            const QString pidFile = cur + QStringLiteral("/idProduct");

            if (QFile::exists(vidFile) && QFile::exists(pidFile)) {
                auto readHex = [](const QString& path) -> QString {
                    QFile f(path);
                    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
                    return QString::fromUtf8(f.readAll())
                        .trimmed()
                        .remove(QLatin1String("0x"), Qt::CaseInsensitive)
                        .toUpper();
                };
                const QString vid = readHex(vidFile);
                const QString pid = readHex(pidFile);
                qCDebug(log_host_hid) << "ChipDetector sysfs:" << cur << "vid=" << vid << "pid=" << pid;

                if      (vid == QLatin1String("345F") && pid == QLatin1String("2132")) { isMS2130S = true; break; }
                else if (vid == QLatin1String("345F") && pid == QLatin1String("2109")) { isMS2109S = true; break; }
                else if (vid == QLatin1String("534D") && pid == QLatin1String("2109")) { isMS2109  = true; break; }
            }
            const QString parent = QFileInfo(cur).dir().path();
            if (parent == cur || parent.isEmpty()) break;
            cur = parent;
        }

        if (!isMS2130S && !isMS2109S && !isMS2109) {
            if      (devicePath.contains(QLatin1String("345F"), Qt::CaseInsensitive) && devicePath.contains(QLatin1String("2132"), Qt::CaseInsensitive)) { isMS2130S = true; qCDebug(log_host_hid) << "ChipDetector: MS2130S path fallback"; }
            else if (devicePath.contains(QLatin1String("345F"), Qt::CaseInsensitive) && devicePath.contains(QLatin1String("2109"), Qt::CaseInsensitive)) { isMS2109S = true; qCDebug(log_host_hid) << "ChipDetector: MS2109S path fallback"; }
            else if (devicePath.contains(QLatin1String("534D"), Qt::CaseInsensitive) && devicePath.contains(QLatin1String("2109"), Qt::CaseInsensitive)) { isMS2109  = true; qCDebug(log_host_hid) << "ChipDetector: MS2109  path fallback"; }
        }
    }
#endif

    if (isMS2130S) return VideoChipType::MS2130S;
    if (isMS2109S) return VideoChipType::MS2109S;
    if (isMS2109)  return VideoChipType::MS2109;

    qCDebug(log_host_hid) << "ChipDetector: unknown chip for path:" << devicePath;
    return VideoChipType::UNKNOWN;
}

// 
//  createChip()
// 
std::unique_ptr<VideoChip> ChipDetector::createChip(VideoChipType type,
                                                      IHIDTransport* transport)
{
    switch (type) {
    case VideoChipType::MS2130S:
        return std::make_unique<Ms2130sChip>(transport);
    case VideoChipType::MS2109S:
        return std::make_unique<Ms2109sChip>(transport);
    case VideoChipType::MS2109:
        return std::make_unique<Ms2109Chip>(transport);
    default:
        return nullptr;
    }
}
