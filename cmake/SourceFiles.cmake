# SourceFiles.cmake - Source files definitions

# Common source files for all platforms
set(COMMON_SOURCES
    global.h
    main.cpp
)

# Add Windows resource file for icon
if(WIN32)
    list(APPEND COMMON_SOURCES openterfaceQT.rc)
endif()

# Add dlopen_wrapper.c only for static builds on Linux (not needed on Windows)
if(OPENTERFACE_BUILD_STATIC AND NOT WIN32)
    list(APPEND COMMON_SOURCES dlopen_wrapper.c)
endif()

# Device management
set(DEVICE_SOURCES
    device/DeviceInfo.cpp device/DeviceInfo.h
    device/DeviceManager.cpp device/DeviceManager.h
    device/HotplugMonitor.cpp device/HotplugMonitor.h
    device/platform/AbstractPlatformDeviceManager.cpp device/platform/AbstractPlatformDeviceManager.h
    device/platform/DeviceFactory.cpp device/platform/DeviceFactory.h
    device/platform/windows/WinDeviceEnumerator.h device/platform/windows/WinDeviceEnumerator.cpp
    device/platform/windows/IDeviceEnumerator.h
)

if(WIN32)
    list(APPEND DEVICE_SOURCES
        device/platform/WindowsDeviceManager.cpp device/platform/WindowsDeviceManager.h
        device/platform/windows/WinDeviceEnumerator.cpp device/platform/windows/WinDeviceEnumerator.h
        device/platform/windows/IDeviceEnumerator.h
        device/platform/windows/discoverers/IDeviceDiscoverer.h
        device/platform/windows/discoverers/BaseDeviceDiscoverer.cpp device/platform/windows/discoverers/BaseDeviceDiscoverer.h
        device/platform/windows/discoverers/BotherDeviceDiscoverer.cpp device/platform/windows/discoverers/BotherDeviceDiscoverer.h
        device/platform/windows/discoverers/Generation3Discoverer.cpp device/platform/windows/discoverers/Generation3Discoverer.h
        device/platform/windows/discoverers/DeviceDiscoveryManager.cpp device/platform/windows/discoverers/DeviceDiscoveryManager.h
    )
endif()

# Host management
set(HOST_SOURCES
    host/HostManager.cpp host/HostManager.h
    host/audiomanager.cpp host/audiomanager.h
    host/audiothread.cpp host/audiothread.h
    host/cameramanager.cpp host/cameramanager.h
    host/usbcontrol.cpp host/usbcontrol.h
    host/multimediabackend.cpp host/multimediabackend.h
    host/imagecapturer.cpp host/imagecapturer.h
    host/backend/ffmpegbackendhandler.cpp host/backend/ffmpegbackendhandler.h
    host/backend/qtmultimediabackendhandler.cpp host/backend/qtmultimediabackendhandler.h
    host/backend/qtbackendhandler.cpp host/backend/qtbackendhandler.h
    host/backend/ffmpeg/capturethread.cpp host/backend/ffmpeg/capturethread.h
    host/backend/ffmpeg/ffmpeg_hardware_accelerator.cpp host/backend/ffmpeg/ffmpeg_hardware_accelerator.h
    host/backend/ffmpeg/ffmpeg_device_manager.cpp host/backend/ffmpeg/ffmpeg_device_manager.h
    host/backend/ffmpeg/ffmpeg_frame_processor.cpp host/backend/ffmpeg/ffmpeg_frame_processor.h
    host/backend/ffmpeg/ffmpeg_recorder.cpp host/backend/ffmpeg/ffmpeg_recorder.h
    host/backend/ffmpeg/ffmpeg_device_validator.cpp host/backend/ffmpeg/ffmpeg_device_validator.h
    host/backend/ffmpeg/ffmpeg_hotplug_handler.cpp host/backend/ffmpeg/ffmpeg_hotplug_handler.h
    host/backend/ffmpeg/ffmpeg_capture_manager.cpp host/backend/ffmpeg/ffmpeg_capture_manager.h
    host/backend/ffmpeg/icapture_frame_reader.h
    host/backend/ffmpeg/ffmpegutils.h
)

