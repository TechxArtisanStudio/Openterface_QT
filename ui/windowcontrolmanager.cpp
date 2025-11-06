/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#include "windowcontrolmanager.h"
#include <QEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QScreen>
#include <QDebug>
#include <QLoggingCategory>
#include <QMenuBar>

Q_LOGGING_CATEGORY(log_ui_windowcontrolmanager, "opf.ui.windowcontrolmanager")

WindowControlManager::WindowControlManager(QMainWindow *mainWindow, QToolBar *toolbar, QObject *parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
    , m_toolbar(toolbar)
    , m_autoHideTimer(new QTimer(this))
    , m_edgeCheckTimer(new QTimer(this))
    , m_autoHideEnabled(false)
    , m_autoHideDelay(5000)  // Default 5 seconds
    , m_edgeThreshold(5)      // Default 5 pixels from edge
    , m_animationDuration(300) // Default 300ms animation
    , m_toolbarAutoHidden(false)
    , m_isMaximized(false)
    , m_isFullScreen(false)
    , m_mouseAtTopEdge(false)
    , m_eventFilterInstalled(false)
{
    setupConnections();
}

WindowControlManager::~WindowControlManager()
{
    removeEventFilterFromWindow();
    
    if (m_autoHideTimer) {
        m_autoHideTimer->stop();
    }
    
    if (m_edgeCheckTimer) {
        m_edgeCheckTimer->stop();
    }
}

void WindowControlManager::setupConnections()
{
    // Connect auto-hide timer
    connect(m_autoHideTimer, &QTimer::timeout, this, [this]() {
        qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] *** TIMER TIMEOUT TRIGGERED ***";
        qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] Checking conditions:";
        qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE]   - AutoHide enabled:" << m_autoHideEnabled;
        qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE]   - Is fullscreen:" << m_isFullScreen;
        qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE]   - Is maximized:" << m_isMaximized;
        
        // Check visibility based on mode
        bool isVisible = false;
        if (m_isFullScreen) {
            isVisible = isMenuBarVisible();
            qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE]   - Menu bar visible:" << isVisible;
        } else {
            isVisible = isToolbarVisible();
            qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE]   - Toolbar visible:" << isVisible;
        }
        
        // Auto-hide only works in fullscreen mode, not in maximized mode
        if (m_autoHideEnabled && m_isFullScreen && isVisible) {
            qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] *** CONDITIONS MET - HIDING TOOLBAR/MENUBAR ***";
            hideToolbar();
        } else {
            qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] *** CONDITIONS NOT MET - NOT HIDDEN ***";
            if (!m_isFullScreen) {
                qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] Not in fullscreen mode - auto-hide disabled";
            }
        }
    });
    
    // Connect edge check timer
    connect(m_edgeCheckTimer, &QTimer::timeout, this, &WindowControlManager::checkMousePosition);
    
    // Single-shot timers
    m_autoHideTimer->setSingleShot(true);
    m_edgeCheckTimer->setSingleShot(false);
    m_edgeCheckTimer->setInterval(100); // Check every 100ms
    
    // Note: Menu bar signal connections removed - they were interfering with normal menu operation
    // Auto-hide timer management is now handled entirely through the event filter and mouse position checking
}

void WindowControlManager::setToolbar(QToolBar *toolbar)
{
    m_toolbar = toolbar;
}

