#include "cornerwidgetmanager.h"
#include <QMenuBar>
#include <QDebug>
#include <QApplication>
#include <QSvgRenderer>
#include <QPainter>
#include <QFile>
#include <QPalette>
#include <QSignalBlocker>
#include <QMouseEvent>
#include <QAbstractItemView>
#include <QTimer>

CornerWidgetManager::CornerWidgetManager(QWidget *parent)
    : QObject(parent),
      screensaverButton(nullptr),
      recordingButton(nullptr),
      muteButton(nullptr),
      aiChatButton(nullptr),
      cornerWidget(new QWidget(parent)),
      verticalCornerWidget(new QWidget(parent)),
      keyboardLayoutComboBox(nullptr),
      screenScaleButton(nullptr),
      zoomInButton(nullptr),
      zoomOutButton(nullptr),
      zoomReductionButton(nullptr),
      virtualKeyboardButton(nullptr),
      captureButton(nullptr),
      fullScreenButton(nullptr),
      pasteButton(nullptr),
      toggleSwitch(new ToggleSwitch(cornerWidget)),
      horizontalLayout(new QHBoxLayout()),
      verticalLayout(new QVBoxLayout()),
      isRecording(false),
      isMuted(false),
      menuBar(nullptr),
      layoutThreshold(800),
      m_updatingFromStatus(false),
      m_isPortraitMode(false),
      m_isDragging(false),
      m_dragPending(false)
{
    createWidgets();
    setupConnections();
    updateThemeColors();
    horizontalLayout->setSpacing(2);
    horizontalLayout->setContentsMargins(5, 5, 5, 5);
    cornerWidget->setLayout(horizontalLayout);
    cornerWidget->adjustSize();
    cornerWidget->show();

    verticalLayout->setSpacing(2);
    verticalLayout->setContentsMargins(5, 5, 5, 5);
    verticalCornerWidget->setObjectName("verticalCornerWidget");
    verticalCornerWidget->setLayout(verticalLayout);
    verticalCornerWidget->setStyleSheet(
        "QWidget#verticalCornerWidget {"
        " background-color: palette(window);"
        " border: 1px solid palette(mid);"
        " border-radius: 5px;"
        "}");
    verticalCornerWidget->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
    verticalCornerWidget->hide();

    const QWidgetList dragTargets = {
        verticalCornerWidget, keyboardLayoutComboBox, screenScaleButton,
        zoomInButton, zoomOutButton, zoomReductionButton, virtualKeyboardButton,
        captureButton, fullScreenButton, pasteButton, screensaverButton,
        recordingButton, muteButton, aiChatButton, toggleSwitch
    };
    for (QWidget *target : dragTargets) {
        target->setMouseTracking(true);
        target->installEventFilter(this);
    }
}

CornerWidgetManager::~CornerWidgetManager()
{
    delete cornerWidget;
    delete verticalCornerWidget;
}

QWidget* CornerWidgetManager::getCornerWidget() const
{
    return cornerWidget;
}

void CornerWidgetManager::setMenuBar(QMenuBar *menuBar)
{
    QWidget *previousWindow = this->menuBar ? this->menuBar->window() : nullptr;
    if (previousWindow) {
        previousWindow->removeEventFilter(this);
    }

    this->menuBar = menuBar;
    updateThemeColors();
    m_lastMainWindowPos = QPoint();
    if (menuBar) {
        menuBar->window()->installEventFilter(this);
        cornerWidget->adjustSize();
        cornerWidget->setMaximumWidth(cornerWidget->sizeHint().width());
        menuBar->setCornerWidget(cornerWidget, Qt::TopRightCorner);
    }
}

void CornerWidgetManager::createWidgets()
{
    keyboardLayoutComboBox = new QComboBox(cornerWidget);
    // Never take keyboard focus: keys typed after picking a layout must keep going to the VideoPane.
    keyboardLayoutComboBox->setFocusPolicy(Qt::NoFocus);
    keyboardLayoutComboBox->setObjectName("keyboardLayoutComboBox");
    keyboardLayoutComboBox->setFixedHeight(30);
    keyboardLayoutComboBox->setMinimumWidth(100);
    keyboardLayoutComboBox->setToolTip(tr("Select Keyboard Layout"));
    keyboardLayoutComboBox->setStyleSheet(
        "QComboBox {"
        "  background-color: palette(base);"
        "  color: palette(text);"
        "  border: 1px solid palette(mid);"
        "  padding: 2px;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background-color: palette(base);"
        "  color: palette(text);"
        "  selection-background-color: palette(highlight);"
        "  selection-color: palette(highlightedText);"
        "}");

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
        {&muteButton, "muteButton", ":/images/audio.svg", "Mute/Unmute Audio", "Ctrl+Shift+F9"},
        {&aiChatButton, "aiChatButton", ":/images/magic_wand.svg", "AI Chat", "Ctrl+Shift+C"}
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
    horizontalLayout->addWidget(aiChatButton);
    horizontalLayout->addWidget(toggleSwitch);
}