# Add GStreamer backend only on Linux
if(NOT WIN32)
    list(APPEND HOST_SOURCES
        host/backend/gstreamerbackendhandler.cpp
        host/backend/gstreamer/sinkselector.cpp
        host/backend/gstreamer/queueconfigurator.cpp
        host/backend/gstreamer/videooverlaymanager.cpp
        host/backend/gstreamer/pipelinebuilder.cpp
        host/backend/gstreamer/pipelinefactory.cpp
        host/backend/gstreamer/gstreamerhelpers.cpp
        host/backend/gstreamer/inprocessgstrunner.cpp
        host/backend/gstreamer/inprocessgstrunner.h
        host/backend/gstreamer/externalgstrunner.cpp
        host/backend/gstreamer/externalgstrunner.h
        host/backend/gstreamer/recordingmanager.cpp
        host/backend/gstreamer/recordingmanager.h
        host/backend/gstreamerbackendhandler.h
        host/backend/gstreamer/sinkselector.h
        host/backend/gstreamer/queueconfigurator.h
        host/backend/gstreamer/videooverlaymanager.h
        host/backend/gstreamer/pipelinebuilder.h
        host/backend/gstreamer/pipelinefactory.h
        host/backend/gstreamer/gstreamerhelpers.h
    )
endif()

# Regex sources
set(REGEX_SOURCES
    regex/RegularExpression.cpp regex/RegularExpression.h
)

# Resource sources
set(RESOURCE_SOURCES
    resources/version.h
)

# Script sources
set(SCRIPT_SOURCES
    scripts/KeyboardMouse.cpp scripts/KeyboardMouse.h
    scripts/Lexer.cpp scripts/Lexer.h
    scripts/Parser.cpp scripts/Parser.h
    scripts/semanticAnalyzer.cpp scripts/semanticAnalyzer.h
    scripts/scriptExecutor.cpp scripts/scriptExecutor.h
    scripts/scriptRunner.cpp scripts/scriptRunner.h
    scripts/scriptEditor.cpp scripts/scriptEditor.h
)

# Serial sources
set(SERIAL_SOURCES
    serial/SerialPortManager.cpp serial/SerialPortManager.h
    serial/SerialCommandCoordinator.cpp serial/SerialCommandCoordinator.h
    serial/SerialStateManager.cpp serial/SerialStateManager.h
    serial/SerialStatistics.cpp serial/SerialStatistics.h
    serial/SerialFacade.cpp serial/SerialFacade.h
    serial/FactoryResetManager.cpp serial/FactoryResetManager.h
    serial/ch9329.h
    serial/chipstrategy/IChipStrategy.h
    serial/chipstrategy/CH9329Strategy.cpp serial/chipstrategy/CH9329Strategy.h
    serial/chipstrategy/CH32V208Strategy.cpp serial/chipstrategy/CH32V208Strategy.h
    serial/chipstrategy/ChipStrategyFactory.cpp serial/chipstrategy/ChipStrategyFactory.h
    serial/protocol/SerialProtocol.cpp serial/protocol/SerialProtocol.h
    serial/watchdog/ConnectionWatchdog.cpp serial/watchdog/ConnectionWatchdog.h
)

# Server sources
set(SERVER_SOURCES
    server/tcpServer.cpp server/tcpServer.h
)

# Target sources
set(TARGET_SOURCES
    target/KeyboardLayouts.cpp target/KeyboardLayouts.h
    target/KeyboardManager.cpp target/KeyboardManager.h
    target/Keymapping.h
    target/MouseManager.cpp target/MouseManager.h
    target/mouseeventdto.cpp target/mouseeventdto.h
)

# Video sources
set(VIDEO_SOURCES
    video/videohid.cpp video/videohid.h
    video/firmwarewriter.cpp video/firmwarewriter.h
    video/firmwarereader.cpp video/firmwarereader.h
    video/ms2109.h
)

# UI core sources
set(UI_CORE_SOURCES
    ui/TaskManager.cpp ui/TaskManager.h
    ui/globalsetting.cpp ui/globalsetting.h
    ui/inputhandler.cpp ui/inputhandler.h
    ui/loghandler.cpp ui/loghandler.h
    ui/mainwindow.cpp ui/mainwindow.h ui/mainwindow.ui
    ui/videopane.cpp ui/videopane.h
    ui/languagemanager.cpp ui/languagemanager.h
    ui/screensavermanager.cpp ui/screensavermanager.h
    ui/screenscale.h ui/screenscale.cpp
    ui/splashscreen.cpp ui/splashscreen.h
    ui/statusevents.h
)

# UI coordinator sources
set(UI_COORDINATOR_SOURCES
    ui/coordinator/devicecoordinator.cpp ui/coordinator/devicecoordinator.h
    ui/coordinator/menucoordinator.cpp ui/coordinator/menucoordinator.h
    ui/coordinator/windowlayoutcoordinator.cpp ui/coordinator/windowlayoutcoordinator.h
)

# UI help sources
set(UI_HELP_SOURCES
    ui/help/helppane.cpp ui/help/helppane.h
    ui/help/versioninfomanager.cpp ui/help/versioninfomanager.h
)