void WindowControlManager::setAutoHideEnabled(bool enabled)
{
    qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] setAutoHideEnabled called with:" << enabled;
    
    if (m_autoHideEnabled == enabled) {
        qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] Already in desired state, no change needed";
        return;
    }
    
    m_autoHideEnabled = enabled;
    qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] Auto-hide state changed to:" << enabled;
    
    if (enabled) {
        // CRITICAL FIX: Only install event filter in fullscreen mode
        // In normal/maximized mode, we don't need the event filter as it can interfere with menu clicks
        if (m_isFullScreen) {
            qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] Installing event filter (fullscreen mode)";
            installEventFilterOnWindow();
        } else {
            qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] Auto-hide enabled but not installing event filter (not in fullscreen mode yet)";
        }
        
        // If already in fullscreen mode, ensure toolbar is visible and start auto-hide sequence
        // NOTE: Auto-hide only works in fullscreen mode, not in maximized mode
        if (m_isFullScreen && m_toolbar) {
            qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] Window is already in fullscreen, ensuring toolbar is visible";
            if (!m_toolbar->isVisible()) {
                qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] Showing toolbar before starting auto-hide";
                showToolbar();
            } else {
                qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] Toolbar already visible, starting auto-hide timer";
                startAutoHideTimer();
            }
        } else if (m_isMaximized) {
            qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] Window is maximized (not fullscreen) - auto-hide disabled, toolbar stays visible";
        }
    } else {
        // Remove event filter
        removeEventFilterFromWindow();
        
        // Stop timers
        stopAutoHideTimer();
        m_edgeCheckTimer->stop();
        
        // Show toolbar if it was auto-hidden
        if (m_toolbarAutoHidden && m_toolbar) {
            showToolbar();
            m_toolbarAutoHidden = false;
        }
    }
}

void WindowControlManager::setAutoHideDelay(int milliseconds)
{
    m_autoHideDelay = qMax(1000, milliseconds); // Minimum 1 second
}

void WindowControlManager::setEdgeDetectionThreshold(int pixels)
{
    m_edgeThreshold = qMax(1, pixels); // Minimum 1 pixel
}

void WindowControlManager::setAnimationDuration(int milliseconds)
{
    m_animationDuration = qMax(0, milliseconds); // Minimum 0ms (instant)
}

bool WindowControlManager::isAutoHideEnabled() const
{
    return m_autoHideEnabled;
}

bool WindowControlManager::isToolbarVisible() const
{
    return m_toolbar ? m_toolbar->isVisible() : false;
}

bool WindowControlManager::isMenuBarVisible() const
{
    if (m_mainWindow && m_mainWindow->menuBar()) {
        return m_mainWindow->menuBar()->isVisible();
    }
    return false;
}

bool WindowControlManager::isMaximized() const
{
    return m_isMaximized;
}

bool WindowControlManager::isFullScreen() const
{
    return m_isFullScreen;
}

void WindowControlManager::showToolbar()
{
    qCDebug(log_ui_windowcontrolmanager) << "[TOOLBAR] showToolbar() called";
    
    // In fullscreen mode, check menu bar visibility
    // In other modes, check function keys toolbar visibility
    bool isVisible = false;
    if (m_isFullScreen) {
        isVisible = isMenuBarVisible();
    } else {
        if (!m_toolbar) {
            qCWarning(log_ui_windowcontrolmanager) << "[TOOLBAR] ERROR: Toolbar is NULL!";
            return;
        }
        isVisible = m_toolbar->isVisible();
    }
    
    if (isVisible) {
        qCDebug(log_ui_windowcontrolmanager) << "[TOOLBAR] Already visible, restarting auto-hide timer";
        // Already visible, just restart the auto-hide timer
        if (m_autoHideEnabled && (m_isFullScreen || m_isMaximized)) {
            startAutoHideTimer();
        }
        return;
    }
    
    qCDebug(log_ui_windowcontrolmanager) << "[TOOLBAR] *** SHOWING TOOLBAR/MENUBAR ***";
    animateToolbarShow();
    m_toolbarAutoHidden = false;
    emit toolbarVisibilityChanged(true);
    
    // Start auto-hide timer after showing (only in fullscreen mode)
    if (m_autoHideEnabled && m_isFullScreen) {
        qCDebug(log_ui_windowcontrolmanager) << "[TOOLBAR] Starting auto-hide timer after show (fullscreen mode)";
        startAutoHideTimer();
    }
}

