#include "cornerwidgetmanager.h"
#include <QMenuBar>
#include <QDebug>
#include <QApplication>
#include <QSvgRenderer>
#include <QPainter>
#include <QFile>

CornerWidgetManager::CornerWidgetManager(QWidget *parent)
    : QObject(parent),
      cornerWidget(new QWidget(parent)),
      keyboardLayoutComboBox(nullptr),
      screenScaleButton(nullptr),
      zoomInButton(nullptr),
      zoomOutButton(nullptr),
      zoomReductionButton(nullptr),
      virtualKeyboardButton(nullptr),
      captureButton(nullptr),
      fullScreenButton(nullptr),
      pasteButton(nullptr),
      screensaverButton(nullptr),
      recordingButton(nullptr),
      muteButton(nullptr),
      toggleSwitch(new ToggleSwitch(cornerWidget)),
      horizontalLayout(new QHBoxLayout()),
      menuBar(nullptr),
      layoutThreshold(800),
      isRecording(false),
      isMuted(false),
      m_updatingFromStatus(false)  // Initialize flag
{
    createWidgets();
    setupConnections();
    horizontalLayout->setSpacing(2);
    horizontalLayout->setContentsMargins(5, 5, 5, 5);
    cornerWidget->setLayout(horizontalLayout);
    cornerWidget->adjustSize();
    cornerWidget->show();
}

CornerWidgetManager::~CornerWidgetManager()
{
    delete cornerWidget;
}

QWidget* CornerWidgetManager::getCornerWidget() const
{
    return cornerWidget;
}

void CornerWidgetManager::setMenuBar(QMenuBar *menuBar)
{
    this->menuBar = menuBar;
    if (menuBar) {
        // CRITICAL FIX: Ensure corner widget is properly sized before adding to menu bar
        // This prevents it from blocking menu items like File and Edit
        cornerWidget->adjustSize();
        cornerWidget->setMaximumWidth(cornerWidget->sizeHint().width());
        
        menuBar->setCornerWidget(cornerWidget, Qt::TopRightCorner);
        
        qDebug() << "[CornerWidgetManager] Set corner widget on menu bar";
        qDebug() << "[CornerWidgetManager] Corner widget size:" << cornerWidget->size();
        qDebug() << "[CornerWidgetManager] Corner widget sizeHint:" << cornerWidget->sizeHint();
        qDebug() << "[CornerWidgetManager] Menu bar width:" << menuBar->width();
    }
}

void CornerWidgetManager::createWidgets()
{
    keyboardLayoutComboBox = new QComboBox(cornerWidget);
    keyboardLayoutComboBox->setObjectName("keyboardLayoutComboBox");
    keyboardLayoutComboBox->setFixedHeight(30);
    keyboardLayoutComboBox->setMinimumWidth(100);
    keyboardLayoutComboBox->setToolTip(tr("Select Keyboard Layout"));

    const struct {
        QPushButton** button;
        const char* objectName;
        const char* iconPath;
        const char* tooltipText;
        const char* shortcut;
    } buttons[] = {
        {&screenScaleButton, "ScreenScaleButton", ":/images/screen_scale.svg", "Screen scale", "Ctrl+Shift+A"},
        {&zoomInButton, "ZoomInButton", ":/images/zoom_in.svg", "Zoom in", "Ctrl++"},
        {&zoomOutButton, "ZoomOutButton", ":/images/zoom_out.svg", "Zoom out", "Ctrl+-"},
        {&zoomReductionButton, "ZoomReductionButton", ":/images/zoom_fit.svg", "Restore original size", "Ctrl+0"},
        {&virtualKeyboardButton, "virtualKeyboardButton", ":/images/keyboard.svg", "Function key and composite key", "Ctrl+Shift+K"},
        {&captureButton, "captureButton", ":/images/capture.svg", "Full screen capture", "Ctrl+Shift+S"},
        {&fullScreenButton, "fullScreenButton", ":/images/full_screen.svg", "Full screen mode", "Alt+F11"},
        {&pasteButton, "pasteButton", ":/images/paste.svg", "Paste text to target", "Ctrl+Shift+V"},
        {&screensaverButton, "screensaverButton", ":/images/screensaver.svg", "Mouse dance", "Ctrl+Shift+F10"},
        {&recordingButton, "recordingButton", ":/images/startRecord.svg", "Start/Stop Recording", "Ctrl+Shift+F11"},
        {&muteButton, "muteButton", ":/images/audio.svg", "Mute/Unmute Audio", "Ctrl+Shift+F9"}
    };

    for (const auto& btn : buttons) {
        *btn.button = new QPushButton(cornerWidget);
        (*btn.button)->setObjectName(btn.objectName);
        setButtonIcon(*btn.button, btn.iconPath);
        (*btn.button)->setToolTip(tr(btn.tooltipText) + " (" + btn.shortcut + ")");
    }
    
    screensaverButton->setCheckable(true);

    toggleSwitch->setFixedSize(78, 28);

    horizontalLayout->addWidget(keyboardLayoutComboBox);
    horizontalLayout->addWidget(screenScaleButton);
    horizontalLayout->addWidget(zoomInButton);
    horizontalLayout->addWidget(zoomOutButton);
    horizontalLayout->addWidget(zoomReductionButton);
    horizontalLayout->addWidget(virtualKeyboardButton);
    horizontalLayout->addWidget(captureButton);
    horizontalLayout->addWidget(fullScreenButton);
    horizontalLayout->addWidget(pasteButton);
    horizontalLayout->addWidget(screensaverButton);
    horizontalLayout->addWidget(recordingButton);
    horizontalLayout->addWidget(muteButton);
    horizontalLayout->addWidget(toggleSwitch);
}

