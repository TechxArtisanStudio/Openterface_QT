#include "platformhidadapter.h"
#include "videohid.h"
#include <QDebug>

PlatformHidAdapter* PlatformHidAdapter::create(VideoHid* owner) {
#ifdef _WIN32
    return new WindowsHidAdapter(owner);
#elif defined(__linux__)
    return new LinuxHidAdapter(owner);
#else
    (void)owner;
    return nullptr;
#endif
}

#ifdef _WIN32
bool WindowsHidAdapter::open() {
    if (!m_owner) return false;
    return m_owner->platform_openDevice();
}

void WindowsHidAdapter::close() {
    if (!m_owner) return;
    m_owner->platform_closeDevice();
}

bool WindowsHidAdapter::sendFeatureReport(uint8_t* buffer, size_t bufferLength) {
    if (!m_owner) return false;
    return m_owner->platform_sendFeatureReport(buffer, bufferLength);
}

bool WindowsHidAdapter::getFeatureReport(uint8_t* buffer, size_t bufferLength) {
    if (!m_owner) return false;
    return m_owner->platform_getFeatureReport(buffer, bufferLength);
}

QString WindowsHidAdapter::getHIDDevicePath() {
    if (!m_owner) return QString();
    return m_owner->platform_getHIDDevicePath();
}

#elif defined(__linux__)
bool LinuxHidAdapter::open() {
    if (!m_owner) return false;
    return m_owner->platform_openDevice();
}

void LinuxHidAdapter::close() {
    if (!m_owner) return;
    m_owner->platform_closeDevice();
}

bool LinuxHidAdapter::sendFeatureReport(uint8_t* buffer, size_t bufferLength) {
    if (!m_owner) return false;
    return m_owner->platform_sendFeatureReport(buffer, bufferLength);
}

bool LinuxHidAdapter::getFeatureReport(uint8_t* buffer, size_t bufferLength) {
    if (!m_owner) return false;
    return m_owner->platform_getFeatureReport(buffer, bufferLength);
}

QString LinuxHidAdapter::getHIDDevicePath() {
    if (!m_owner) return QString();
    return m_owner->platform_getHIDDevicePath();
}
#endif