void WindowControlManager::hideToolbar()
{
    qCDebug(log_ui_windowcontrolmanager) << "[TOOLBAR] hideToolbar() called";
    
    // Check visibility based on current mode
    bool currentlyVisible = m_isFullScreen ? isMenuBarVisible() : isToolbarVisible();
    qCDebug(log_ui_windowcontrolmanager) << "[TOOLBAR] Currently visible (" 
             << (m_isFullScreen ? "menubar" : "toolbar") << "):" << currentlyVisible;
    
    if (!currentlyVisible) {
        qCDebug(log_ui_windowcontrolmanager) << "[TOOLBAR] Already hidden";
        return;
    }
    
    qCDebug(log_ui_windowcontrolmanager) << "[TOOLBAR] Checking conditions before hiding";

    // Don't hide if a menu is open
    if (m_mainWindow && m_mainWindow->menuBar()) {
        QMenu *activeMenu = m_mainWindow->menuBar()->activeAction() 
                            ? m_mainWindow->menuBar()->activeAction()->menu() 
                            : nullptr;
        if (activeMenu && activeMenu->isVisible()) {
            qCDebug(log_ui_windowcontrolmanager) << "[TOOLBAR] Not hiding - menu is active, restarting timer";
            startAutoHideTimer(); // Restart timer
            return;
        } else {
            qCDebug(log_ui_windowcontrolmanager) << "[TOOLBAR] No active menu, proceeding with hide";
        }
    }
    
    qCDebug(log_ui_windowcontrolmanager) << "[TOOLBAR] *** HIDING TOOLBAR/MENUBAR (AUTO-HIDE) ***";
    animateToolbarHide();
    m_toolbarAutoHidden = true;
    stopAutoHideTimer();
    
    // Keep edge check timer running so we can detect when mouse hovers at top to show menu bar again
    qCDebug(log_ui_windowcontrolmanager) << "[TOOLBAR] Edge check timer will continue to detect mouse at top edge";
    
    emit toolbarVisibilityChanged(false);
    emit autoHideTriggered();
    qCDebug(log_ui_windowcontrolmanager) << "[TOOLBAR] Toolbar/menubar hidden successfully, auto-hide triggered signal emitted";
}

void WindowControlManager::toggleToolbar()
{
    if (isToolbarVisible()) {
        hideToolbar();
    } else {
        showToolbar();
    }
}

void WindowControlManager::onWindowMaximized()
{
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::onWindowMaximized() - Window maximized";
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::onWindowMaximized() - AutoHide enabled:" << m_autoHideEnabled
             << "Toolbar exists:" << (m_toolbar != nullptr)
             << "Toolbar visible:" << (m_toolbar ? m_toolbar->isVisible() : false);
    
    m_isMaximized = true;
    m_isFullScreen = false;
    
    // NOTE: Auto-hide is ONLY enabled in fullscreen mode, not in maximized mode
    // In maximized mode, toolbar should always be visible
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::onWindowMaximized() - Maximized mode: toolbar stays visible (auto-hide only in fullscreen)";
    
    // Ensure toolbar is visible in maximized mode
    if (m_toolbar && !m_toolbar->isVisible()) {
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::onWindowMaximized() - Showing toolbar for maximized mode";
        showToolbar();
    }
    
    // Stop any auto-hide behavior from fullscreen mode
    stopAutoHideTimer();
    m_edgeCheckTimer->stop();
}

void WindowControlManager::onWindowRestored()
{
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager: Window restored to normal";
    m_isMaximized = false;
    m_isFullScreen = false;
    
    // CRITICAL FIX: Remove event filter when exiting fullscreen/maximized mode
    if (m_eventFilterInstalled) {
        qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] Removing event filter (exiting fullscreen mode)";
        removeEventFilterFromWindow();
    }
    
    // Stop auto-hide behavior
    stopAutoHideTimer();
    m_edgeCheckTimer->stop();
    
    // Show toolbar if it was auto-hidden
    if (m_toolbarAutoHidden && m_toolbar) {
        showToolbar();
        m_toolbarAutoHidden = false;
    }
}

