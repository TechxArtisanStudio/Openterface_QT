#ifndef CORNERWIDGETMANAGER_H
#define CORNERWIDGETMANAGER_H

#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QMenuBar>
#include <QSvgRenderer>
#include <QPainter>
#include <QApplication>
#include <QEvent>
#include "ui/toolbar/toggleswitch.h"

class CornerWidgetManager : public QObject {
    Q_OBJECT

public:
    explicit CornerWidgetManager(QWidget *parent = nullptr);
    ~CornerWidgetManager();

    QWidget* getCornerWidget() const;
    void setMenuBar(QMenuBar *menuBar);
    void updatePosition(int windowWidth, int windowHeight, int menuBarHeight, bool isFullScreen, bool isPortraitMode);
    void initializeKeyboardLayouts(const QStringList &layouts, const QString &defaultLayout);
    void setCurrentKeyboardLayout(const QString &layout);
    void restoreMuteState(bool muted);
    void updateUSBStatus(bool isToTarget);
    void updateRecordingState(bool recording);
    void updateMuteState(bool muted);
    bool isUpdatingFromStatus() const { return m_updatingFromStatus; }  // New getter
    void retranslateUi();
    QPushButton *screensaverButton;
    QPushButton *recordingButton;
    QPushButton *muteButton;
    QPushButton *aiChatButton;

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
    void toggleSwitchChanged(Qt::CheckState state);
    void keyboardLayoutChanged(const QString &layout);
    void recordingToggled();
    void muteToggled();
    void aiChatToggled();

private:
    void createWidgets();
    void setupConnections();
    void setButtonIcon(QPushButton *button, const QString &iconPath);
    void updateAllIcons();
    void updateThemeColors();
    QColor getIconColor() const;
    void configureOrientationLayout(int windowWidth, bool isFullScreen, bool isPortraitMode);
    void positionVerticalWidget();
    void positionHorizontalWidget(bool floating, bool fullscreen);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

    QWidget *cornerWidget;
    QWidget *verticalCornerWidget;  // New vertical widget for portrait mode
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
    QVBoxLayout *verticalLayout;  // New vertical layout
    bool isRecording;
    bool isMuted;
    QMenuBar *menuBar;
    int layoutThreshold;
    bool m_updatingFromStatus;  // New flag to track programmatic updates
    bool m_isPortraitMode;  // Track portrait mode state
    QPoint m_dragStartPos;
    bool m_isDragging;
    bool m_orientationInitialized = false;
    bool m_verticalUserPositioned = false;
    bool m_orientationFullScreen = false;
    bool m_horizontalFloating = false;
    bool m_dragPending = false;
    QPoint m_dragWindowStartPos;
    QPoint m_lastMainWindowPos;
};

#endif // CORNERWIDGETMANAGER_H