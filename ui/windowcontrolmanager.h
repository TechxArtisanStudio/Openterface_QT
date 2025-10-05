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

#ifndef WINDOWCONTROLMANAGER_H
#define WINDOWCONTROLMANAGER_H

#include <QObject>
#include <QTimer>
#include <QMainWindow>
#include <QToolBar>
#include <QPropertyAnimation>

/**
 * @brief The WindowControlManager class
 * 
 * A generic manager for controlling window behaviors such as:
 * - Auto-hiding toolbar when maximized
 * - Showing toolbar on mouse hover at top edge
 * - Auto-hiding toolbar after inactivity
 * - Managing window state transitions (fullscreen, maximized, normal)
 * 
 * This class provides a reusable and maintainable solution for window control behaviors.
 */
class WindowControlManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param mainWindow The main window to control
     * @param toolbar The toolbar to manage (auto-hide behavior)
     * @param parent Parent object
     */
    explicit WindowControlManager(QMainWindow *mainWindow, QToolBar *toolbar = nullptr, QObject *parent = nullptr);
    ~WindowControlManager();

    // Configuration methods
    void setToolbar(QToolBar *toolbar);
    void setAutoHideEnabled(bool enabled);
    void setAutoHideDelay(int milliseconds);
    void setEdgeDetectionThreshold(int pixels);
    void setAnimationDuration(int milliseconds);
    
    // State queries
    bool isAutoHideEnabled() const;
    bool isToolbarVisible() const;
    bool isMaximized() const;
    bool isFullScreen() const;
    
    // Manual control
    void showToolbar();
    void hideToolbar();
    void toggleToolbar();

public slots:
    // Window state management
    void onWindowMaximized();
    void onWindowRestored();
    void onWindowFullScreen();
    void onWindowStateChanged(Qt::WindowStates oldState, Qt::WindowStates newState);
    
    // Mouse tracking
    void onMouseMoved(const QPoint &globalPos);
    void checkMousePosition();

signals:
    void toolbarVisibilityChanged(bool visible);
    void autoHideTriggered();
    void edgeHoverDetected();

private:
    void setupConnections();
    void startAutoHideTimer();
    void stopAutoHideTimer();
    void animateToolbarShow();
    void animateToolbarHide();
    bool isMouseAtTopEdge(const QPoint &globalPos);
    void installEventFilterOnWindow();
    void removeEventFilterFromWindow();

private:
    QMainWindow *m_mainWindow;
    QToolBar *m_toolbar;
    
    // Timers
    QTimer *m_autoHideTimer;          // Timer for auto-hiding toolbar after delay
    QTimer *m_edgeCheckTimer;         // Timer for checking mouse position at edges
    
    // Configuration
    bool m_autoHideEnabled;           // Whether auto-hide is enabled
    int m_autoHideDelay;              // Delay before auto-hiding (milliseconds)
    int m_edgeThreshold;              // Pixel distance from edge to trigger show
    int m_animationDuration;          // Animation duration (milliseconds)
    
    // State tracking
    bool m_toolbarAutoHidden;         // Whether toolbar is currently auto-hidden
    bool m_isMaximized;               // Whether window is maximized
    bool m_isFullScreen;              // Whether window is in fullscreen mode
    bool m_mouseAtTopEdge;            // Whether mouse is currently at top edge
    QPoint m_lastMousePos;            // Last recorded mouse position
    
    // Prevent event filter conflicts
    bool m_eventFilterInstalled;
    
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif // WINDOWCONTROLMANAGER_H