void CornerWidgetManager::updateThemeColors()
{
    QWidget *mainWindow = menuBar ? menuBar->window() : qobject_cast<QWidget *>(parent());
    if (!mainWindow) {
        return;
    }

    const QPalette mainPalette = mainWindow->palette();
    QPalette themePalette = mainPalette;
    const QPalette::ColorGroup groups[] = {
        QPalette::Active, QPalette::Inactive, QPalette::Disabled
    };
    const QPalette::ColorRole backgroundRoles[] = {
        QPalette::Window, QPalette::Base, QPalette::Button
    };
    const QPalette::ColorRole foregroundRoles[] = {
        QPalette::WindowText, QPalette::Text, QPalette::ButtonText
    };
    for (QPalette::ColorGroup group : groups) {
        for (QPalette::ColorRole role : backgroundRoles) {
            themePalette.setColor(group, role, mainPalette.color(group, QPalette::Window));
        }
        for (QPalette::ColorRole role : foregroundRoles) {
            themePalette.setColor(group, role, mainPalette.color(group, QPalette::WindowText));
        }
    }

    cornerWidget->setPalette(themePalette);
    verticalCornerWidget->setPalette(themePalette);
    keyboardLayoutComboBox->setPalette(themePalette);
    keyboardLayoutComboBox->view()->setPalette(themePalette);
}

