#include "edididentitycache.h"
#include "edidsettingsmanager.h"
#include "../../../video/videohid.h"
#include "../../globalsetting.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(log_edid_cache, "opf.edid.cache")

namespace edid {

EdidIdentityCache& EdidIdentityCache::instance()
{
    static EdidIdentityCache cache;
    return cache;
}

EdidIdentityCache::EdidIdentityCache(QObject *parent)
    : QObject(parent)
    , m_manager(new EdidSettingsManager(this))
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &EdidIdentityCache::startRead);
    connect(m_manager, &EdidSettingsManager::identityRead, this, &EdidIdentityCache::onIdentityRead);
    connect(&VideoHid::getInstance(), &VideoHid::hidDeviceConnected,
            this, &EdidIdentityCache::onHidDeviceConnected);
}

QString EdidIdentityCache::displayName(const QString &portChain) const
{
    return m_byPortChain.value(portChain).displayName;
}

EdidIdentity EdidIdentityCache::identity(const QString &portChain) const
{
    return m_byPortChain.value(portChain);
}

void EdidIdentityCache::onHidDeviceConnected(const QString &devicePath)
{
    qCInfo(log_edid_cache) << "HID device connected:" << devicePath << "- scheduling EDID identity read";
    // Coalesce: a reconnect during the delay restarts the clock.
    m_attempt = 0;
    m_timer.start(kInitialDelayMs);
}

void EdidIdentityCache::refresh(const QString &portChain)
{
    if (portChain.isEmpty()) {
        return;
    }
    const bool changed = (portChain != m_currentPortChain);
    m_currentPortChain = portChain;
    qCInfo(log_edid_cache) << "Selected unit at port chain" << portChain
                           << (hasIdentity(portChain) ? "(name known, re-reading)" : "(name unknown yet)");
    if (changed) {
        emit currentChanged(portChain);
    }
    m_attempt = 0;
    // Short delay: a selection has just rebound HID/serial/camera; let that settle.
    m_timer.start(1000);
}

void EdidIdentityCache::refreshCurrent()
{
    QString portChain = m_currentPortChain;
    if (portChain.isEmpty()) {
        portChain = GlobalSetting::instance().getOpenterfacePortChain();
    }
    refresh(portChain);
}

void EdidIdentityCache::startRead()
{
    QString portChain = m_currentPortChain;
    if (portChain.isEmpty()) {
        portChain = GlobalSetting::instance().getOpenterfacePortChain();
        m_currentPortChain = portChain;
    }
    if (portChain.isEmpty()) {
        if (m_attempt < kMaxAttempts) {
            ++m_attempt;
            qCInfo(log_edid_cache) << "No current HID port chain yet; retrying in" << kRetryDelayMs / 1000 << "s";
            m_timer.start(kRetryDelayMs);
        } else {
            qCInfo(log_edid_cache) << "No current HID port chain; not reading EDID identity";
        }
        return;
    }
    if (m_readInFlight || EdidSettingsManager::anyBusy()) {
        // Our own earlier read (or another EDID operation: MCP tool, preferences
        // page) is still running. Do NOT touch the pending bookkeeping -- the
        // running read's result must stay attributed to the key it started
        // with -- just come back later.
        qCInfo(log_edid_cache) << "EDID read busy; will retry for port chain" << portChain << "in 3 s";
        m_timer.start(3000);
        return;
    }
    m_pendingPortChain = portChain;
    m_pendingDevicePath = VideoHid::getInstance().getCurrentHIDDevicePath();
    m_readInFlight = true;
    ++m_attempt;
    qCInfo(log_edid_cache) << "Reading EDID identity for port chain" << portChain << "attempt" << m_attempt;
    m_manager->readIdentity();
}

void EdidIdentityCache::onIdentityRead(bool ok, const EdidIdentity &identity, const QString &error)
{
    m_readInFlight = false;
    if (!ok) {
        if (m_attempt < kMaxAttempts) {
            qCInfo(log_edid_cache) << "EDID identity read failed (" << error << "); retrying in"
                                   << kRetryDelayMs / 1000 << "s";
            m_timer.start(kRetryDelayMs);
        } else {
            qCInfo(log_edid_cache) << "EDID identity read failed (" << error << "); giving up until the next device connect";
        }
        return;
    }
    const QString portChain = m_pendingPortChain;
    const QString devicePathNow = VideoHid::getInstance().getCurrentHIDDevicePath();
    if (portChain != m_currentPortChain || devicePathNow != m_pendingDevicePath) {
        // Selection changed while this read was in flight: the image we got
        // belongs to whichever unit VideoHid was bound to then -- not safe to
        // attribute. Re-read the current one instead.
        qCInfo(log_edid_cache) << "Discarding identity read for" << portChain << "- selection/device changed during the read"
                               << "(now" << m_currentPortChain << devicePathNow << ")";
        m_attempt = 0;
        m_timer.start(1000);
        return;
    }
    const bool changed = !m_byPortChain.contains(portChain)
                      || m_byPortChain.value(portChain).displayName != identity.displayName
                      || m_byPortChain.value(portChain).serialString != identity.serialString;
    m_byPortChain.insert(portChain, identity);
    qCInfo(log_edid_cache) << "EDID identity for port chain" << portChain << ":" << identity.summary();
    if (changed) {
        emit identityChanged(portChain, identity);
    }
}

} // namespace edid
