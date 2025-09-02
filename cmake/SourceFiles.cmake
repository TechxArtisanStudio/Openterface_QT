# SourceFiles.cmake - Source files definitions

# Common source files for all platforms
set(COMMON_SOURCES
    global.h
    main.cpp
)

# Add dlopen_wrapper.c only for static builds
if(OPENTERFACE_BUILD_STATIC)
    list(APPEND COMMON_SOURCES dlopen_wrapper.c)
endif()

# Device management
set(DEVICE_SOURCES
    device/DeviceInfo.cpp device/DeviceInfo.h
    device/DeviceManager.cpp device/DeviceManager.h
    device/HotplugMonitor.cpp device/HotplugMonitor.h
    device/platform/AbstractPlatformDeviceManager.cpp device/platform/AbstractPlatformDeviceManager.h
    device/platform/DeviceFactory.cpp device/platform/DeviceFactory.h
)

# Host management
set(HOST_SOURCES
    host/HostManager.cpp host/HostManager.h
    host/audiomanager.cpp host/audiomanager.h
    host/audiothread.cpp host/audiothread.h
    host/cameramanager.cpp host/cameramanager.h
    host/usbcontrol.cpp host/usbcontrol.h
    host/multimediabackend.cpp host/multimediabackend.h
    host/backend/ffmpegbackendhandler.cpp host/backend/ffmpegbackendhandler.h
    host/backend/gstreamerbackendhandler.cpp host/backend/gstreamerbackendhandler.h
    host/backend/qtmultimediabackendhandler.cpp host/backend/qtmultimediabackendhandler.h
)

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
    scripts/scriptEditor.cpp scripts/scriptEditor.h
)

# Serial sources
set(SERIAL_SOURCES
    serial/SerialPortManager.cpp serial/SerialPortManager.h
    serial/ch9329.h
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
    ui/statusevents.h
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
    ui/advance/envdialog.cpp ui/advance/envdialog.h ui/advance/envdialog.ui
    ui/advance/firmwareupdatedialog.cpp ui/advance/firmwareupdatedialog.h
    ui/advance/firmwaremanagerdialog.cpp ui/advance/firmwaremanagerdialog.h
    ui/advance/renamedisplaydialog.cpp ui/advance/renamedisplaydialog.h
    ui/advance/updatedisplaysettingsdialog.cpp ui/advance/updatedisplaysettingsdialog.h
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

# UI toolbar sources
set(UI_TOOLBAR_SOURCES
    ui/toolbar/toggleswitch.cpp ui/toolbar/toggleswitch.h
    ui/toolbar/toolbarmanager.cpp ui/toolbar/toolbarmanager.h
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
    ${UI_CORNERWIDGET_SOURCES}
    ${UI_TOOLBAR_SOURCES}
    ${UI_PREFERENCES_SOURCES}
)

# Print source files summary
message(STATUS "Source files configured:")
message(STATUS "  Device sources: ${DEVICE_SOURCES}")
message(STATUS "  Host sources: ${HOST_SOURCES}")
message(STATUS "  UI sources: ${UI_CORE_SOURCES}")
message(STATUS "  Total source files: ${SOURCE_FILES}")
