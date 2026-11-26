#ifdef _WIN32
#include "WindowsDeviceManager.h"
#include "windows/WinDeviceEnumerator.h"
#include "windows/discoverers/BotherDeviceDiscoverer.h"
#include "windows/discoverers/Generation3Discoverer.h"
#include "windows/discoverers/DeviceDiscoveryManager.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QMutexLocker>
#include "log/opflogging.h"

OPF_LOGGING_CATEGORY(log_device_windows, "opf.host.windows")

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
    {
        QMutexLocker locker(&m_cacheMutex);
        if (m_lastCacheUpdate.isValid() &&
            m_lastCacheUpdate.msecsTo(QDateTime::currentDateTime()) < CACHE_TIMEOUT_MS) {
            return m_cachedDevices;
        }
    }

    // The cache is authoritative only for the duration of this scan.
    QMutexLocker scanLock(&m_cacheMutex);

    if (!m_discoveryManager) {
        initializeDiscoveryManager();
    }

    if (!m_discoveryManager) {
        return QVector<DeviceInfo>();
    }

    // Re-check: another thread may have refreshed the cache while we waited.
    if (m_lastCacheUpdate.isValid() &&
        m_lastCacheUpdate.msecsTo(QDateTime::currentDateTime()) < CACHE_TIMEOUT_MS) {
        return m_cachedDevices;
    }

    QElapsedTimer timer;
    timer.start();
    m_cachedDevices = m_discoveryManager->discoverAllDevices();
    m_lastCacheUpdate = QDateTime::currentDateTime();

    const qint64 elapsedMs = timer.elapsed();
    if (elapsedMs > 250) {
        qCInfo(log_device_windows) << "Windows device scan took" << elapsedMs << "ms for"
                                   << m_cachedDevices.size() << "devices";
    } else {
        qCDebug(log_device_windows) << "Windows device scan took" << elapsedMs << "ms for"
                                    << m_cachedDevices.size() << "devices";
    }

    return m_cachedDevices;
}


void WindowsDeviceManager::clearCache()
{
    QMutexLocker locker(&m_cacheMutex);
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


