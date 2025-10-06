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

#include "windowlayoutcoordinator.h"
#include "ui/videopane.h"
#include "ui/globalsetting.h"
#include "ui/toolbar/toolbarmanager.h"
#include "global.h"
#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QScreen>
#include <QApplication>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>

Q_LOGGING_CATEGORY(log_ui_windowlayoutcoordinator, "opf.ui.windowlayoutcoordinator")

WindowLayoutCoordinator::WindowLayoutCoordinator(QMainWindow *mainWindow,
                                                 VideoPane *videoPane,
                                                 QMenuBar *menuBar,
                                                 QStatusBar *statusBar,
                                                 QObject *parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
    , m_videoPane(videoPane)
    , m_menuBar(menuBar)
    , m_statusBar(statusBar)
    , m_toolbarManager(nullptr)
    , m_systemScaleFactor(1.0)
    , m_videoWidth(1920)
    , m_videoHeight(1080)
    , m_fullScreenState(false)
    , m_oldWindowState(Qt::WindowNoState)
{
    qCDebug(log_ui_windowlayoutcoordinator) << "WindowLayoutCoordinator created";
}

WindowLayoutCoordinator::~WindowLayoutCoordinator()
{
    qCDebug(log_ui_windowlayoutcoordinator) << "WindowLayoutCoordinator destroyed";
}

void WindowLayoutCoordinator::setToolbarManager(ToolbarManager *toolbarManager)
{
    m_toolbarManager = toolbarManager;
    qCDebug(log_ui_windowlayoutcoordinator) << "ToolbarManager set for animation coordination";
}

void WindowLayoutCoordinator::doResize()
{
    if (!m_mainWindow || !m_videoPane) {
        qCWarning(log_ui_windowlayoutcoordinator) << "Main window or video pane not initialized";
        return;
    }
    
    // Log window state
    if (m_mainWindow->windowState() & Qt::WindowMaximized) {
        qCDebug(log_ui_windowlayoutcoordinator) << "Window is maximized.";
    } else {
        qCDebug(log_ui_windowlayoutcoordinator) << "Window is normal.";
    }

    // Get screen and system information
    QScreen *currentScreen = m_mainWindow->screen();
    QRect availableGeometry = currentScreen->availableGeometry();
    m_systemScaleFactor = currentScreen->devicePixelRatio();
    
    // Calculate aspect ratios
    double captureAspectRatio = 1.0;
    if (GlobalVar::instance().getCaptureWidth() && GlobalVar::instance().getCaptureHeight()) {
        m_videoWidth = GlobalVar::instance().getCaptureWidth();
        m_videoHeight = GlobalVar::instance().getCaptureHeight();
        captureAspectRatio = static_cast<double>(m_videoWidth) / m_videoHeight;
    }
    double aspectRatio = GlobalSetting::instance().getScreenRatio();
    
    // Get dimensions
    int availableWidth = availableGeometry.width();
    int availableHeight = availableGeometry.height();
    int currentWidth = m_mainWindow->width();
    int currentHeight = m_mainWindow->height();
    
    // Calculate UI element heights
    int titleBarHeight = m_mainWindow->frameGeometry().height() - m_mainWindow->geometry().height();
    int menuBarHeight = m_menuBar->height();
    int statusBarHeight = m_statusBar->height();
    int maxContentHeight = availableHeight - titleBarHeight - menuBarHeight - statusBarHeight;
    
    // Check if resize is needed due to screen bounds
    bool needResize = (currentWidth >= availableWidth || currentHeight >= availableHeight);
    
    if (needResize) {
        qCDebug(log_ui_windowlayoutcoordinator) << "Need resize due to screen bounds.";
        handleScreenBoundsResize(currentWidth, currentHeight, availableWidth, availableHeight, 
                                maxContentHeight, menuBarHeight, statusBarHeight, aspectRatio);
    } else {
        qCDebug(log_ui_windowlayoutcoordinator) << "No resize needed.";
        handleAspectRatioResize(currentWidth, currentHeight, menuBarHeight, statusBarHeight, 
                               aspectRatio, captureAspectRatio);
    }
    
    // Update global state
    GlobalVar::instance().setWinWidth(m_mainWindow->width());
    GlobalVar::instance().setWinHeight(m_mainWindow->height());
    
    emit layoutChanged(QSize(m_mainWindow->width(), m_mainWindow->height()));
}

void WindowLayoutCoordinator::handleScreenBoundsResize(int &currentWidth, int &currentHeight,
                                                       int availableWidth, int availableHeight,
                                                       int maxContentHeight, int menuBarHeight,
                                                       int statusBarHeight, double aspectRatio)
{
    // Adjust size while maintaining aspect ratio
    if (currentWidth >= availableWidth) {
        currentWidth = availableWidth;
    }
    if (currentHeight >= maxContentHeight) {
        currentHeight = std::min(maxContentHeight + menuBarHeight + statusBarHeight, availableHeight);
    }

    int newVideoHeight = std::min(currentHeight - menuBarHeight - statusBarHeight, maxContentHeight);
    int newVideoWidth = static_cast<int>(newVideoHeight * aspectRatio);

    if (currentWidth < newVideoWidth) {
        // If video width is larger than the window's width, adjust based on width
        newVideoWidth = currentWidth;
        newVideoHeight = static_cast<int>(newVideoWidth / aspectRatio);
    }

    // Apply changes to video pane
    m_videoPane->resize(newVideoWidth, newVideoHeight);
    
    // Resize main window if necessary
    if (currentWidth != availableWidth && currentHeight != availableHeight) {
        qCDebug(log_ui_windowlayoutcoordinator) << "Resize to Width:" << currentWidth 
                                                << "Height:" << currentHeight 
                                                << "due to exceeding screen bounds.";
        qCDebug(log_ui_windowlayoutcoordinator) << "Available Width:" << availableWidth 
                                                << "Height:" << availableHeight;
        m_mainWindow->resize(currentWidth, currentHeight);
    }
}

void WindowLayoutCoordinator::handleAspectRatioResize(int currentWidth, int currentHeight,
                                                      int menuBarHeight, int statusBarHeight,
                                                      double aspectRatio, double captureAspectRatio)
{
    // When within screen bounds, adjust height according to width and aspect ratio
    int contentHeight = static_cast<int>(currentWidth / aspectRatio) + menuBarHeight + statusBarHeight;
    int adjustedContentHeight = contentHeight - menuBarHeight - statusBarHeight;
    
    // Check if window is maximized
    bool isMaximized = (m_mainWindow->windowState() & Qt::WindowMaximized);
    
    if (isMaximized) {
        // For maximized windows, use the full available space
        adjustedContentHeight = currentHeight - menuBarHeight - statusBarHeight;
        
        // Calculate video pane size to fill the available area while maintaining aspect ratio
        int availableWidth = currentWidth;
        int availableHeight = adjustedContentHeight;
        
        // Scale video pane to fit the available area
        double scaleX = static_cast<double>(availableWidth) / (captureAspectRatio * availableHeight);
        double scaleY = 1.0;
        
        int videoWidth, videoHeight;
        if (scaleX <= 1.0) {
            // Height-constrained: video height = available height, width scaled accordingly
            videoHeight = availableHeight;
            videoWidth = static_cast<int>(videoHeight * captureAspectRatio);
        } else {
            // Width-constrained: video width = available width, height scaled accordingly
            videoWidth = availableWidth;
            videoHeight = static_cast<int>(videoWidth / captureAspectRatio);
        }
        
        m_videoPane->resize(videoWidth, videoHeight);
        qCDebug(log_ui_windowlayoutcoordinator) << "Maximized window - VideoPane resized to:" 
                                                << videoWidth << "x" << videoHeight;
        
    } else if (aspectRatio < 1.0) {
        // Portrait orientation
        int newWidth = static_cast<int>(currentHeight * aspectRatio);
        adjustedContentHeight = currentHeight - menuBarHeight - statusBarHeight;
        int contentWidth = static_cast<int>(adjustedContentHeight * captureAspectRatio);
        
        m_videoPane->resize(contentWidth, adjustedContentHeight);
        m_mainWindow->setMinimumSize(100, 500);
        
        qCDebug(log_ui_windowlayoutcoordinator) << "Resize to Width:" << newWidth 
                                                << "Height:" << currentHeight 
                                                << "due to aspect ratio < 1.0";
        m_mainWindow->resize(newWidth, currentHeight);
    } else {
        // Landscape orientation
        m_videoPane->resize(currentWidth, adjustedContentHeight);
        
        qCDebug(log_ui_windowlayoutcoordinator) << "Resize to Width:" << currentWidth 
                                                << "Height:" << contentHeight 
                                                << "due to aspect ratio >= 1.0";
        m_mainWindow->resize(currentWidth, contentHeight);
    }
}

void WindowLayoutCoordinator::checkInitSize()
{
    if (!m_mainWindow) {
        qCWarning(log_ui_windowlayoutcoordinator) << "Main window not initialized";
        return;
    }
    
    QScreen *currentScreen = m_mainWindow->screen();
    m_systemScaleFactor = currentScreen->devicePixelRatio();
    
    // Get screen geometry
    QRect screenGeometry = currentScreen->geometry();
    int screenWidth = screenGeometry.width();
    int screenHeight = screenGeometry.height();
    int titleBarHeight = m_mainWindow->frameGeometry().height() - m_mainWindow->geometry().height();
    int menuBarHeight = m_menuBar->height();
    int statusBarHeight = m_statusBar->height();
    int height = titleBarHeight + menuBarHeight + statusBarHeight;

    int windowHeight = int(screenHeight / 3 * 2);
    int windowWidth = int(windowHeight / 9 * 16) + height;

    // Set window size to 2/3 of screen size with 16:9 aspect ratio
    m_mainWindow->resize(windowWidth, windowHeight);
    
    qCDebug(log_ui_windowlayoutcoordinator) << "Initial window size:" << windowWidth << "x" << windowHeight;
}

void WindowLayoutCoordinator::fullScreen()
{
    qCDebug(log_ui_windowlayoutcoordinator) << "=== FULLSCREEN TOGGLE START ===";
    if (!m_mainWindow || !m_videoPane) {
        qCWarning(log_ui_windowlayoutcoordinator) << "Main window or video pane not initialized";
        return;
    }

    // Log current window state in detail
    Qt::WindowStates windowState = m_mainWindow->windowState();
    WId originalWinId = m_mainWindow->winId();
    qCDebug(log_ui_windowlayoutcoordinator) << "Window state flags:" << windowState;
    qCDebug(log_ui_windowlayoutcoordinator) << "  - winId:" << originalWinId;
    qCDebug(log_ui_windowlayoutcoordinator) << "  - isVisible():" << m_mainWindow->isVisible();
    qCDebug(log_ui_windowlayoutcoordinator) << "  - isHidden():" << m_mainWindow->isHidden();
    qCDebug(log_ui_windowlayoutcoordinator) << "  - isMinimized():" << (windowState & Qt::WindowMinimized);
    qCDebug(log_ui_windowlayoutcoordinator) << "  - isMaximized():" << (windowState & Qt::WindowMaximized);
    qCDebug(log_ui_windowlayoutcoordinator) << "  - isFullScreen():" << (windowState & Qt::WindowFullScreen);
    qCDebug(log_ui_windowlayoutcoordinator) << "  - isActiveWindow():" << m_mainWindow->isActiveWindow();
    qCDebug(log_ui_windowlayoutcoordinator) << "Window geometry:" << m_mainWindow->geometry();
    
    // CRITICAL FIX: Check for corrupted window state (both minimized AND maximized)
    // This can happen due to window manager issues or event filter conflicts
    bool hasMinimizedFlag = (windowState & Qt::WindowMinimized) != 0;
    bool hasMaximizedFlag = (windowState & Qt::WindowMaximized) != 0;
    
    qCDebug(log_ui_windowlayoutcoordinator) << "Window state analysis:";
    qCDebug(log_ui_windowlayoutcoordinator) << "  Has WindowMinimized flag:" << hasMinimizedFlag;
    qCDebug(log_ui_windowlayoutcoordinator) << "  Has WindowMaximized flag:" << hasMaximizedFlag;
    
    if (hasMinimizedFlag && hasMaximizedFlag) {
        qCCritical(log_ui_windowlayoutcoordinator) << "CRITICAL: DETECTED CORRUPTED WINDOW STATE!";
        qCCritical(log_ui_windowlayoutcoordinator) << "  Both WindowMinimized and WindowMaximized flags are set simultaneously";
        qCCritical(log_ui_windowlayoutcoordinator) << "  This indicates a serious window manager or event processing issue";
        qCCritical(log_ui_windowlayoutcoordinator) << "  Window ID:" << originalWinId << "(decimal)" << Qt::hex << originalWinId << "(hex)";
        
        // CRITICAL: Check if window geometry is corrupted (indicates bad pointer/memory corruption)
        QRect geom = m_mainWindow->geometry();
        bool geometryCorrupted = (geom.x() < -100000 || geom.y() < -100000 || 
                                 geom.width() > 100000 || geom.height() > 100000);
        
        if (geometryCorrupted) {
            qCCritical(log_ui_windowlayoutcoordinator) << "  Window geometry is CORRUPTED:" << geom;
            qCCritical(log_ui_windowlayoutcoordinator) << "  This indicates MEMORY CORRUPTION or invalid window pointer!";
            qCCritical(log_ui_windowlayoutcoordinator) << "  ABORTING fullscreen operation - cannot safely operate on corrupted window";
            qCCritical(log_ui_windowlayoutcoordinator) << "  This is likely caused by window manager issues or premature window destruction";
            return; // ABORT immediately - do NOT touch the window!
        }
        
        qCCritical(log_ui_windowlayoutcoordinator) << "  Window geometry:" << geom;
        
        // IMPORTANT: Do NOT call setWindowFlags() here! It will destroy and recreate the window,
        // changing the window ID and making the corruption worse. 
        qCWarning(log_ui_windowlayoutcoordinator) << "Attempting gentle recovery without recreating window...";
        
        // Validate window pointer before attempting recovery
        if (!m_mainWindow || m_mainWindow->testAttribute(Qt::WA_WState_Created) == false) {
            qCCritical(log_ui_windowlayoutcoordinator) << "CRITICAL: Window is not properly created, aborting recovery";
            return;
        }
        
        // Try to clear the corrupted state by explicitly setting to normal state
        // Wrap in try-catch equivalent (Qt doesn't throw, but check for crashes)
        qCWarning(log_ui_windowlayoutcoordinator) << "Setting window state to WindowNoState...";
        
        // Use a safer approach: check if the operation succeeds
        Qt::WindowStates oldState = m_mainWindow->windowState();
        m_mainWindow->setWindowState(Qt::WindowNoState);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
        
        // Re-read the state after recovery attempt
        windowState = m_mainWindow->windowState();
        WId afterRecoveryWinId = m_mainWindow->winId();
        qCDebug(log_ui_windowlayoutcoordinator) << "Window state after recovery:" << windowState;
        qCDebug(log_ui_windowlayoutcoordinator) << "Window ID after recovery:" << afterRecoveryWinId;
        
        if (afterRecoveryWinId != originalWinId) {
            qCCritical(log_ui_windowlayoutcoordinator) << "ERROR: Window ID changed during recovery!";
            qCCritical(log_ui_windowlayoutcoordinator) << "  Before:" << originalWinId << "After:" << afterRecoveryWinId;
        }
        
        // Update flags after recovery
        hasMinimizedFlag = (windowState & Qt::WindowMinimized) != 0;
        hasMaximizedFlag = (windowState & Qt::WindowMaximized) != 0;
        qCDebug(log_ui_windowlayoutcoordinator) << "After recovery - minimized:" << hasMinimizedFlag << " maximized:" << hasMaximizedFlag;
        
        // If still corrupted, abort fullscreen operation
        if (hasMinimizedFlag || !m_mainWindow->isVisible()) {
            qCCritical(log_ui_windowlayoutcoordinator) << "ABORT: Failed to recover from corrupted state, blocking fullscreen operation";
            return;
        }
    }
    
    // Check if window is being destroyed or minimized (but allow invisible during state transitions)
    if (hasMinimizedFlag) {
        qCWarning(log_ui_windowlayoutcoordinator) << "BLOCKED: Cannot toggle fullscreen - window is minimized";
        return;
    }
    
    if (m_mainWindow->testAttribute(Qt::WA_DeleteOnClose) && m_mainWindow->isHidden()) {
        qCWarning(log_ui_windowlayoutcoordinator) << "BLOCKED: Cannot toggle fullscreen - window is being destroyed";
        return;
    }
    
    // Get screen using safe method (not widget->screen() which can crash during transitions)
    QRect windowGeometry = m_mainWindow->geometry();
    QPoint centerPoint = windowGeometry.center();
    QScreen *currentScreen = QGuiApplication::screenAt(centerPoint);
    if (!currentScreen) {
        qCDebug(log_ui_windowlayoutcoordinator) << "Using primary screen (position-based lookup failed)";
        currentScreen = QGuiApplication::primaryScreen();
    }
    if (!currentScreen) {
        qCCritical(log_ui_windowlayoutcoordinator) << "CRITICAL: No screen available!";
        return;
    }
    
    if (!m_menuBar || !m_statusBar) {
        qCCritical(log_ui_windowlayoutcoordinator) << "CRITICAL: MenuBar or StatusBar is null!";
        return;
    }
    
    // Update video dimensions from GlobalVar
    int captureWidth = GlobalVar::instance().getCaptureWidth();
    int captureHeight = GlobalVar::instance().getCaptureHeight();
    
    if (captureWidth > 0 && captureHeight > 0) {
        m_videoWidth = captureWidth;
        m_videoHeight = captureHeight;
        qCDebug(log_ui_windowlayoutcoordinator) << "Video dimensions:" << m_videoWidth << "x" << m_videoHeight;
    } else {
        qCDebug(log_ui_windowlayoutcoordinator) << "Using default video dimensions: 1920x1080";
        m_videoWidth = 1920;
        m_videoHeight = 1080;
    }
    
    // Validate dimensions before division
    if (m_videoWidth <= 0 || m_videoHeight <= 0) {
        qCWarning(log_ui_windowlayoutcoordinator) << "Invalid video dimensions, using defaults";
        m_videoWidth = 1920;
        m_videoHeight = 1080;
    }
    
    qreal aspect_ratio = static_cast<qreal>(m_videoWidth) / m_videoHeight;
    QRect screenGeometry = currentScreen->geometry();
    int menuBarHeight = m_menuBar->height();
    int videoAvailibleHeight = screenGeometry.height() - menuBarHeight;
    int videoAvailibleWidth = videoAvailibleHeight * aspect_ratio;
    int horizontalOffset = (screenGeometry.width() - videoAvailibleWidth) / 2;
    
    qCDebug(log_ui_windowlayoutcoordinator) << "Screen:" << screenGeometry 
                                            << "Video:" << videoAvailibleWidth << "x" << videoAvailibleHeight;
    
    if (!isFullScreenMode()) {
        qCDebug(log_ui_windowlayoutcoordinator) << "Entering fullscreen mode...";
        qCDebug(log_ui_windowlayoutcoordinator) << "  Step 1: Hiding status bar";
        m_statusBar->hide();
        
        qCDebug(log_ui_windowlayoutcoordinator) << "  Step 2: Resizing video pane to:" << videoAvailibleWidth << "x" << videoAvailibleHeight;
        m_videoPane->resize(videoAvailibleWidth, videoAvailibleHeight);
        
        qCDebug(log_ui_windowlayoutcoordinator) << "  Step 3: Setting fullscreen state flag";
        m_fullScreenState = true;
        
        qCDebug(log_ui_windowlayoutcoordinator) << "  Step 4: Calling showFullScreen() - window state before:" << m_mainWindow->windowState();
        m_mainWindow->showFullScreen();
        qCDebug(log_ui_windowlayoutcoordinator) << "  Step 4: showFullScreen() completed - window state after:" << m_mainWindow->windowState();
        qCDebug(log_ui_windowlayoutcoordinator) << "  Window isVisible():" << m_mainWindow->isVisible();
        
        qCDebug(log_ui_windowlayoutcoordinator) << "  Step 5: Moving video pane to offset:" << horizontalOffset;
        m_videoPane->move(horizontalOffset, m_videoPane->y());
        
        qCDebug(log_ui_windowlayoutcoordinator) << "  Step 6: Emitting fullscreenChanged(true)";
        emit fullscreenChanged(true);
        qCDebug(log_ui_windowlayoutcoordinator) << "=== FULLSCREEN TOGGLE END (entered fullscreen) ===";
    } else {
        qCDebug(log_ui_windowlayoutcoordinator) << "Exiting fullscreen mode...";
        qCDebug(log_ui_windowlayoutcoordinator) << "  Calling showNormal()";
        m_mainWindow->setWindowFlags(Qt::Window);
        m_mainWindow->showNormal();
        qCDebug(log_ui_windowlayoutcoordinator) << "  Showing status bar";
        m_statusBar->show();
        m_fullScreenState = false;
        
        emit fullscreenChanged(false);
        qCDebug(log_ui_windowlayoutcoordinator) << "=== FULLSCREEN TOGGLE END (exited fullscreen) ===";
    }
}

bool WindowLayoutCoordinator::isFullScreenMode() const
{
    return m_fullScreenState;
}

void WindowLayoutCoordinator::zoomIn()
{
    if (!m_videoPane) {
        qCWarning(log_ui_windowlayoutcoordinator) << "Video pane not initialized";
        return;
    }
    
    // Use VideoPane's built-in zoom functionality
    m_videoPane->zoomIn(1.1);
    
    qCDebug(log_ui_windowlayoutcoordinator) << "Zoom in applied";
    emit zoomChanged(1.1);
}

void WindowLayoutCoordinator::zoomOut()
{
    if (!m_videoPane || !m_mainWindow) {
        qCWarning(log_ui_windowlayoutcoordinator) << "Video pane or main window not initialized";
        return;
    }
    
    if (m_videoPane->width() != m_mainWindow->width()) {
        // Use VideoPane's built-in zoom functionality
        m_videoPane->zoomOut(0.9);
        
        qCDebug(log_ui_windowlayoutcoordinator) << "Zoom out applied";
        emit zoomChanged(0.9);
    }
}

void WindowLayoutCoordinator::zoomReduction()
{
    if (!m_videoPane) {
        qCWarning(log_ui_windowlayoutcoordinator) << "Video pane not initialized";
        return;
    }
    
    // Use VideoPane's fit to window functionality
    m_videoPane->fitToWindow();
    
    qCDebug(log_ui_windowlayoutcoordinator) << "Zoom reset to fit window";
    emit zoomChanged(1.0);
}

void WindowLayoutCoordinator::calculateVideoPosition()
{
    if (!m_mainWindow || !m_mainWindow->screen()) {
        qCWarning(log_ui_windowlayoutcoordinator) << "Main window or screen not initialized";
        return;
    }
    
    qCDebug(log_ui_windowlayoutcoordinator) << "Calculate video position...";
    
    // Perform resize to update video pane
    doResize();
    
    // Center window on screen
    QScreen *screen = m_mainWindow->screen();
    QRect availableGeometry = screen->availableGeometry();
    int x = (availableGeometry.width() - m_mainWindow->width()) / 2;
    int y = (availableGeometry.height() - m_mainWindow->height()) / 2;
    m_mainWindow->move(x, y);
    
    qCDebug(log_ui_windowlayoutcoordinator) << "Window centered at:" << x << "," << y;
}

void WindowLayoutCoordinator::animateVideoPane()
{
    // Safety check: Don't animate if window is being destroyed
    if (!m_videoPane || !m_mainWindow || !m_mainWindow->isVisible() || 
        m_mainWindow->testAttribute(Qt::WA_DeleteOnClose)) {
        m_mainWindow->setUpdatesEnabled(true);
        m_mainWindow->blockSignals(false);
        qCDebug(log_ui_windowlayoutcoordinator) << "Animation skipped - window not ready";
        return;
    }

    // CRITICAL FIX: Validate video dimensions before using them
    if (m_videoWidth <= 0 || m_videoHeight <= 0) {
        qCWarning(log_ui_windowlayoutcoordinator) << "Invalid video dimensions in animateVideoPane:" 
                                                   << m_videoWidth << "x" << m_videoHeight;
        // Try to update from GlobalVar
        if (GlobalVar::instance().getCaptureWidth() > 0 && GlobalVar::instance().getCaptureHeight() > 0) {
            m_videoWidth = GlobalVar::instance().getCaptureWidth();
            m_videoHeight = GlobalVar::instance().getCaptureHeight();
        } else {
            m_videoWidth = 1920;
            m_videoHeight = 1080;
        }
        qCDebug(log_ui_windowlayoutcoordinator) << "Reset to:" << m_videoWidth << "x" << m_videoHeight;
    }

    // Get toolbar visibility and window state
    bool isToolbarVisible = m_toolbarManager && m_toolbarManager->getToolbar() && 
                           m_toolbarManager->getToolbar()->isVisible();
    bool isMaximized = (m_mainWindow->windowState() & Qt::WindowMaximized);

    // Calculate content height based on toolbar visibility
    int contentHeight;
    if (!isFullScreenMode()) {
        contentHeight = m_mainWindow->height() - m_statusBar->height() - m_menuBar->height();
    } else {
        contentHeight = m_mainWindow->height() - m_menuBar->height();
    }

    int contentWidth;
    double aspect_ratio = static_cast<double>(m_videoWidth) / m_videoHeight;
    
    if (isMaximized) {
        // For maximized windows, use the full window width and calculated height
        contentWidth = m_mainWindow->width();
        if (isToolbarVisible && m_toolbarManager && m_toolbarManager->getToolbar()) {
            contentHeight -= m_toolbarManager->getToolbar()->height();
        }
        // Don't constrain by aspect ratio for maximized windows - let VideoPane handle it
        qCDebug(log_ui_windowlayoutcoordinator) << "Maximized window - contentWidth:" 
                                                << contentWidth << "contentHeight:" << contentHeight;
    } else {
        // Normal window behavior
        if (isToolbarVisible && m_toolbarManager && m_toolbarManager->getToolbar()) {
            contentHeight -= m_toolbarManager->getToolbar()->height();
            contentWidth = static_cast<int>(contentHeight * aspect_ratio);
            qCDebug(log_ui_windowlayoutcoordinator) << "toolbarHeight" 
                                                    << m_toolbarManager->getToolbar()->height() 
                                                    << "content height" << contentHeight 
                                                    << "content width" << contentWidth;
        } else {
            if (!isFullScreenMode()) {
                contentHeight = m_mainWindow->height() - m_statusBar->height() - m_menuBar->height();
            } else {
                contentHeight = m_mainWindow->height() - m_menuBar->height();
            }
            contentWidth = static_cast<int>(contentHeight * aspect_ratio);
        }
    }

    // Resize the video pane
    m_videoPane->resize(contentWidth, contentHeight);

    // Position the video pane (center horizontally if window is wider than video pane)
    if (m_mainWindow->width() > m_videoPane->width()) {
        // Calculate new position
        int horizontalOffset = (m_mainWindow->width() - m_videoPane->width()) / 2;
        
        // Safety check: Only create animation if videoPane is still valid and window is visible
        if (m_videoPane && m_mainWindow->isVisible() && !m_mainWindow->testAttribute(Qt::WA_DeleteOnClose)) {
            // Animate the videoPane position
            QPropertyAnimation *videoAnimation = new QPropertyAnimation(m_videoPane, "pos");
            videoAnimation->setDuration(150);
            videoAnimation->setStartValue(m_videoPane->pos());
            videoAnimation->setEndValue(QPoint(horizontalOffset, m_videoPane->y()));
            videoAnimation->setEasingCurve(QEasingCurve::OutCubic);

            // Create animation group with just video animation
            QParallelAnimationGroup *group = new QParallelAnimationGroup(m_mainWindow);
            group->addAnimation(videoAnimation);
            
            // Cleanup after animation
            connect(group, &QParallelAnimationGroup::finished, m_mainWindow, [this]() {
                if (m_mainWindow && m_mainWindow->isVisible() && 
                    !m_mainWindow->testAttribute(Qt::WA_DeleteOnClose)) {
                    m_mainWindow->setUpdatesEnabled(true);
                    m_mainWindow->blockSignals(false);
                    m_mainWindow->update();
                }
            });
            
            group->start(QAbstractAnimation::DeleteWhenStopped);
            qCDebug(log_ui_windowlayoutcoordinator) << "Video pane animation started to offset:" 
                                                    << horizontalOffset;
        } else {
            // If animation can't be created safely, just move immediately
            if (m_videoPane) {
                m_videoPane->move(horizontalOffset, m_videoPane->y());
            }
            m_mainWindow->setUpdatesEnabled(true);
            m_mainWindow->blockSignals(false);
            qCDebug(log_ui_windowlayoutcoordinator) << "Video pane moved immediately (no animation)";
        }
    } else {
        // VideoPane fills the window width, position at x=0
        if (m_videoPane) {
            m_videoPane->move(0, m_videoPane->y());
        }
        m_mainWindow->setUpdatesEnabled(true);
        m_mainWindow->blockSignals(false);
        m_mainWindow->update();
        qCDebug(log_ui_windowlayoutcoordinator) << "Video pane positioned at x=0 (fills width)";
    }
}

void WindowLayoutCoordinator::centerVideoPane(int videoWidth, int videoHeight, 
                                              int windowWidth, int windowHeight)
{
    if (!m_videoPane) {
        qCWarning(log_ui_windowlayoutcoordinator) << "Video pane not initialized";
        return;
    }
    
    int horizontalOffset = (windowWidth - videoWidth) / 2;
    int verticalOffset = (windowHeight - videoHeight) / 2;
    
    m_videoPane->move(horizontalOffset, verticalOffset);
    
    qCDebug(log_ui_windowlayoutcoordinator) << "Video pane centered at:" 
                                            << horizontalOffset << "," << verticalOffset;
}
