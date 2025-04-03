// ScreenSaverManager.cpp
#include "ui/screensavermanager.h"
#include <QDebug>

ScreenSaverManager::ScreenSaverManager(QObject *parent)
    : QObject(parent) {
    loadSettings();
}

ScreenSaverManager::~ScreenSaverManager() {

    if (m_isInhibited) {
        uninhibitScreenSaver();
    }
}

void ScreenSaverManager::loadSettings() {
    bool savedState = GlobalSetting::instance().getScreenSaverInhibited();
    m_isInhibited = savedState; // load the state from QSettings
    if (m_isInhibited) {
        inhibitScreenSaver();
    } else {
        uninhibitScreenSaver();
    }
}

void ScreenSaverManager::setScreenSaverInhibited(bool inhibit) {
    if (m_isInhibited == inhibit) {
        return;
    }

    if (inhibit) {
        inhibitScreenSaver();
    } else {
        uninhibitScreenSaver();
    }

    m_isInhibited = inhibit;
    GlobalSetting::instance().setScreenSaverInhibited(m_isInhibited); // save the state to QSettings
}

void ScreenSaverManager::inhibitScreenSaver() {
#ifdef Q_OS_WIN
    // Windows: inhibit screen saver using SetThreadExecutionState
    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    qDebug() << "Screen saver inhibited on Windows";
#endif
#ifdef Q_OS_LINUX
    // Linux: use DBus to inhibit screen saver
    QDBusInterface screenSaver("org.freedesktop.ScreenSaver",
                               "/org/freedesktop/ScreenSaver",
                               "org.freedesktop.ScreenSaver");
    if (screenSaver.isValid()) {
        QDBusReply<uint> reply = screenSaver.call("Inhibit", "OpenterfaceQt", "Running KVM application");
        if (reply.isValid()) {
            m_cookie = reply.value();
            qDebug() << "Screen saver inhibited on Linux with cookie:" << m_cookie;
        } else {
            qWarning() << "Failed to inhibit screen saver on Linux:" << reply.error().message();
        }
    }
#endif
}

void ScreenSaverManager::uninhibitScreenSaver() {
#ifdef Q_OS_WIN
    // Windows: recover screen saver using SetThreadExecutionState
    SetThreadExecutionState(ES_CONTINUOUS);
    qDebug() << "Screen saver uninhibited on Windows";
#endif
#ifdef Q_OS_LINUX
    // Linux: DBus recovery screen saver
    QDBusInterface screenSaver("org.freedesktop.ScreenSaver",
                               "/org/freedesktop/ScreenSaver",
                               "org.freedesktop.ScreenSaver");
    if (screenSaver.isValid() && m_cookie != 0) {
        screenSaver.call("UnInhibit", m_cookie);
        qDebug() << "Screen saver uninhibited on Linux with cookie:" << m_cookie;
        m_cookie = 0;
    }
#endif
}