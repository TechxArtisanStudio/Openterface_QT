#-------------------------------------------------
#
# Project created by QtCreator 2024-04-02T19:21:24
#
#-------------------------------------------------

TARGET = openterfaceQT
TEMPLATE = app

MOC_DIR = moc
OBJECTS_DIR = obj
RCC_DIR = rcc

QMAKE_CXX = E:/QT/Tools/mingw1120_64/bin/g++.exe
QMAKE_CC = E:/QT/Tools/mingw1120_64/bin/gcc.exe

QT       += core gui multimedia multimediawidgets serialport concurrent svg svgwidgets network opengl openglwidgets xml dbus
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

INCLUDEPATH += $$PWD

SOURCES += main.cpp \
    device/DeviceInfo.cpp \
    device/DeviceManager.cpp \
    device/HotplugMonitor.cpp \
    device/platform/AbstractPlatformDeviceManager.cpp \
    device/platform/DeviceFactory.cpp \
    host/HostManager.cpp \
    host/audiomanager.cpp \
    host/audiothread.cpp \
    host/cameramanager.cpp \
    host/usbcontrol.cpp \
    host/multimediabackend.cpp \
    host/backend/qtmultimediabackendhandler.cpp \
    host/backend/qtbackendhandler.cpp \
    host/backend/ffmpegbackendhandler.cpp \
    host/backend/ffmpeg/capturethread.cpp \
    host/backend/ffmpeg/ffmpeg_hardware_accelerator.cpp \
    host/backend/ffmpeg/ffmpeg_device_manager.cpp \
    host/backend/ffmpeg/ffmpeg_frame_processor.cpp \
    host/backend/ffmpeg/ffmpeg_recorder.cpp \
    host/backend/ffmpeg/ffmpeg_device_validator.cpp \
    host/backend/ffmpeg/ffmpeg_hotplug_handler.cpp \
    host/backend/ffmpeg/ffmpeg_capture_manager.cpp \
    regex/RegularExpression.cpp \
    scripts/KeyboardMouse.cpp \
    scripts/Lexer.cpp \
    scripts/Parser.cpp \
    scripts/semanticAnalyzer.cpp \
    scripts/scriptEditor.cpp \
    serial/SerialPortManager.cpp \
    serial/SerialCommandCoordinator.cpp \
    serial/SerialStateManager.cpp \
    serial/SerialStatistics.cpp \
    serial/SerialFacade.cpp \
    server/tcpServer.cpp \
    target/KeyboardLayouts.cpp \
    target/KeyboardManager.cpp \
    target/MouseManager.cpp \
    target/mouseeventdto.cpp \
    video/videohid.cpp \
    video/firmwarewriter.cpp \
    video/firmwarereader.cpp \
    ui/TaskManager.cpp \
    ui/globalsetting.cpp \
    ui/inputhandler.cpp \
    ui/loghandler.cpp \
    ui/mainwindow.cpp \
    ui/videopane.cpp \
    ui/languagemanager.cpp \
    ui/screensavermanager.cpp \
    ui/screenscale.cpp \
    ui/coordinator/devicecoordinator.cpp \
    ui/coordinator/menucoordinator.cpp \
    ui/coordinator/windowlayoutcoordinator.cpp \
    ui/help/helppane.cpp \
    ui/help/versioninfomanager.cpp \
    ui/advance/scripttool.cpp \
    ui/advance/serialportdebugdialog.cpp \
    ui/advance/DeviceSelectorDialog.cpp \
    ui/advance/devicediagnosticsdialog.cpp \
    ui/advance/envdialog.cpp \
    ui/advance/firmwareupdatedialog.cpp \
    ui/advance/firmwaremanagerdialog.cpp \
    ui/advance/renamedisplaydialog.cpp \
    ui/advance/updatedisplaysettingsdialog.cpp \
    ui/advance/recordingsettingsdialog.cpp \
    ui/initializer/mainwindowinitializer.cpp \
    ui/statusbar/statusbarmanager.cpp \
    ui/statusbar/statuswidget.cpp \
    ui/cornerwidget/cornerwidgetmanager.cpp \
    ui/windowcontrolmanager.cpp \
    ui/toolbar/toggleswitch.cpp \
    ui/toolbar/toolbarmanager.cpp \
    ui/recording/recordingcontroller.cpp \
    ui/preferences/cameraadjust.cpp \
    ui/preferences/fpsspinbox.cpp \
    ui/preferences/settingdialog.cpp \
    ui/preferences/logpage.cpp \
    ui/preferences/videopage.cpp \
    ui/preferences/audiopage.cpp \
    ui/preferences/targetcontrolpage.cpp