void CornerWidgetManager::setButtonIcon(QPushButton *button, const QString &iconPath)
{
    // Use QSvgRenderer to load and render SVG files directly
    // This ensures SVGs work correctly on Linux even if the SVG image plugin is not available
    
    // Load the SVG from Qt resources
    QFile svgFile(iconPath);
    if (!svgFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open SVG resource:" << iconPath;
        return;
    }
    
    QByteArray svgData = svgFile.readAll();
    svgFile.close();
    
    QSvgRenderer svgRenderer(svgData);
    if (!svgRenderer.isValid()) {
        qWarning() << "Failed to parse SVG:" << iconPath;
        return;
    }
    
    QSize iconSize(16, 16);
    QPixmap pixmap(iconSize);
    pixmap.fill(Qt::transparent);
    
    QPainter painter(&pixmap);
    svgRenderer.render(&painter);
    painter.end();
    
    QIcon icon(pixmap);
    button->setIcon(icon);
    button->setIconSize(iconSize);
    button->setFixedSize(30, 30);
}

void CornerWidgetManager::setupConnections()
{
    connect(zoomInButton, &QPushButton::clicked, this, &CornerWidgetManager::zoomInClicked);
    connect(zoomOutButton, &QPushButton::clicked, this, &CornerWidgetManager::zoomOutClicked);
    connect(zoomReductionButton, &QPushButton::clicked, this, &CornerWidgetManager::zoomReductionClicked);
    connect(screenScaleButton, &QPushButton::clicked, this, &CornerWidgetManager::screenScaleClicked);
    connect(virtualKeyboardButton, &QPushButton::clicked, this, &CornerWidgetManager::virtualKeyboardClicked);
    connect(captureButton, &QPushButton::clicked, this, &CornerWidgetManager::captureClicked);
    connect(fullScreenButton, &QPushButton::clicked, this, &CornerWidgetManager::fullScreenClicked);
    connect(pasteButton, &QPushButton::clicked, this, &CornerWidgetManager::pasteClicked);
    connect(screensaverButton, &QPushButton::toggled, this, &CornerWidgetManager::screensaverClicked);
    connect(toggleSwitch, &ToggleSwitch::stateChanged, this, &CornerWidgetManager::toggleSwitchChanged);
    connect(keyboardLayoutComboBox, &QComboBox::currentTextChanged, this, &CornerWidgetManager::keyboardLayoutChanged);
    
    // Connect recording button click to toggle recording state and emit signal
    connect(recordingButton, &QPushButton::clicked, this, [this]() {
        emit recordingToggled();
    });
    
    // Connect mute button click to toggle mute state and emit signal
    connect(muteButton, &QPushButton::clicked, this, [this]() {
        emit muteToggled();
    });
}

void CornerWidgetManager::initializeKeyboardLayouts(const QStringList &layouts, const QString &defaultLayout)
{
    keyboardLayoutComboBox->clear();
    keyboardLayoutComboBox->addItems(layouts);
    if (layouts.contains(defaultLayout)) {
        keyboardLayoutComboBox->setCurrentText(defaultLayout);
    } else if (!layouts.isEmpty()) {
        keyboardLayoutComboBox->setCurrentText(layouts.first());
    }
}

void CornerWidgetManager::restoreMuteState(bool muted)
{
    isMuted = muted;
    if (muteButton) {
        setButtonIcon(muteButton, isMuted ? ":/images/mute.svg" : ":/images/audio.svg");
        muteButton->setToolTip(isMuted ? tr("Unmute Audio") + " (Ctrl+Shift+F9)" : tr("Mute Audio") + " (Ctrl+Shift+F9)");
    }
}