void CornerWidgetManager::setButtonIcon(QPushButton *button, const QString &iconPath)
{
    QSize buttonSize = button->size();
    if (!buttonSize.isValid() || buttonSize.width() > 64 || buttonSize.height() > 64) {
        buttonSize = QSize(30, 30);
    }

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

    QSize iconSize = button->iconSize();
    if (!iconSize.isValid()) {
        iconSize = QSize(16, 16);
    }
    QPixmap pixmap(iconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    svgRenderer.render(&painter);

    // Apply theme-aware color overlay using CompositionMode_SourceIn.
    // This replaces all opaque pixels with the icon color while preserving transparency.
    QColor iconColor = getIconColor();
    QPixmap colorOverlay(iconSize);
    colorOverlay.fill(iconColor);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.drawPixmap(0, 0, colorOverlay);

    painter.end();

    QIcon icon(pixmap);
    button->setIcon(icon);
    button->setIconSize(iconSize);
    button->setFixedSize(buttonSize);
}

QColor CornerWidgetManager::getIconColor() const
{
    // Use the WindowText color which automatically contrasts with the Window background.
    // In light theme: dark text on light background → dark icons.
    // In dark theme: light text on dark background → light icons.
    return QApplication::palette().color(QPalette::WindowText);
}

void CornerWidgetManager::updateAllIcons()
{
    // Re-apply all icons with the current theme colors
    setButtonIcon(screenScaleButton, ":/images/screen_scale.svg");
    setButtonIcon(zoomInButton, ":/images/zoom_in.svg");
    setButtonIcon(zoomOutButton, ":/images/zoom_out.svg");
    setButtonIcon(zoomReductionButton, ":/images/zoom_fit.svg");
    setButtonIcon(virtualKeyboardButton, ":/images/keyboard.svg");
    setButtonIcon(captureButton, ":/images/capture.svg");
    setButtonIcon(fullScreenButton, ":/images/full_screen.svg");
    setButtonIcon(pasteButton, ":/images/paste.svg");
    setButtonIcon(screensaverButton, ":/images/screensaver.svg");
    setButtonIcon(recordingButton, isRecording ? ":/images/stopRecord.svg" : ":/images/startRecord.svg");
    setButtonIcon(muteButton, isMuted ? ":/images/mute.svg" : ":/images/audio.svg");
    setButtonIcon(aiChatButton, ":/images/magic_wand.svg");
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
    
    // AI Chat button - checkable to reflect open/close state
    aiChatButton->setCheckable(true);
    connect(aiChatButton, &QPushButton::toggled, this, [this](bool checked) {
        emit aiChatToggled();
    });
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    connect(toggleSwitch, &QCheckBox::checkStateChanged, this, &CornerWidgetManager::toggleSwitchChanged);
#else
    connect(toggleSwitch, &QCheckBox::stateChanged, this, [this](int state) {
        toggleSwitchChanged(static_cast<Qt::CheckState>(state));
    });
#endif
    connect(keyboardLayoutComboBox, &QComboBox::currentTextChanged, this, &CornerWidgetManager::keyboardLayoutChanged);
    
    // Connect recording button click to toggle recording state and emit signal
    connect(recordingButton, &QPushButton::clicked, this, [this]() {
        emit recordingToggled();
    });
    
    // Connect mute button click to toggle mute state and emit signal
    connect(muteButton, &QPushButton::clicked, this, [this]() {
        emit muteToggled();
    });

    // Re-color SVG icons when system theme/palette changes
    qApp->installEventFilter(this);
}

bool CornerWidgetManager::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == qApp && event->type() == QEvent::ApplicationPaletteChange) {
        updateAllIcons();
        updateThemeColors();
        return false;
    }

    QWidget *mainWindow = menuBar ? menuBar->window() : nullptr;
    if (obj == mainWindow && event->type() == QEvent::PaletteChange) {
        updateThemeColors();
    }
    if (obj == mainWindow &&
        (event->type() == QEvent::Move || event->type() == QEvent::Resize ||
         event->type() == QEvent::Show)) {
        if (m_isPortraitMode && !m_orientationFullScreen) {
            positionVerticalWidget();
        }
        return false;
    }

    bool isVerticalDescendant = false;
    if (m_isPortraitMode && verticalCornerWidget) {
        QObject *current = obj;
        while (current) {
            if (current == verticalCornerWidget) {
                isVerticalDescendant = true;
                break;
            }
            current = current->parent();
        }
    }

    if (isVerticalDescendant) {
        QMouseEvent *mouseEvent = dynamic_cast<QMouseEvent *>(event);
        if (!mouseEvent) {
            return QObject::eventFilter(obj, event);
        }
        if (event->type() == QEvent::MouseButtonPress &&
            mouseEvent->button() == Qt::LeftButton) {
            m_dragStartPos = mouseEvent->globalPosition().toPoint();
            m_dragWindowStartPos = verticalCornerWidget->pos();
            m_dragPending = true;
            m_isDragging = false;
        } else if (event->type() == QEvent::MouseMove &&
                   (mouseEvent->buttons() & Qt::LeftButton) &&
                   (m_dragPending || m_isDragging)) {
            const QPoint delta = mouseEvent->globalPosition().toPoint() - m_dragStartPos;
            if (!m_isDragging && delta.manhattanLength() < 6) {
                return false;
            }
            if (!m_isDragging) {
                m_isDragging = true;
                m_dragPending = false;
                m_verticalUserPositioned = true;
                verticalCornerWidget->grabMouse();
            }
            verticalCornerWidget->move(m_dragWindowStartPos + delta);
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease &&
                   mouseEvent->button() == Qt::LeftButton) {
            const bool wasDragging = m_isDragging;
            m_dragPending = false;
            m_isDragging = false;
            if (wasDragging) {
                verticalCornerWidget->releaseMouse();
                return true;
            }
        }
    }

    return QObject::eventFilter(obj, event);
}

void CornerWidgetManager::initializeKeyboardLayouts(const QStringList &layouts, const QString &defaultLayout)
{
    // Block signals while populating so the first item doesn't get persisted as
    // the layout; MainWindow::initializeKeyboardLayouts applies the layout itself.
    const QSignalBlocker blocker(keyboardLayoutComboBox);
    keyboardLayoutComboBox->clear();
    keyboardLayoutComboBox->addItems(layouts);
    if (layouts.contains(defaultLayout)) {
        keyboardLayoutComboBox->setCurrentText(defaultLayout);
    } else if (!layouts.isEmpty()) {
        keyboardLayoutComboBox->setCurrentText(layouts.first());
    }
}

void CornerWidgetManager::setCurrentKeyboardLayout(const QString &layout)
{
    // Sync selection only; the caller has already applied the layout.
    const QSignalBlocker blocker(keyboardLayoutComboBox);
    keyboardLayoutComboBox->setCurrentText(layout);
}