# Platform-specific backend handlers (exclude on Windows)
!win32 {
    SOURCES += host/backend/gstreamerbackendhandler.cpp \
               host/backend/gstreamer/sinkselector.cpp \
               host/backend/gstreamer/queueconfigurator.cpp \
               host/backend/gstreamer/videooverlaymanager.cpp \
               host/backend/gstreamer/pipelinebuilder.cpp \
               host/backend/gstreamer/pipelinefactory.cpp \
               host/backend/gstreamer/gstreamerhelpers.cpp \
               host/backend/gstreamer/inprocessgstrunner.cpp \
               host/backend/gstreamer/externalgstrunner.cpp \
               host/backend/gstreamer/recordingmanager.cpp
    HEADERS += host/backend/gstreamerbackendhandler.h \
               host/backend/gstreamer/sinkselector.h \
               host/backend/gstreamer/queueconfigurator.h \
               host/backend/gstreamer/videooverlaymanager.h \
               host/backend/gstreamer/pipelinebuilder.h \
               host/backend/gstreamer/pipelinefactory.h \
               host/backend/gstreamer/gstreamerhelpers.h \
               host/backend/gstreamer/inprocessgstrunner.h \
               host/backend/gstreamer/externalgstrunner.h \
               host/backend/gstreamer/recordingmanager.h
}


HEADERS  += \
    global.h \
    device/DeviceInfo.h \
    device/DeviceManager.h \
    device/HotplugMonitor.h \
    device/platform/AbstractPlatformDeviceManager.h \
    device/platform/DeviceFactory.h \
    device/platform/windows/WinDeviceEnumerator.h \
    device/platform/windows/IDeviceEnumerator.h \
    host/HostManager.h \
    host/audiomanager.h \
    host/audiothread.h \
    host/cameramanager.h \
    host/usbcontrol.h \
    host/multimediabackend.h \
    host/backend/qtmultimediabackendhandler.h \
    host/backend/qtbackendhandler.h \
    host/backend/ffmpegbackendhandler.h \
    host/backend/ffmpeg/capturethread.h \
    host/backend/ffmpeg/ffmpeg_hardware_accelerator.h \
    host/backend/ffmpeg/ffmpeg_device_manager.h \
    host/backend/ffmpeg/ffmpeg_frame_processor.h \
    host/backend/ffmpeg/ffmpeg_recorder.h \
    host/backend/ffmpeg/ffmpeg_device_validator.h \
    host/backend/ffmpeg/ffmpeg_hotplug_handler.h \
    host/backend/ffmpeg/ffmpeg_capture_manager.h \
    host/backend/ffmpeg/icapture_frame_reader.h \
    host/backend/ffmpeg/ffmpegutils.h \
    regex/RegularExpression.h \
    scripts/KeyboardMouse.h \
    scripts/Lexer.h \
    scripts/Parser.h \
    scripts/semanticAnalyzer.h \
    scripts/scriptEditor.h \
    serial/SerialPortManager.h \
    serial/SerialCommandCoordinator.h \
    serial/SerialStateManager.h \
    serial/SerialStatistics.h \
    serial/SerialFacade.h \
    serial/ch9329.h \
    server/tcpServer.h \
    target/KeyboardLayouts.h \
    target/KeyboardManager.h \
    target/MouseManager.h \
    target/Keymapping.h \
    target/mouseeventdto.h \
    resources/version.h \
    video/videohid.h \
    video/firmwarewriter.h \
    video/firmwarereader.h \
    video/ms2109.h \
    ui/TaskManager.h \
    ui/globalsetting.h \
    ui/inputhandler.h \
    ui/loghandler.h \
    ui/mainwindow.h \
    ui/videopane.h \
    ui/statusevents.h \
    ui/languagemanager.h \
    ui/screensavermanager.h \
    ui/screenscale.h \
    ui/coordinator/devicecoordinator.h \
    ui/coordinator/menucoordinator.h \
    ui/coordinator/windowlayoutcoordinator.h \
    ui/help/helppane.h \
    ui/help/versioninfomanager.h \
    ui/advance/scripttool.h \
    ui/advance/serialportdebugdialog.h \
    ui/advance/DeviceSelectorDialog.h \
    ui/advance/devicediagnosticsdialog.h \
    ui/advance/envdialog.h \
    ui/advance/firmwareupdatedialog.h \
    ui/advance/firmwaremanagerdialog.h \
    ui/advance/renamedisplaydialog.h \
    ui/advance/updatedisplaysettingsdialog.h \
    ui/advance/recordingsettingsdialog.h \
    ui/initializer/mainwindowinitializer.h \
    ui/statusbar/statusbarmanager.h \
    ui/statusbar/statuswidget.h \
    ui/cornerwidget/cornerwidgetmanager.h \
    ui/windowcontrolmanager.h \
    ui/toolbar/toggleswitch.h \
    ui/toolbar/toolbarmanager.h \
    ui/recording/recordingcontroller.h \
    ui/preferences/cameraadjust.h \
    ui/preferences/fpsspinbox.h \
    ui/preferences/settingdialog.h \
    ui/preferences/logpage.h \
    ui/preferences/targetcontrolpage.h \
    ui/preferences/videopage.h \
    ui/preferences/audiopage.h

FORMS    += \
    ui/mainwindow.ui \
    ui/preferences/settingdialog.ui \
    ui/advance/envdialog.ui

