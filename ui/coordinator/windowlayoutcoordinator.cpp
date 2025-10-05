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
#include "global.h"
#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QScreen>
#include <QApplication>
#include <QDebug>

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
    if (!m_mainWindow || !m_videoPane) {
        qCWarning(log_ui_windowlayoutcoordinator) << "Main window or video pane not initialized";
        return;
    }
    
    qreal aspect_ratio = static_cast<qreal>(m_videoWidth) / m_videoHeight;
    QScreen *currentScreen = m_mainWindow->screen();
    QRect screenGeometry = currentScreen->geometry();
    int videoAvailibleHeight = screenGeometry.height() - m_menuBar->height();
    int videoAvailibleWidth = videoAvailibleHeight * aspect_ratio;
    int horizontalOffset = (screenGeometry.width() - videoAvailibleWidth) / 2;
    
    if (!isFullScreenMode()) {
        m_statusBar->hide();
        
        // Resize and position the videoPane
        m_videoPane->resize(videoAvailibleWidth, videoAvailibleHeight);
        qCDebug(log_ui_windowlayoutcoordinator) << "Resize to Width" << videoAvailibleWidth 
                                                << "\tHeight:" << videoAvailibleHeight;
        
        // Move the videoPane to the center
        m_fullScreenState = true;
        m_mainWindow->showFullScreen();
        qCDebug(log_ui_windowlayoutcoordinator) << "offset:" << horizontalOffset;
        m_videoPane->move(horizontalOffset, m_videoPane->y());
        
        emit fullscreenChanged(true);
    } else {
        m_mainWindow->showNormal();
        m_statusBar->show();
        m_fullScreenState = false;
        
        emit fullscreenChanged(false);
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
    // This method is intentionally left as a stub for Phase 3
    // The full animation logic is complex and involves ToolbarManager
    // It will be implemented in a future phase when ToolbarManager is also refactored
    qCDebug(log_ui_windowlayoutcoordinator) << "animateVideoPane called (stub - full implementation pending)";
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
