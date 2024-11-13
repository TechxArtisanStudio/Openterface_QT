#ifndef CAMERAAJUST_H
#define CAMERAAJUST_H

#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QVBoxLayout>
#include <QPalette>
#include <QDebug>
#include "host/usbcontrol.h"

class CameraAdjust : public QWidget
{
    Q_OBJECT

public:
    explicit CameraAdjust(QWidget *parent = nullptr);
    ~CameraAdjust();

    void updatePosition();
    void initializeControls();

public slots:
    void toggleVisibility();
    void updatePosition(int menuBarHeight, int parentWidth);
    void updateColors();

private slots:
    void onContrastChanged(int value);

private:
    void setupUI();
    QSlider *contrastSlider;
    QLabel *contrastLabel;
    USBControl *usbControl;
};

#endif // CAMERAAJUST_H