void WindowControlManager::onWindowFullScreen()
{
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager: Window entered fullscreen";
    
    // If already in fullscreen, ignore duplicate calls (prevent timer restarts during window state flicker)
    if (m_isFullScreen) {
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::onWindowFullScreen() - Already in fullscreen, ignoring duplicate";
        return;
    }
    
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::onWindowFullScreen() - AutoHide enabled:" << m_autoHideEnabled
             << "Toolbar exists:" << (m_toolbar != nullptr)
             << "Toolbar visible:" << (m_toolbar ? m_toolbar->isVisible() : false);
    
    m_isFullScreen = true;
    m_isMaximized = false; // Fullscreen is separate from maximized
    
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::onWindowFullScreen() - Fullscreen mode activated";
    
    // CRITICAL FIX: Install event filter when entering fullscreen mode
    if (m_autoHideEnabled && !m_eventFilterInstalled) {
        qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] Installing event filter for fullscreen mode";
        installEventFilterOnWindow();
    }
    
    // In fullscreen mode:
    // 1. The function keys toolbar should stay HIDDEN (not auto-shown)
    // 2. The menu bar (with corner widget) should auto-hide after timeout
    // 3. Start auto-hide timer for menu bar (only once)
    
    if (m_autoHideEnabled) {
        qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] Starting auto-hide timer for menu bar in fullscreen";
        startAutoHideTimer();
        if (!m_edgeCheckTimer->isActive()) {
            m_edgeCheckTimer->start();
        }
    }

    // Do NOT auto-show the function keys toolbar - it should stay hidden
    // Only menu bar will be hidden/shown based on timer and mouse position
}

void WindowControlManager::onWindowStateChanged(Qt::WindowStates oldState, Qt::WindowStates newState)
{
    bool wasMaximized = m_isMaximized;
    bool wasFullScreen = m_isFullScreen;
    
    bool isMaximized = (newState & Qt::WindowMaximized) != 0;
    bool isFullScreen = (newState & Qt::WindowFullScreen) != 0;
    
    // Detect state transitions and call appropriate handlers
    // Only call handlers on actual state CHANGES to prevent duplicates
    if (isMaximized && !wasMaximized) {
        onWindowMaximized();
    } else if (!isMaximized && !isFullScreen && (wasMaximized || wasFullScreen)) {
        onWindowRestored();
    } else if (isFullScreen && !wasFullScreen) {
        onWindowFullScreen();
    }
    
    // Handlers update their own flags, but ensure consistency
    // Update flags only if handlers didn't already
    if (m_isMaximized != isMaximized || m_isFullScreen != isFullScreen) {
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::onWindowStateChanged() - Syncing flags after handler";
        m_isMaximized = isMaximized;
        m_isFullScreen = isFullScreen;
    }
}