RESOURCES += \
    ui/mainwindow.qrc \
    config/keyboards/keyboard_layouts.qrc \
    config/languages/language.qrc


# Link against the HID library
win32:LIBS += -lhid
win32:LIBS += -lsetupapi
win32:LIBS += -lcfgmgr32
win32:LIBS += -lole32

win32 {
    # Add Windows-specific device manager
    SOURCES += device/platform/WindowsDeviceManager.cpp \
        device/platform/windows/WinDeviceEnumerator.cpp \
        device/platform/windows/discoverers/BaseDeviceDiscoverer.cpp \
        device/platform/windows/discoverers/BotherDeviceDiscoverer.cpp \
        device/platform/windows/discoverers/Generation3Discoverer.cpp \
        device/platform/windows/discoverers/DeviceDiscoveryManager.cpp
    HEADERS += device/platform/WindowsDeviceManager.h \
        device/platform/windows/discoverers/IDeviceDiscoverer.h \
        device/platform/windows/discoverers/BaseDeviceDiscoverer.h \
        device/platform/windows/discoverers/BotherDeviceDiscoverer.h \
        device/platform/windows/discoverers/Generation3Discoverer.h \
        device/platform/windows/discoverers/DeviceDiscoveryManager.h
    
    INCLUDEPATH += $$PWD/lib
    INCLUDEPATH += C:\ffmpeg-static\include
    LIBS += -L$$PWD/lib -llibusb-1.0 -loleaut32 -lwinpthread
    # Prefer ffmpeg libs from C:/ffmpeg-static, also search msys2 mingw64; only add -lmfx if libmfx exists
    FF_LIB_DIR = C:/ffmpeg-static/lib
    MSYS_LIB_DIR = C:/msys64/mingw64/lib
    MFX_FF = $$FF_LIB_DIR/libmfx.a
    MFX_MSYS = $$MSYS_LIB_DIR/libmfx.a
    # Ensure the ffmpeg lib dir is searched first so -lav* resolves
    LIBS += -L$$FF_LIB_DIR -L$$MSYS_LIB_DIR
    exists($$MFX_FF) {
        message("Using libmfx from $$MFX_FF")
        LIBS += -Wl,--start-group -lavcodec -lavformat -lavutil -lavdevice -lswscale -lswresample -lmfx -lz -Wl,--end-group
    } else {
        exists($$MFX_MSYS) {
            message("Using libmfx from $$MFX_MSYS")
            LIBS += -Wl,--start-group -lavcodec -lavformat -lavutil -lavdevice -lswscale -lswresample -lmfx -lz -Wl,--end-group
        } else {
            message("libmfx not found; building without -lmfx. Install Intel oneVPL or place libmfx in C:/ffmpeg-static/lib or C:/msys64/mingw64/lib to enable.")
            LIBS += -Wl,--start-group -lavcodec -lavformat -lavutil -lavdevice -lswscale -lswresample -lz -Wl,--end-group
        }
    }

    # Add additional system libraries FFmpeg objects may depend on (Windows)
    LIBS += -lws2_32 -lwsock32 -lbcrypt -lbz2 -llzma -lsecur32 -lshlwapi -lwinmm
    
    # Or if you installed oneVPL elsewhere, point to that lib dir:
    # LIBS += -L"C:/Program Files (x86)/Intel/oneVPL/lib" -lmfx
    # Add FFmpeg support for Windows
    DEFINES += HAVE_FFMPEG

    # Add libjpeg-turbo for Windows (commented out - not available)
    # LIBS += -ljpeg
    # DEFINES += HAVE_LIBJPEG_TURBO

    # Add libjpeg-turbo for Windows (commented out - not available)
    # LIBS += -ljpeg
    # DEFINES += HAVE_LIBJPEG_TURBO

    RESOURCES += driver/windows/drivers.qrc
}

unix {
    # Add Linux-specific sources if any
    SOURCES += dlopen_wrapper.c
    HEADERS += device/platform/LinuxDeviceManager.h

    INCLUDEPATH += /usr/include
    LIBS += -lusb-1.0

    # On non-mac Unix systems enable pkg-config based dependencies
    unix:!macx {
        CONFIG += link_pkgconfig
        PKGCONFIG += libudev gstreamer-1.0 gstreamer-video-1.0 libavformat libavcodec libavutil libswscale libavdevice libjpeg
        DEFINES += HAVE_LIBUDEV HAVE_GSTREAMER HAVE_FFMPEG HAVE_LIBJPEG_TURBO
    }

    RESOURCES += driver/linux/drivers.qrc
}

# Set platform-specific installation paths
win32 {
    target.path = $$(PROGRAMFILES)/openterfaceQT
} else {
    target.path = /usr/local/bin
}

# INSTALLS += target

RC_FILE = openterfaceQT.rc

TRANSLATIONS += config/languages/openterface_en.ts \
                config/languages/openterface_fr.ts \
                config/languages/openterface_da.ts \
                config/languages/openterface_ja.ts \
                config/languages/openterface_se.ts \
                config/languages/openterface_de.ts \
                config/languages/openterface_zh.ts
                # Add more languages here