void CornerWidgetManager::restoreMuteState(bool muted)
{
    isMuted = muted;
    if (muteButton) {
        setButtonIcon(muteButton, isMuted ? ":/images/mute.svg" : ":/images/audio.svg");
        muteButton->setToolTip(isMuted ? tr("Unmute Audio") + " (Ctrl+Shift+F9)" : tr("Mute Audio") + " (Ctrl+Shift+F9)");
    }
}

void CornerWidgetManager::updatePosition(int windowWidth, int, int, bool isFullScreen, bool isPortraitMode)
{
    const bool portrait = isPortraitMode;
    const bool fullscreen = isFullScreen && !portrait;
    const bool floating = windowWidth < layoutThreshold || fullscreen;

    if (!m_orientationInitialized || m_isPortraitMode != portrait ||
        m_orientationFullScreen != fullscreen ||
        (!portrait && m_horizontalFloating != floating)) {
        m_isPortraitMode = portrait;
        m_orientationFullScreen = fullscreen;
        m_horizontalFloating = floating;
        m_orientationInitialized = true;
        configureOrientationLayout(windowWidth, fullscreen, portrait);
    }

    if (portrait) {
        positionVerticalWidget();
    } else {
        positionHorizontalWidget(floating, fullscreen);
    }
}

void CornerWidgetManager::configureOrientationLayout(int windowWidth, bool isFullScreen, bool isPortraitMode)
{
    fullScreenButton->setEnabled(!isPortraitMode);
    fullScreenButton->setToolTip(isPortraitMode
        ? tr("Fullscreen not available in portrait mode")
        : tr("Full screen mode") + " (Alt+F11)");

    if (isPortraitMode) {
        m_verticalUserPositioned = false;
        m_lastMainWindowPos = QPoint();
        // Portrait mode: show vertical widget on the right side
        // Hide horizontal widget
        if (menuBar) {
            menuBar->setCornerWidget(nullptr, Qt::TopRightCorner);
        }
        cornerWidget->hide();

        // Move controls to vertical layout
        verticalLayout->addWidget(keyboardLayoutComboBox);
        verticalLayout->addWidget(screenScaleButton);
        verticalLayout->addWidget(zoomInButton);
        verticalLayout->addWidget(zoomOutButton);
        verticalLayout->addWidget(zoomReductionButton);
        verticalLayout->addWidget(virtualKeyboardButton);
        verticalLayout->addWidget(captureButton);
        verticalLayout->addWidget(fullScreenButton);
        verticalLayout->addWidget(pasteButton);
        verticalLayout->addWidget(screensaverButton);
        verticalLayout->addWidget(recordingButton);
        verticalLayout->addWidget(muteButton);
        verticalLayout->addWidget(aiChatButton);
        verticalLayout->addWidget(toggleSwitch);

        // Adjust button sizes for vertical layout and re-apply icons
        screenScaleButton->setFixedSize(40, 40);
        screenScaleButton->setIconSize(QSize(24, 24));
        setButtonIcon(screenScaleButton, ":/images/screen_scale.svg");

        zoomInButton->setFixedSize(40, 40);
        zoomInButton->setIconSize(QSize(24, 24));
        setButtonIcon(zoomInButton, ":/images/zoom_in.svg");

        zoomOutButton->setFixedSize(40, 40);
        zoomOutButton->setIconSize(QSize(24, 24));
        setButtonIcon(zoomOutButton, ":/images/zoom_out.svg");

        zoomReductionButton->setFixedSize(40, 40);
        zoomReductionButton->setIconSize(QSize(24, 24));
        setButtonIcon(zoomReductionButton, ":/images/zoom_fit.svg");

        virtualKeyboardButton->setFixedSize(40, 40);
        virtualKeyboardButton->setIconSize(QSize(24, 24));
        setButtonIcon(virtualKeyboardButton, ":/images/keyboard.svg");

        captureButton->setFixedSize(40, 40);
        captureButton->setIconSize(QSize(24, 24));
        setButtonIcon(captureButton, ":/images/capture.svg");

        fullScreenButton->setFixedSize(40, 40);
        fullScreenButton->setIconSize(QSize(24, 24));
        setButtonIcon(fullScreenButton, ":/images/full_screen.svg");

        pasteButton->setFixedSize(40, 40);
        pasteButton->setIconSize(QSize(24, 24));
        setButtonIcon(pasteButton, ":/images/paste.svg");

        screensaverButton->setFixedSize(40, 40);
        screensaverButton->setIconSize(QSize(24, 24));
        setButtonIcon(screensaverButton, ":/images/screensaver.svg");

        recordingButton->setFixedSize(40, 40);
        recordingButton->setIconSize(QSize(24, 24));
        setButtonIcon(recordingButton, isRecording ? ":/images/stopRecord.svg" : ":/images/startRecord.svg");

        muteButton->setFixedSize(40, 40);
        muteButton->setIconSize(QSize(24, 24));
        setButtonIcon(muteButton, isMuted ? ":/images/mute.svg" : ":/images/audio.svg");

        aiChatButton->setFixedSize(40, 40);
        aiChatButton->setIconSize(QSize(24, 24));
        setButtonIcon(aiChatButton, ":/images/magic_wand.svg");

        keyboardLayoutComboBox->setMinimumWidth(0);
        keyboardLayoutComboBox->setFixedWidth(50);
        keyboardLayoutComboBox->view()->setMinimumWidth(180);
        toggleSwitch->setFixedSize(50, 30);

        // Show vertical widget as a floating window on the right side of main window
        verticalCornerWidget->setFixedWidth(60);

        // Set as a tool window (independent, floating)
        verticalCornerWidget->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
        updateThemeColors();

        // Install event filter for drag support
        verticalCornerWidget->installEventFilter(this);

        verticalLayout->invalidate();
        verticalLayout->activate();
        verticalCornerWidget->setMinimumSize(verticalLayout->sizeHint());
        verticalCornerWidget->adjustSize();

        verticalCornerWidget->show();
        positionVerticalWidget();
        QTimer::singleShot(0, this, [this] { positionVerticalWidget(); });

        // Disable fullscreen button in portrait mode
        fullScreenButton->setEnabled(false);
        fullScreenButton->setToolTip(tr("Fullscreen not available in portrait mode"));

    } else if (windowWidth < layoutThreshold || isFullScreen) {
        // Landscape but narrow or fullscreen: show horizontal widget as floating
        // Hide vertical widget
        verticalCornerWidget->hide();

        // Move controls back to horizontal layout
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
        horizontalLayout->addWidget(aiChatButton);
        horizontalLayout->addWidget(toggleSwitch);

        // Restore button sizes for horizontal layout
        screenScaleButton->setFixedSize(30, 30);
        screenScaleButton->setIconSize(QSize(16, 16));
        zoomInButton->setFixedSize(30, 30);
        zoomInButton->setIconSize(QSize(16, 16));
        zoomOutButton->setFixedSize(30, 30);
        zoomOutButton->setIconSize(QSize(16, 16));
        zoomReductionButton->setFixedSize(30, 30);
        zoomReductionButton->setIconSize(QSize(16, 16));
        virtualKeyboardButton->setFixedSize(30, 30);
        virtualKeyboardButton->setIconSize(QSize(16, 16));
        captureButton->setFixedSize(30, 30);
        captureButton->setIconSize(QSize(16, 16));
        fullScreenButton->setFixedSize(30, 30);
        fullScreenButton->setIconSize(QSize(16, 16));
        pasteButton->setFixedSize(30, 30);
        pasteButton->setIconSize(QSize(16, 16));
        screensaverButton->setFixedSize(30, 30);
        screensaverButton->setIconSize(QSize(16, 16));
        recordingButton->setFixedSize(30, 30);
        recordingButton->setIconSize(QSize(16, 16));
        muteButton->setFixedSize(30, 30);
        muteButton->setIconSize(QSize(16, 16));
        aiChatButton->setFixedSize(30, 30);
        aiChatButton->setIconSize(QSize(16, 16));
        keyboardLayoutComboBox->setMinimumWidth(100);
        keyboardLayoutComboBox->setMaximumWidth(QWIDGETSIZE_MAX);
        keyboardLayoutComboBox->view()->setMinimumWidth(0);
        toggleSwitch->setFixedSize(78, 28);

        // Show horizontal widget
        cornerWidget->setMaximumWidth(QWIDGETSIZE_MAX);
        horizontalLayout->invalidate();
        horizontalLayout->activate();
        cornerWidget->setMinimumSize(horizontalLayout->sizeHint());
        cornerWidget->resize(horizontalLayout->sizeHint());
        cornerWidget->adjustSize();

        positionHorizontalWidget(true, isFullScreen);
    } else {
        // Normal landscape mode: show horizontal widget in menu bar corner
        // Hide vertical widget
        verticalCornerWidget->hide();

        // Move controls back to horizontal layout
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
        horizontalLayout->addWidget(aiChatButton);
        horizontalLayout->addWidget(toggleSwitch);

        // Restore button sizes for horizontal layout
        screenScaleButton->setFixedSize(30, 30);
        screenScaleButton->setIconSize(QSize(16, 16));
        zoomInButton->setFixedSize(30, 30);
        zoomInButton->setIconSize(QSize(16, 16));
        zoomOutButton->setFixedSize(30, 30);
        zoomOutButton->setIconSize(QSize(16, 16));
        zoomReductionButton->setFixedSize(30, 30);
        zoomReductionButton->setIconSize(QSize(16, 16));
        virtualKeyboardButton->setFixedSize(30, 30);
        virtualKeyboardButton->setIconSize(QSize(16, 16));
        captureButton->setFixedSize(30, 30);
        captureButton->setIconSize(QSize(16, 16));
        fullScreenButton->setFixedSize(30, 30);
        fullScreenButton->setIconSize(QSize(16, 16));
        pasteButton->setFixedSize(30, 30);
        pasteButton->setIconSize(QSize(16, 16));
        screensaverButton->setFixedSize(30, 30);
        screensaverButton->setIconSize(QSize(16, 16));
        recordingButton->setFixedSize(30, 30);
        recordingButton->setIconSize(QSize(16, 16));
        muteButton->setFixedSize(30, 30);
        muteButton->setIconSize(QSize(16, 16));
        aiChatButton->setFixedSize(30, 30);
        aiChatButton->setIconSize(QSize(16, 16));
        keyboardLayoutComboBox->setMinimumWidth(100);
        keyboardLayoutComboBox->setMaximumWidth(QWIDGETSIZE_MAX);
        keyboardLayoutComboBox->view()->setMinimumWidth(0);
        toggleSwitch->setFixedSize(78, 28);

        // Show horizontal widget
        cornerWidget->setMaximumWidth(horizontalLayout->sizeHint().width());
        horizontalLayout->invalidate();
        horizontalLayout->activate();
        cornerWidget->setMinimumSize(horizontalLayout->sizeHint());
        cornerWidget->resize(horizontalLayout->sizeHint());
        cornerWidget->adjustSize();

        if (menuBar) {
            menuBar->setCornerWidget(cornerWidget, Qt::TopRightCorner);
            cornerWidget->show();
        }
    }
}

