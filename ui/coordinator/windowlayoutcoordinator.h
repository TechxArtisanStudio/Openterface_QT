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

#ifndef WINDOWLAYOUTCOORDINATOR_H
#define WINDOWLAYOUTCOORDINATOR_H

#include <QObject>
#include <QRect>
#include <QSize>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_ui_windowlayoutcoordinator)

class QMainWindow;
class QMenuBar;
class QStatusBar;
class QScreen;
class VideoPane;

/**
 * @brief Coordinates window layout, geometry calculations, and resize operations
 * 
 * This class handles all window layout-related operations including:
 * - Window resize and geometry calculations
 * - Aspect ratio maintenance
 * - Fullscreen mode management
 * - Zoom operations
 * - Video pane positioning and sizing
 * - Screen bounds checking
 */
class WindowLayoutCoordinator : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct a new Window Layout Coordinator
     * @param mainWindow Pointer to the main window
     * @param videoPane Pointer to the video pane
     * @param menuBar Pointer to the menu bar
     * @param statusBar Pointer to the status bar
     * @param parent Parent QObject
     */
    explicit WindowLayoutCoordinator(QMainWindow *mainWindow,
                                     VideoPane *videoPane,
                                     QMenuBar *menuBar,
                                     QStatusBar *statusBar,
                                     QObject *parent = nullptr);
    
    /**
     * @brief Destructor
     */
    ~WindowLayoutCoordinator();

    /**
     * @brief Perform window resize based on current settings
     * 
     * Handles aspect ratio, screen bounds, and window state
     */
    void doResize();
    
    /**
     * @brief Initialize window size based on screen dimensions
     * 
     * Sets initial window size to appropriate ratio of screen size
     */
    void checkInitSize();
    
    /**
     * @brief Toggle fullscreen mode
     * 
     * Switches between fullscreen and normal windowed mode
     */
    void fullScreen();
    
    /**
     * @brief Check if window is in fullscreen mode
     * @return bool True if fullscreen, false otherwise
     */
    bool isFullScreenMode() const;
    
    /**
     * @brief Zoom in the video pane
     * 
     * Increases video pane size by scaling factor
     */
    void zoomIn();
    
    /**
     * @brief Zoom out the video pane
     * 
     * Decreases video pane size by scaling factor
     */
    void zoomOut();
    
    /**
     * @brief Reset zoom to fit window
     * 
     * Resets video pane to fit within window bounds
     */
    void zoomReduction();
    
    /**
     * @brief Calculate and update video position
     * 
     * Centers video pane within window based on current dimensions
     */
    void calculateVideoPosition();
    
    /**
     * @brief Animate video pane for visual feedback
     * 
     * Performs animation when video settings change
     */
    void animateVideoPane();
    
    /**
     * @brief Center video pane within window
     * @param videoWidth Video pane width
     * @param videoHeight Video pane height
     * @param windowWidth Window width
     * @param windowHeight Window height
     */
    void centerVideoPane(int videoWidth, int videoHeight, int windowWidth, int windowHeight);
    
    /**
     * @brief Get current system scale factor
     * @return double The scale factor from current screen
     */
    double getSystemScaleFactor() const { return m_systemScaleFactor; }
    
    /**
     * @brief Get video dimensions
     * @return QSize Current video width and height
     */
    QSize getVideoDimensions() const { return QSize(m_videoWidth, m_videoHeight); }

signals:
    /**
     * @brief Emitted when window layout changes
     * @param size New window size
     */
    void layoutChanged(const QSize &size);
    
    /**
     * @brief Emitted when fullscreen state changes
     * @param isFullscreen True if entering fullscreen, false if exiting
     */
    void fullscreenChanged(bool isFullscreen);
    
    /**
     * @brief Emitted when zoom level changes
     * @param zoomFactor Current zoom factor
     */
    void zoomChanged(double zoomFactor);

private:
    /**
     * @brief Handle resize when window exceeds screen bounds
     * @param currentWidth Current window width (may be modified)
     * @param currentHeight Current window height (may be modified)
     * @param availableWidth Available screen width
     * @param availableHeight Available screen height
     * @param maxContentHeight Maximum content height
     * @param menuBarHeight Menu bar height
     * @param statusBarHeight Status bar height
     * @param aspectRatio Target aspect ratio
     */
    void handleScreenBoundsResize(int &currentWidth, int &currentHeight,
                                  int availableWidth, int availableHeight,
                                  int maxContentHeight, int menuBarHeight,
                                  int statusBarHeight, double aspectRatio);
    
    /**
     * @brief Handle resize based on aspect ratio
     * @param currentWidth Current window width
     * @param currentHeight Current window height
     * @param menuBarHeight Menu bar height
     * @param statusBarHeight Status bar height
     * @param aspectRatio Target aspect ratio
     * @param captureAspectRatio Capture device aspect ratio
     */
    void handleAspectRatioResize(int currentWidth, int currentHeight,
                                int menuBarHeight, int statusBarHeight,
                                double aspectRatio, double captureAspectRatio);

    // Member variables
    QMainWindow *m_mainWindow;          ///< Pointer to main window (not owned)
    VideoPane *m_videoPane;             ///< Pointer to video pane (not owned)
    QMenuBar *m_menuBar;                ///< Pointer to menu bar (not owned)
    QStatusBar *m_statusBar;            ///< Pointer to status bar (not owned)
    
    double m_systemScaleFactor;         ///< System DPI scale factor
    int m_videoWidth;                   ///< Current video width
    int m_videoHeight;                  ///< Current video height
    bool m_fullScreenState;             ///< Current fullscreen state
    Qt::WindowStates m_oldWindowState;  ///< Window state before fullscreen
    
    // Edge scrolling for zoom
    const int m_edgeThreshold = 50;     ///< Edge threshold for scrolling
    const int m_edgeDuration = 125;     ///< Edge check duration in ms
    const int m_maxScrollSpeed = 50;    ///< Maximum scroll speed
};

#endif // WINDOWLAYOUTCOORDINATOR_H