# UI advance sources
set(UI_ADVANCE_SOURCES
    ui/advance/scripttool.cpp ui/advance/scripttool.h
    ui/advance/serialportdebugdialog.cpp ui/advance/serialportdebugdialog.h
    ui/advance/DeviceSelectorDialog.cpp ui/advance/DeviceSelectorDialog.h
    ui/advance/devicediagnosticsdialog.cpp ui/advance/devicediagnosticsdialog.h
    ui/advance/diagnostics/diagnosticsmanager.cpp ui/advance/diagnostics/diagnosticsmanager.h ui/advance/diagnostics/diagnostics_constants.h
    ui/advance/envdialog.cpp ui/advance/envdialog.h ui/advance/envdialog.ui
    ui/advance/firmwareupdatedialog.cpp ui/advance/firmwareupdatedialog.h
    ui/advance/firmwaremanagerdialog.cpp ui/advance/firmwaremanagerdialog.h
    ui/advance/renamedisplaydialog.cpp ui/advance/renamedisplaydialog.h
    ui/advance/updatedisplaysettingsdialog.cpp ui/advance/updatedisplaysettingsdialog.h
    ui/advance/recordingsettingsdialog.cpp ui/advance/recordingsettingsdialog.h
)

# UI initializer sources
set(UI_INITIALIZER_SOURCES
    ui/initializer/mainwindowinitializer.cpp ui/initializer/mainwindowinitializer.h
)

# UI statusbar sources
set(UI_STATUSBAR_SOURCES
    ui/statusbar/statusbarmanager.cpp ui/statusbar/statusbarmanager.h
    ui/statusbar/statuswidget.cpp ui/statusbar/statuswidget.h
)

# UI corner widget sources
set(UI_CORNERWIDGET_SOURCES
    ui/cornerwidget/cornerwidgetmanager.h ui/cornerwidget/cornerwidgetmanager.cpp
)

# UI window control sources
set(UI_WINDOWCONTROL_SOURCES
    ui/windowcontrolmanager.h ui/windowcontrolmanager.cpp
)

# UI toolbar sources
set(UI_TOOLBAR_SOURCES
    ui/toolbar/toggleswitch.cpp ui/toolbar/toggleswitch.h
    ui/toolbar/toolbarmanager.cpp ui/toolbar/toolbarmanager.h
)

# UI recording sources
set(UI_RECORDING_SOURCES
    ui/recording/recordingcontroller.cpp ui/recording/recordingcontroller.h
)

# UI preferences sources
set(UI_PREFERENCES_SOURCES
    ui/preferences/cameraadjust.cpp ui/preferences/cameraadjust.h
    ui/preferences/fpsspinbox.cpp ui/preferences/fpsspinbox.h
    ui/preferences/settingdialog.cpp ui/preferences/settingdialog.h ui/preferences/settingdialog.ui
    ui/preferences/logpage.cpp ui/preferences/logpage.h
    ui/preferences/videopage.cpp ui/preferences/videopage.h
    ui/preferences/audiopage.cpp ui/preferences/audiopage.h
    ui/preferences/targetcontrolpage.cpp ui/preferences/targetcontrolpage.h
)

# Combine all source files
set(SOURCE_FILES
    ${COMMON_SOURCES}
    ${DEVICE_SOURCES}
    ${HOST_SOURCES}
    ${REGEX_SOURCES}
    ${RESOURCE_SOURCES}
    ${SCRIPT_SOURCES}
    ${SERIAL_SOURCES}
    ${SERVER_SOURCES}
    ${TARGET_SOURCES}
    ${VIDEO_SOURCES}
    ${UI_CORE_SOURCES}
    ${UI_HELP_SOURCES}
    ${UI_ADVANCE_SOURCES}
    ${UI_STATUSBAR_SOURCES}
    ${UI_INITIALIZER_SOURCES}
    ${UI_COORDINATOR_SOURCES}
    ${UI_CORNERWIDGET_SOURCES}
    ${UI_WINDOWCONTROL_SOURCES}
    ${UI_TOOLBAR_SOURCES}
    ${UI_RECORDING_SOURCES}
    ${UI_PREFERENCES_SOURCES}
)

# Print source files summary
message(STATUS "Source files configured:")
message(STATUS "  Device sources: ${DEVICE_SOURCES}")
message(STATUS "  Host sources: ${HOST_SOURCES}")
message(STATUS "  UI sources: ${UI_CORE_SOURCES}")
message(STATUS "  Total source files: ${SOURCE_FILES}")