void CornerWidgetManager::updatePosition(int windowWidth, int menuBarHeight, bool isFullScreen)
{
    if (windowWidth < layoutThreshold || isFullScreen) {
        cornerWidget->setMaximumWidth(QWIDGETSIZE_MAX);
    } else {
        if (menuBar) cornerWidget->setMaximumWidth(horizontalLayout->sizeHint().width());
    }

    horizontalLayout->invalidate();
    horizontalLayout->activate();
    cornerWidget->setMinimumSize(horizontalLayout->sizeHint());
    cornerWidget->resize(horizontalLayout->sizeHint());
    cornerWidget->adjustSize();

    if (windowWidth < layoutThreshold || isFullScreen) {
        if (menuBar) {
            menuBar->setCornerWidget(nullptr, Qt::TopRightCorner);
        }
        cornerWidget->setParent(cornerWidget->parentWidget());

        QSize size = cornerWidget->size();
        int x = qMax(0, windowWidth - size.width());
        int y = isFullScreen ? 10 : (menuBarHeight > 0 ? menuBarHeight + 10 : 10);
        cornerWidget->setGeometry(x, y, size.width(), size.height());
        cornerWidget->raise();
        cornerWidget->show();
        qDebug() << "Floating position: (" << x << "," << y << "), size:" << size
                 << ", geometry:" << cornerWidget->geometry()
                 << ", layout sizeHint:" << horizontalLayout->sizeHint();
    } else {
        if (menuBar) {
            menuBar->setCornerWidget(cornerWidget, Qt::TopRightCorner);
            cornerWidget->show();
        }
        qDebug() << "Menu bar corner widget, size:" << cornerWidget->size()
                 << ", geometry:" << cornerWidget->geometry()
                 << ", layout sizeHint:" << horizontalLayout->sizeHint();
    }
}

void CornerWidgetManager::updateUSBStatus(bool isToTarget)
{
    if (toggleSwitch->isChecked() != isToTarget) {
        m_updatingFromStatus = true;  // Set flag before update
        toggleSwitch->setChecked(isToTarget);
        m_updatingFromStatus = false;  // Clear flag after update
    }
}

void CornerWidgetManager::updateRecordingState(bool recording)
{
    isRecording = recording;
    if (recordingButton) {
        setButtonIcon(recordingButton, isRecording ? ":/images/stopRecord.svg" : ":/images/startRecord.svg");
        recordingButton->setToolTip(isRecording ? tr("Stop Recording") + " (Ctrl+Shift+F11)" : tr("Start Recording") + " (Ctrl+Shift+F11)");
    }
}

void CornerWidgetManager::updateMuteState(bool muted)
{
    isMuted = muted;
    if (muteButton) {
        setButtonIcon(muteButton, isMuted ? ":/images/mute.svg" : ":/images/audio.svg");
        muteButton->setToolTip(isMuted ? tr("Unmute Audio") + " (Ctrl+Shift+F9)" : tr("Mute Audio") + " (Ctrl+Shift+F9)");
    }
}

void CornerWidgetManager::retranslateUi()
{
    keyboardLayoutComboBox->setToolTip(tr("Select Keyboard Layout"));

    const struct {
        QPushButton** button;
        const char* tooltipText;
        const char* shortcut;
    } buttons[] = {
        {&screenScaleButton,     "Screen scale",                   "Ctrl+Shift+A"},
        {&zoomInButton,          "Zoom in",                        "Ctrl++"},
        {&zoomOutButton,         "Zoom out",                       "Ctrl+-"},
        {&zoomReductionButton,   "Restore original size",          "Ctrl+0"},
        {&virtualKeyboardButton, "Function key and composite key", "Ctrl+Shift+K"},
        {&captureButton,         "Full screen capture",            "Ctrl+Shift+S"},
        {&fullScreenButton,      "Full screen mode",               "Alt+F11"},
        {&pasteButton,           "Paste text to target",           "Ctrl+Shift+V"},
        {&screensaverButton,     "Mouse dance",                    "Ctrl+Shift+F10"},
    };
    for (const auto& btn : buttons) {
        (*btn.button)->setToolTip(tr(btn.tooltipText) + " (" + btn.shortcut + ")");
    }

    if (recordingButton) {
        recordingButton->setToolTip(isRecording
            ? tr("Stop Recording")  + " (Ctrl+Shift+F11)"
            : tr("Start Recording") + " (Ctrl+Shift+F11)");
    }
    if (muteButton) {
        muteButton->setToolTip(isMuted
            ? tr("Unmute Audio") + " (Ctrl+Shift+F9)"
            : tr("Mute Audio")   + " (Ctrl+Shift+F9)");
    }
}