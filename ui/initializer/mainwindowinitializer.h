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

#ifndef MAINWINDOWINITIALIZER_H
#define MAINWINDOWINITIALIZER_H

#include <QObject>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_ui_mainwindowinitializer)

class MainWindow;
class QStackedLayout;
class VideoPane;
class CameraManager;
class StatusBarManager;
class CornerWidgetManager;
class WindowLayoutCoordinator;
class ToolbarManager;
class WindowControlManager;
class DeviceCoordinator;
class MenuCoordinator;
class LanguageManager;
class QTimer;

namespace Ui {
    class MainWindow;
}

/**
 * @brief Initializes MainWindow components and connections
 * 
 * This class extracts the complex initialization logic from MainWindow's constructor,
 * organizing it into logical, testable sections:
 * - Layout and UI setup
 * - Signal/slot connections
 * - Camera and audio initialization
 * - Coordinator setup
 * - Event callbacks
 */
class MainWindowInitializer : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct initializer with MainWindow reference
     * @param mainWindow The MainWindow instance to initialize
     * @param parent Parent QObject
     */
    explicit MainWindowInitializer(MainWindow *mainWindow, QObject *parent = nullptr);
    
    /**
     * @brief Destructor
     */
    ~MainWindowInitializer();
    
    /**
     * @brief Perform complete initialization sequence
     * 
     * Calls all initialization methods in the correct order
     */
    void initialize();

private:
    /**
     * @brief Setup central widget and stacked layout
     * 
     * Creates central widget, help pane, video pane layout
     */
    void setupCentralWidget();
    
    /**
     * @brief Setup coordinator classes
     * 
     * Initializes Device, Menu, and WindowLayout coordinators
     */
    void setupCoordinators();
    
    /**
     * @brief Connect corner widget signals
     * 
     * Connects zoom, fullscreen, capture, paste, screensaver buttons
     */
    void connectCornerWidgetSignals();
    
    /**
     * @brief Connect device manager signals
     * 
     * Connects hotplug monitor to status bar and camera manager
     */
    void connectDeviceManagerSignals();
    
    /**
     * @brief Connect action menu signals
     * 
     * Connects menu actions for mouse, HID reset, USB switching, etc.
     */
    void connectActionSignals();
    
    /**
     * @brief Setup toolbar and window control
     * 
     * Initializes toolbar manager and window control manager
     */
    void setupToolbar();
    
    /**
     * @brief Connect camera manager signals
     * 
     * Connects camera events to status bar and video pane
     */
    void connectCameraSignals();
    
    /**
     * @brief Connect video HID signals
     * 
     * Connects resolution change and input events
     */
    void connectVideoHidSignals();
    
    /**
     * @brief Initialize camera subsystem
     * 
     * Schedules camera and audio initialization with timers
     */
    void initializeCamera();
    
    /**
     * @brief Setup script tool components
     * 
     * Initializes mouse manager, keyboard mouse, semantic analyzer, script tool
     */
    void setupScriptComponents();
    
    /**
     * @brief Setup event filters and callbacks
     * 
     * Sets up event callbacks for HostManager, VideoHid, and Qt event filter
     */
    void setupEventCallbacks();
    
    /**
     * @brief Setup keyboard shortcuts
     * 
     * Configures application-level keyboard shortcuts (Alt+F11 for fullscreen, etc.)
     */
    void setupKeyboardShortcuts();
    
    /**
     * @brief Perform final initialization steps
     * 
     * Window title, mouse timer, language connections
     */
    void finalize();

    // Member variables
    MainWindow *m_mainWindow;                ///< Reference to MainWindow (not owned)
    Ui::MainWindow *m_ui;                    ///< Reference to UI (not owned)
    
    // Component references (not owned, managed by MainWindow)
    QStackedLayout *m_stackedLayout;
    VideoPane *m_videoPane;
    CameraManager *m_cameraManager;
    StatusBarManager *m_statusBarManager;
    CornerWidgetManager *m_cornerWidgetManager;
    WindowLayoutCoordinator *m_windowLayoutCoordinator;
    ToolbarManager *m_toolbarManager;
    WindowControlManager *m_windowControlManager;
    DeviceCoordinator *m_deviceCoordinator;
    MenuCoordinator *m_menuCoordinator;
    LanguageManager *m_languageManager;
    QTimer *m_mouseEdgeTimer;
};

#endif // MAINWINDOWINITIALIZER_H
