#ifndef CORNERWIDGETMANAGER_H
#define CORNERWIDGETMANAGER_H

#include <QWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QMenuBar>
#include <QSvgRenderer>
#include <QPainter>
#include "ui/toolbar/toggleswitch.h"

class CornerWidgetManager : public QObject {
    Q_OBJECT

public:
    explicit CornerWidgetManager(QWidget *parent = nullptr);
    ~CornerWidgetManager();

    QWidget* getCornerWidget() const;
    void setMenuBar(QMenuBar *menuBar);
    void updatePosition(int windowWidth, int menuBarHeight, bool isFullScreen);
    void initializeKeyboardLayouts(const QStringList &layouts, const QString &defaultLayout);
    void restoreMuteState(bool muted);
    void updateUSBStatus(bool isToTarget);
    bool isUpdatingFromStatus() const { return m_updatingFromStatus; }  // New getter
    QPushButton *screensaverButton;
    QPushButton *recordingButton;
    QPushButton *muteButton;

signals:
    void zoomInClicked();
    void zoomOutClicked();
    void zoomReductionClicked();
    void screenScaleClicked();
    void virtualKeyboardClicked();
    void captureClicked();
    void fullScreenClicked();
    void pasteClicked();
    void screensaverClicked(bool checked);
    void toggleSwitchChanged(int state);
    void keyboardLayoutChanged(const QString &layout);
    void recordingToggled();
    void muteToggled();

private:
    void createWidgets();
    void setupConnections();
    void setButtonIcon(QPushButton *button, const QString &iconPath);

    QWidget *cornerWidget;
    QComboBox *keyboardLayoutComboBox;
    QPushButton *screenScaleButton;
    QPushButton *zoomInButton;
    QPushButton *zoomOutButton;
    QPushButton *zoomReductionButton;
    QPushButton *virtualKeyboardButton;
    QPushButton *captureButton;
    QPushButton *fullScreenButton;
    QPushButton *pasteButton;
    
    ToggleSwitch *toggleSwitch;
    QHBoxLayout *horizontalLayout;
    bool isRecording;
    bool isMuted;
    QMenuBar *menuBar;
    int layoutThreshold;
    bool m_updatingFromStatus;  // New flag to track programmatic updates
};

#endif // CORNERWIDGETMANAGER_H