void WindowControlManager::onMouseMoved(const QPoint &globalPos)
{
    // Auto-hide only works in fullscreen mode, not in maximized mode
    if (!m_autoHideEnabled || !m_isFullScreen) {
        return;
    }
    
    // Check if mouse actually moved (more than 5 pixels) to avoid constant timer resets
    bool mouseActuallyMoved = false;
    if (m_lastMousePos.isNull() || 
        (qAbs(globalPos.x() - m_lastMousePos.x()) > 5 || qAbs(globalPos.y() - m_lastMousePos.y()) > 5)) {
        mouseActuallyMoved = true;
        m_lastMousePos = globalPos;
    }
    
    // Check if mouse is at top edge (where menu bar is)
    bool atEdge = isMouseAtTopEdge(globalPos);
    
    if (atEdge && !m_mouseAtTopEdge) {
        // Mouse just entered top edge (menu bar area)
        qCDebug(log_ui_windowcontrolmanager) << "[MOUSE] *** MOUSE ENTERED TOP EDGE (MENU BAR AREA) ***";
        m_mouseAtTopEdge = true;
        emit edgeHoverDetected();
        
        if (m_toolbarAutoHidden) {
            qCDebug(log_ui_windowcontrolmanager) << "[MOUSE] Menu bar is auto-hidden, showing it";
            showToolbar();
        } else {
            qCDebug(log_ui_windowcontrolmanager) << "[MOUSE] Menu bar is visible, restarting auto-hide timer";
            // User is interacting with menu bar area, restart timer
            startAutoHideTimer();
        }
    } else if (!atEdge && m_mouseAtTopEdge) {
        // Mouse left top edge (menu bar area)
        qCDebug(log_ui_windowcontrolmanager) << "[MOUSE] Mouse left top edge (menu bar area)";
        m_mouseAtTopEdge = false;
        // Mouse left menu bar, start/restart auto-hide countdown
        if (isMenuBarVisible()) {
            qCDebug(log_ui_windowcontrolmanager) << "[MOUSE] Mouse left menu bar, starting auto-hide countdown";
            startAutoHideTimer();
        }
    } else if (atEdge && mouseActuallyMoved) {
        // Mouse is still in menu bar area and moved
        // Restart timer only if mouse is in menu bar area
        if (isMenuBarVisible()) {
            qCDebug(log_ui_windowcontrolmanager) << "[MOUSE] Mouse activity in menu bar area, restarting timer";
            startAutoHideTimer();
        }
    }
    // NOTE: Mouse movement outside menu bar area does NOT restart timer
    // This allows auto-hide to proceed even when user is moving mouse elsewhere
}

void WindowControlManager::checkMousePosition()
{
    // Auto-hide only works in fullscreen mode, not in maximized mode
    if (!m_mainWindow || !m_autoHideEnabled || !m_isFullScreen) {
        return;
    }
    
    QPoint globalPos = QCursor::pos();
    
    // Debug: Log every 100th check to see if this is being called
    static int posCheckCount = 0;
    if (++posCheckCount % 100 == 0) {
        qCDebug(log_ui_windowcontrolmanager) << "[CHECK-POS] Checking mouse position:" << globalPos 
                 << "(check #" << posCheckCount << ")";
    }
    
    onMouseMoved(globalPos);
}

void WindowControlManager::startAutoHideTimer()
{
    qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] startAutoHideTimer() called";
    
    if (!m_autoHideTimer) {
        qCWarning(log_ui_windowcontrolmanager) << "[AUTO-HIDE] ERROR: Timer is NULL!";
        return;
    }
    
    qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE]   - Delay:" << m_autoHideDelay << "ms";
    qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE]   - AutoHide enabled:" << m_autoHideEnabled;
    qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE]   - Is fullscreen:" << m_isFullScreen;
    qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE]   - Timer active:" << m_autoHideTimer->isActive();
    
    if (m_autoHideTimer->isActive()) {
        int remaining = m_autoHideTimer->remainingTime();
        qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE]   - Timer already running, remaining:" << remaining << "ms";
        // If timer has less than 1 second left, let it finish. Otherwise restart.
        if (remaining > 1000) {
            qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] Timer restarted (more than 1s remaining)";
            m_autoHideTimer->start(m_autoHideDelay);
        } else {
            qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] Timer near completion, letting it finish";
        }
    } else {
        // Timer not running, start it
        m_autoHideTimer->start(m_autoHideDelay);
        qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] Timer STARTED - will fire in" << m_autoHideDelay << "ms";
    }
}

void WindowControlManager::stopAutoHideTimer()
{
    qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] stopAutoHideTimer() called";
    if (m_autoHideTimer) {
        m_autoHideTimer->stop();
        qCDebug(log_ui_windowcontrolmanager) << "[AUTO-HIDE] Timer STOPPED";
    }
}

