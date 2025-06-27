#ifndef CORNERWIDGETMANAGER_H
#define CORNERWIDGETMANAGER_H

#include <QWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QMenuBar>
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
    QPushButton *screensaverButton;

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
    QMenuBar *menuBar;
    int layoutThreshold;
};

#endif // CORNERWIDGETMANAGER_H