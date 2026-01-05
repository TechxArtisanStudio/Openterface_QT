#ifdef _WIN32
#include "WindowsDeviceManager.h"
#include "windows/WinDeviceEnumerator.h"
#include "windows/discoverers/BotherDeviceDiscoverer.h"
#include "windows/discoverers/Generation3Discoverer.h"
#include "windows/discoverers/DeviceDiscoveryManager.h"
#include <QDebug>

Q_LOGGING_CATEGORY(log_device_windows, "opf.host.windows")

WindowsDeviceManager::WindowsDeviceManager(QObject *parent)
    : WindowsDeviceManager(std::make_unique<WinDeviceEnumerator>(), parent)
{
}

WindowsDeviceManager::WindowsDeviceManager(std::unique_ptr<IDeviceEnumerator> enumerator, QObject *parent)
    : AbstractPlatformDeviceManager(parent), m_enumerator(std::move(enumerator))
{
    initializeDiscoveryManager();
}


WindowsDeviceManager::~WindowsDeviceManager()
{
    cleanup();
}


QVector<DeviceInfo> WindowsDeviceManager::discoverDevices()
{
    if (!m_cachedDevices.isEmpty() && m_lastCacheUpdate.isValid() &&
        m_lastCacheUpdate.msecsTo(QDateTime::currentDateTime()) < CACHE_TIMEOUT_MS) {
        return m_cachedDevices;
    }

    if (!m_discoveryManager) {
        initializeDiscoveryManager();
    }

    if (m_discoveryManager) {
        m_cachedDevices = m_discoveryManager->discoverAllDevices();
        m_lastCacheUpdate = QDateTime::currentDateTime();
    } else {
        m_cachedDevices.clear();
    }

    return m_cachedDevices;
}


void WindowsDeviceManager::clearCache()
{
    m_cachedDevices.clear();
    m_lastCacheUpdate = QDateTime();
}


void WindowsDeviceManager::debugListAllUSBDevices()
{
    if (!m_enumerator) {
        qCWarning(log_device_windows) << "Enumerator not available";
        return;
    }

    auto all = m_enumerator->enumerateAllDevices();
    qCDebug(log_device_windows) << "Total enumerated devices:" << all.size();
}


void WindowsDeviceManager::cleanup()
{
    m_discoveryManager.reset();
    m_enumerator.reset();
}


void WindowsDeviceManager::initializeDiscoveryManager()
{
    if (!m_enumerator) {
        qCWarning(log_device_windows) << "Cannot initialize discovery manager: enumerator missing";
        return;
    }

    auto enumeratorShared = std::shared_ptr<IDeviceEnumerator>(m_enumerator.get(), [](IDeviceEnumerator*){});

    m_discoveryManager = std::make_unique<DeviceDiscoveryManager>(enumeratorShared, this);
    m_discoveryManager->registerDiscoverer(std::make_shared<BotherDeviceDiscoverer>(enumeratorShared));
    m_discoveryManager->registerDiscoverer(std::make_shared<Generation3Discoverer>(enumeratorShared));
}

#endif // _WIN32


