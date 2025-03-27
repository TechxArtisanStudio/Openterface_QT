#ifndef STATUSBARMANAGER_H
#define STATUSBARMANAGER_H

#include <QObject>
#include <QStatusBar>
#include <QLabel>
#include <QHBoxLayout>
#include <QWidget>
#include "statuswidget.h"

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
    void setTargetUsbConnected(bool isConnected);
    void updateIconColor();

private:
    QStatusBar *m_statusBar;
    StatusWidget *m_statusWidget;
    QLabel *mouseLabel;
    QLabel *mouseLocationLabel;
    QLabel *keyPressedLabel;
    QLabel *keyLabel;
    QColor iconColor;

    QPixmap recolorSvg(const QString &svgPath, const QColor &color, const QSize &size);
    QColor getContrastingColor(const QColor &color);
    QString m_currentPort;
    void updateKeyboardIcon(const QString& key);
};

#endif // STATUSBARMANAGER_H