void CornerWidgetManager::positionVerticalWidget()
{
    QWidget *mainWindow = menuBar ? menuBar->window() : nullptr;
    if (!mainWindow || verticalCornerWidget->isHidden()) {
        return;
    }

    const QRect frame = mainWindow->frameGeometry();
    QPoint target = verticalCornerWidget->pos();
    if (m_verticalUserPositioned && !m_lastMainWindowPos.isNull()) {
        target += frame.topLeft() - m_lastMainWindowPos;
    } else {
        target = QPoint(frame.right() + 1, frame.y() + menuBar->height() + 10);
    }
    m_lastMainWindowPos = frame.topLeft();

    if (target != verticalCornerWidget->pos()) {
        verticalCornerWidget->move(target);
    }
}

void CornerWidgetManager::positionHorizontalWidget(bool floating, bool fullscreen)
{
    QWidget *mainWindow = menuBar ? menuBar->window() : nullptr;
    if (!mainWindow) {
        return;
    }

    if (floating) {
        if (cornerWidget->parentWidget() != mainWindow) {
            cornerWidget->setParent(mainWindow);
        }
        if (menuBar->cornerWidget(Qt::TopRightCorner) == cornerWidget) {
            menuBar->setCornerWidget(nullptr, Qt::TopRightCorner);
        }
        const QSize size = cornerWidget->size();
        const int x = qMax(0, mainWindow->width() - size.width());
        const int y = fullscreen ? 10 : menuBar->height() + 10;
        cornerWidget->setGeometry(x, y, size.width(), size.height());
        cornerWidget->raise();
        cornerWidget->show();
    } else if (menuBar->cornerWidget(Qt::TopRightCorner) != cornerWidget) {
        menuBar->setCornerWidget(cornerWidget, Qt::TopRightCorner);
        cornerWidget->show();
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
    if (m_isPortraitMode) {
        fullScreenButton->setToolTip(tr("Fullscreen not available in portrait mode"));
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