void WindowControlManager::animateToolbarShow()
{
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarShow() - Start";
    
    // In fullscreen mode, we need to show the MENU BAR (not the function keys toolbar)
    if (m_isFullScreen && m_mainWindow && m_mainWindow->menuBar()) {
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarShow() - Showing MENU BAR in fullscreen";
        QMenuBar *menuBar = m_mainWindow->menuBar();
        
        if (!menuBar) {
            qCWarning(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarShow() - ERROR: Menu bar became null!";
            return;
        }
        
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarShow() - Menu bar before show - isVisible:" << menuBar->isVisible();
        
        menuBar->show();
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarShow() - Called show()";
        
        menuBar->raise();  // Ensure it's on top
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarShow() - Called raise()";
        
        menuBar->update(); // Force repaint
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarShow() - Called update()";
        
        // Removed processEvents - Qt will handle event processing naturally
        
        m_mainWindow->update(); // Update main window
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarShow() - Called mainWindow update()";
        
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarShow() - Menu bar shown, raised, and updated";
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarShow() - Menu bar geometry:" << menuBar->geometry();
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarShow() - Menu bar isVisible:" << menuBar->isVisible();
        return;
    }
    
    if (!m_toolbar) {
        qWarning() << "WindowControlManager::animateToolbarShow() - m_toolbar is null!";
        return;
    }
    
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarShow() - Current state - Visible:" << m_toolbar->isVisible()
             << "Height:" << m_toolbar->height() << "MaxHeight:" << m_toolbar->maximumHeight();
    
    // Reset maximum height constraint to allow toolbar to expand fully
    m_toolbar->setMaximumHeight(QWIDGETSIZE_MAX);
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarShow() - Reset maximumHeight to QWIDGETSIZE_MAX";
    
    // For now, just show/hide directly
    // You can add animation here if needed
    m_toolbar->show();
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarShow() - Toolbar shown successfully";
}

void WindowControlManager::animateToolbarHide()
{
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarHide() - Start";
    
    // In fullscreen mode, we need to hide the MENU BAR (not the function keys toolbar)
    if (m_isFullScreen && m_mainWindow && m_mainWindow->menuBar()) {
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarHide() - Hiding MENU BAR in fullscreen";
        QMenuBar *menuBar = m_mainWindow->menuBar();
        menuBar->hide();
        m_mainWindow->update(); // Update main window to reflect the change
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarHide() - Menu bar hidden and window updated";
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarHide() - Menu bar isVisible:" << menuBar->isVisible();
        return;
    }
    
    if (!m_toolbar) {
        qWarning() << "WindowControlManager::animateToolbarHide() - m_toolbar is null!";
        return;
    }
    
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarHide() - Current state - Visible:" << m_toolbar->isVisible()
             << "Height:" << m_toolbar->height() << "MaxHeight:" << m_toolbar->maximumHeight();
    
    // For now, just show/hide directly
    // You can add animation here if needed
    m_toolbar->hide();
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarHide() - Toolbar hidden";
    
    // Reset maximum height after hiding to ensure clean state
    m_toolbar->setMaximumHeight(QWIDGETSIZE_MAX);
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarHide() - Reset maximumHeight to QWIDGETSIZE_MAX";
}

bool WindowControlManager::isMouseAtTopEdge(const QPoint &globalPos)
{
    if (!m_mainWindow) {
        return false;
    }
    
    // Get window geometry in global coordinates
    QRect windowRect = m_mainWindow->geometry();
    
    // In fullscreen, the window typically starts at (0,0)
    // We want to detect if mouse is within the top menu bar area
    int topEdge = windowRect.top();
    int menuBarHeight = 30; // Default menu bar height
    
    if (m_mainWindow->menuBar()) {
        menuBarHeight = qMax(30, m_mainWindow->menuBar()->sizeHint().height());
        topEdge += menuBarHeight;
    }
    
    // Check if mouse is within the window horizontally
    bool withinHorizontalBounds = globalPos.x() >= windowRect.left() && 
                                  globalPos.x() <= windowRect.right();
    
    // Check if mouse is within the top menu bar area vertically
    // Using a larger threshold to make it easier to trigger
    bool withinVerticalThreshold = globalPos.y() >= windowRect.top() && 
                                    globalPos.y() <= (windowRect.top() + menuBarHeight + m_edgeThreshold);
    
    bool result = withinHorizontalBounds && withinVerticalThreshold;
    
    // Debug logging every 50th check to avoid spam
    static int checkCount = 0;
    if (++checkCount % 50 == 0 || result) {
        qCDebug(log_ui_windowcontrolmanager) << "[EDGE-CHECK] Mouse pos:" << globalPos 
                 << "Window:" << windowRect 
                 << "MenuBar height:" << menuBarHeight
                 << "Threshold:" << m_edgeThreshold
                 << "Checking Y:" << globalPos.y() << "<=" << (windowRect.top() + menuBarHeight + m_edgeThreshold)
                 << "H-bounds:" << withinHorizontalBounds 
                 << "V-threshold:" << withinVerticalThreshold
                 << "AT EDGE:" << result;
    }
    
    return withinHorizontalBounds && withinVerticalThreshold;
}

void WindowControlManager::installEventFilterOnWindow()
{
    if (m_mainWindow && !m_eventFilterInstalled) {
        m_mainWindow->installEventFilter(this);
        m_eventFilterInstalled = true;
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager: Event filter installed";
    }
}

void WindowControlManager::removeEventFilterFromWindow()
{
    if (m_mainWindow && m_eventFilterInstalled) {
        m_mainWindow->removeEventFilter(this);
        m_eventFilterInstalled = false;
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager: Event filter removed";
    }
}

bool WindowControlManager::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_mainWindow) {
        if (event->type() == QEvent::MouseMove) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            onMouseMoved(m_mainWindow->mapToGlobal(mouseEvent->pos()));
        } else if (event->type() == QEvent::WindowStateChange) {
            QWindowStateChangeEvent *stateEvent = static_cast<QWindowStateChangeEvent*>(event);
            onWindowStateChanged(stateEvent->oldState(), m_mainWindow->windowState());
        } else if (event->type() == QEvent::MouseButtonPress) {
            // CRITICAL FIX: Only handle menu bar clicks in FULLSCREEN mode
            // In normal/maximized mode, let Qt handle menu clicks naturally
            if (!m_isFullScreen) {
                // Not in fullscreen - don't interfere with menu clicks
                return QObject::eventFilter(watched, event);
            }
            
            // In fullscreen mode: Stop auto-hide timer when user clicks in menu bar area
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            QPoint globalPos = m_mainWindow->mapToGlobal(mouseEvent->pos());
            
            // Check if click is in menu bar area
            if (m_mainWindow->menuBar()) {
                QRect menuBarRect = m_mainWindow->menuBar()->geometry();
                QPoint menuBarGlobalPos = m_mainWindow->menuBar()->mapToGlobal(QPoint(0, 0));
                QRect globalMenuBarRect(menuBarGlobalPos, menuBarRect.size());
                
                if (globalMenuBarRect.contains(globalPos)) {
                    qCDebug(log_ui_windowcontrolmanager) << "[MENU-FIX] Mouse click in menu bar area (fullscreen mode) - stopping auto-hide timer";
                    stopAutoHideTimer();
                    
                    // Ensure menu bar is visible for the click in fullscreen mode
                    if (!m_mainWindow->menuBar()->isVisible()) {
                        qCDebug(log_ui_windowcontrolmanager) << "[MENU-FIX] Menu bar was hidden, showing it now";
                        showToolbar();
                    }
                }
            }
        }
    }
    
    // Always pass events through - never block them
    return QObject::eventFilter(watched, event);
}
