#include "DeviceFactory.h"
#include <QLoggingCategory>

#ifdef _WIN32
#include "WindowsDeviceManager.h"
#endif

#ifdef __linux__
#include "LinuxDeviceManager.h"
#endif

Q_LOGGING_CATEGORY(log_device_factory, "opf.device.factory")

AbstractPlatformDeviceManager* DeviceFactory::createDeviceManager(QObject* parent)
{
    QString platform = getCurrentPlatform();
    qCDebug(log_device_factory) << "Creating device manager for platform:" << platform;
    
#ifdef _WIN32
    if (platform == "Windows") {
        return new WindowsDeviceManager(parent);
    }
#endif

#ifdef __linux__
    if (platform == "Linux") {
        return new LinuxDeviceManager(parent);
    }
#endif

    qCWarning(log_device_factory) << "Unsupported platform:" << platform;
    return nullptr;
}

QString DeviceFactory::getCurrentPlatform()
{
#ifdef _WIN32
    return "Windows";
#elif __linux__
    return "Linux";
#elif __APPLE__
    return "macOS";
#else
    return "Unknown";
#endif
}

bool DeviceFactory::isPlatformSupported(const QString& platformName)
{
    QString platform = platformName.isEmpty() ? getCurrentPlatform() : platformName;
    return getSupportedPlatforms().contains(platform, Qt::CaseInsensitive);
}

QStringList DeviceFactory::getSupportedPlatforms()
{
    QStringList platforms;
    
#ifdef _WIN32
    platforms << "Windows";
#endif

#ifdef __linux__
    platforms << "Linux";
#endif

#ifdef __APPLE__
    platforms << "macOS";
#endif

    return platforms;
}
