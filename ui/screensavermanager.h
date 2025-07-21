// ScreenSaverManager.h
#ifndef SCREENSAVERMANAGER_H
#define SCREENSAVERMANAGER_H

#include <QObject>
#include "ui/globalsetting.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#ifdef Q_OS_LINUX
#include <QDBusInterface>
#include <QDBusReply>
#endif

class ScreenSaverManager : public QObject {
    Q_OBJECT
public:
    explicit ScreenSaverManager(QObject *parent = nullptr);
    ~ScreenSaverManager();

    bool isScreenSaverInhibited() const { return m_isInhibited; }

    void loadSettings();

public slots:
    void setScreenSaverInhibited(bool inhibit);

private:
    void inhibitScreenSaver();
    void uninhibitScreenSaver();

    bool m_isInhibited = false;
    QSettings m_settings;
#ifdef Q_OS_LINUX
    uint m_cookie = 0;
#endif
};

#endif // SCREENSAVERMANAGER_H