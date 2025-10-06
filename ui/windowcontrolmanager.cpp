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
    , m_autoHideDelay(10000)  // Default 10 seconds
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
        if (m_autoHideEnabled && m_isMaximized && m_toolbar && m_toolbar->isVisible()) {
            hideToolbar();
        }
    });
    
    // Connect edge check timer
    connect(m_edgeCheckTimer, &QTimer::timeout, this, &WindowControlManager::checkMousePosition);
    
    // Single-shot timers
    m_autoHideTimer->setSingleShot(true);
    m_edgeCheckTimer->setSingleShot(false);
    m_edgeCheckTimer->setInterval(100); // Check every 100ms
}

void WindowControlManager::setToolbar(QToolBar *toolbar)
{
    m_toolbar = toolbar;
}

void WindowControlManager::setAutoHideEnabled(bool enabled)
{
    if (m_autoHideEnabled == enabled) {
        return;
    }
    
    m_autoHideEnabled = enabled;
    
    if (enabled) {
        // Install event filter to track mouse movement
        installEventFilterOnWindow();
        
        // If already maximized, start auto-hide sequence
        if (m_isMaximized && m_toolbar && m_toolbar->isVisible()) {
            startAutoHideTimer();
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
    if (!m_toolbar) {
        return;
    }
    
    if (m_toolbar->isVisible()) {
        // Already visible, just restart the auto-hide timer
        if (m_autoHideEnabled && m_isMaximized) {
            startAutoHideTimer();
        }
        return;
    }
    
    animateToolbarShow();
    m_toolbarAutoHidden = false;
    emit toolbarVisibilityChanged(true);
    
    // Start auto-hide timer after showing
    if (m_autoHideEnabled && m_isMaximized) {
        startAutoHideTimer();
    }
}

void WindowControlManager::hideToolbar()
{
    if (!m_toolbar || !m_toolbar->isVisible()) {
        return;
    }
    
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::hideToolbar() - Checking conditions before hiding";

    // Don't hide if a menu is open
    if (m_mainWindow && m_mainWindow->menuBar()) {
        QMenu *activeMenu = m_mainWindow->menuBar()->activeAction() 
                            ? m_mainWindow->menuBar()->activeAction()->menu() 
                            : nullptr;
        if (activeMenu && activeMenu->isVisible()) {
            qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager: Not hiding toolbar - menu is active";
            startAutoHideTimer(); // Restart timer
            return;
        }
    }
    
    animateToolbarHide();
    m_toolbarAutoHidden = true;
    stopAutoHideTimer();
    emit toolbarVisibilityChanged(false);
    emit autoHideTriggered();
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
    
    if (m_autoHideEnabled && m_toolbar && m_toolbar->isVisible()) {
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::onWindowMaximized() - Starting auto-hide timer and edge detection";
        // Start the auto-hide timer when maximized
        startAutoHideTimer();
        // Start edge detection
        m_edgeCheckTimer->start();
    } else {
        qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::onWindowMaximized() - Not starting auto-hide (conditions not met)";
    }
}

void WindowControlManager::onWindowRestored()
{
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager: Window restored to normal";
    m_isMaximized = false;
    m_isFullScreen = false;
    
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
    m_isFullScreen = true;
    m_isMaximized = false; // Fullscreen is separate from maximized
    
    if (m_autoHideEnabled && m_toolbar && m_toolbar->isVisible()) {
        // In fullscreen, hide toolbar immediately
        QTimer::singleShot(m_autoHideDelay, this, [this]() {
            if (m_isFullScreen) {
                hideToolbar();
            }
        });
        m_edgeCheckTimer->start();
    }
}

void WindowControlManager::onWindowStateChanged(Qt::WindowStates oldState, Qt::WindowStates newState)
{
    qCDebug(log_ui_windowcontrolmanager) << "[CRASH DEBUG] WindowControlManager::onWindowStateChanged() - START";
    qCDebug(log_ui_windowcontrolmanager) << "[CRASH DEBUG] Old state:" << oldState << "New state:" << newState;
    
    bool wasMaximized = m_isMaximized;
    bool wasFullScreen = m_isFullScreen;
    
    m_isMaximized = (newState & Qt::WindowMaximized) != 0;
    m_isFullScreen = (newState & Qt::WindowFullScreen) != 0;
    
    qCDebug(log_ui_windowcontrolmanager) << "[CRASH DEBUG] State transition:";
    qCDebug(log_ui_windowcontrolmanager) << "[CRASH DEBUG]   Was Maximized:" << wasMaximized << "-> Now Maximized:" << m_isMaximized;
    qCDebug(log_ui_windowcontrolmanager) << "[CRASH DEBUG]   Was FullScreen:" << wasFullScreen << "-> Now FullScreen:" << m_isFullScreen;
    
    if (m_isMaximized && !wasMaximized) {
        qCDebug(log_ui_windowcontrolmanager) << "[CRASH DEBUG] *** WINDOW BEING MAXIMIZED - Calling onWindowMaximized() ***";
        onWindowMaximized();
        qCDebug(log_ui_windowcontrolmanager) << "[CRASH DEBUG] onWindowMaximized() completed";
    } else if (!m_isMaximized && !m_isFullScreen && (wasMaximized || wasFullScreen)) {
        qCDebug(log_ui_windowcontrolmanager) << "[CRASH DEBUG] *** WINDOW BEING RESTORED - Calling onWindowRestored() ***";
        onWindowRestored();
        qCDebug(log_ui_windowcontrolmanager) << "[CRASH DEBUG] onWindowRestored() completed";
    } else if (m_isFullScreen && !wasFullScreen) {
        qCDebug(log_ui_windowcontrolmanager) << "[CRASH DEBUG] *** WINDOW ENTERING FULLSCREEN - Calling onWindowFullScreen() ***";
        onWindowFullScreen();
        qCDebug(log_ui_windowcontrolmanager) << "[CRASH DEBUG] onWindowFullScreen() completed";
    } else {
        qCDebug(log_ui_windowcontrolmanager) << "[CRASH DEBUG] No state handler triggered";
    }
    
    qCDebug(log_ui_windowcontrolmanager) << "[CRASH DEBUG] WindowControlManager::onWindowStateChanged() - END";
}

void WindowControlManager::onMouseMoved(const QPoint &globalPos)
{
    m_lastMousePos = globalPos;
    
    if (!m_autoHideEnabled || !m_isMaximized) {
        return;
    }
    
    // Check if mouse is at top edge
    bool atEdge = isMouseAtTopEdge(globalPos);
    
    if (atEdge && !m_mouseAtTopEdge) {
        // Mouse just entered top edge
        m_mouseAtTopEdge = true;
        emit edgeHoverDetected();
        
        if (m_toolbarAutoHidden) {
            showToolbar();
        }
    } else if (!atEdge && m_mouseAtTopEdge) {
        // Mouse left top edge
        m_mouseAtTopEdge = false;
    }
    
    // Reset auto-hide timer on mouse movement if toolbar is visible
    if (m_toolbar && m_toolbar->isVisible()) {
        startAutoHideTimer();
    }
}

void WindowControlManager::checkMousePosition()
{
    if (!m_mainWindow || !m_autoHideEnabled || !m_isMaximized) {
        return;
    }
    
    QPoint globalPos = QCursor::pos();
    onMouseMoved(globalPos);
}

void WindowControlManager::startAutoHideTimer()
{
    if (m_autoHideTimer) {
        m_autoHideTimer->start(m_autoHideDelay);
    }
}

void WindowControlManager::stopAutoHideTimer()
{
    if (m_autoHideTimer) {
        m_autoHideTimer->stop();
    }
}

void WindowControlManager::animateToolbarShow()
{
    qCDebug(log_ui_windowcontrolmanager) << "WindowControlManager::animateToolbarShow() - Start";
    
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
    
    // Get window geometry
    QRect windowRect = m_mainWindow->geometry();
    
    // Check if mouse is within threshold of top edge
    int topEdge = windowRect.top();
    
    // Also check menu bar height if present
    if (m_mainWindow->menuBar()) {
        topEdge += m_mainWindow->menuBar()->height();
    }
    
    bool withinHorizontalBounds = globalPos.x() >= windowRect.left() && 
                                  globalPos.x() <= windowRect.right();
    bool withinVerticalThreshold = globalPos.y() >= windowRect.top() && 
                                    globalPos.y() <= (topEdge + m_edgeThreshold);
    
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
    if (watched == m_mainWindow && event->type() == QEvent::MouseMove) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        onMouseMoved(m_mainWindow->mapToGlobal(mouseEvent->pos()));
    } else if (watched == m_mainWindow && event->type() == QEvent::WindowStateChange) {
        QWindowStateChangeEvent *stateEvent = static_cast<QWindowStateChangeEvent*>(event);
        onWindowStateChanged(stateEvent->oldState(), m_mainWindow->windowState());
    }
    
    return QObject::eventFilter(watched, event);
}
