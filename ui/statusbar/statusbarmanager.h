#ifndef STATUSBARMANAGER_H
#define STATUSBARMANAGER_H

#include <QObject>
#include <QStatusBar>
#include <QLabel>
#include <QHBoxLayout>
#include <QWidget>
#include <QTimer>
#include "statuswidget.h"
#include "../globalsetting.h"

class StatusBarManager : public QObject
{
    Q_OBJECT

public:
    explicit StatusBarManager(QStatusBar *statusBar, QObject *parent = nullptr);

    void initStatusBar();
    void onLastKeyPressed(const QString& key);
    void onLastMouseLocation(const QPoint& location, const QString& mouseEvent);
    void setConnectedPort(const QString& port, const int& baudrate);
    void setStatusUpdate(const QString& status);
    void setInputResolution(int width, int height, float fps, float pixelClk);
    void setCaptureResolution(int width, int height, int fps);
    void setFps(double fps);
    void setTargetUsbConnected(bool isConnected);
    void factoryReset(bool isStarted);
    void serialPortReset(bool isStarted);
    void showNewDevicePluggedIn(const QString& portChain);
    void showDeviceUnplugged(const QString& portChain);
    void showCameraSwitching(const QString& fromDevice, const QString& toDevice);
    void showCameraSwitchComplete(const QString& device);
    void setKeyStates(bool numLock, bool capsLock, bool scrollLock);
    void updateIconColor();
    void showSerialAutoRestart(int attemptNumber, int maxAttempts, double lossRate);


private:
    QStatusBar *m_statusBar;
    StatusWidget *m_statusWidget;
    QLabel *mouseLabel;
    QLabel *mouseLocationLabel;
    QLabel *keyPressedLabel;
    QLabel *keyLabel;
    QColor iconColor;
    QLabel *statusMessageLabel;
    
    // Message throttling to prevent flooding during device switches
    QTimer *m_messageTimer;
    QString m_lastMessage;
    QString m_pendingMessage;
    bool m_messageThrottleActive;

    QPixmap recolorSvg(const QString &svgPath, const QColor &color, const QSize &size);
    QColor getContrastingColor(const QColor &color);
    QString m_currentPort;
    void updateKeyboardIcon(const QString& key);
    void showThrottledMessage(const QString& message, const QString& style, int duration = 3000);
};

#endif // STATUSBARMANAGER_H