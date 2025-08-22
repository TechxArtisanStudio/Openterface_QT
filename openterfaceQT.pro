#-------------------------------------------------
#
# Project created by QtCreator 2024-04-02T19:21:24
#
#-------------------------------------------------

TARGET = openterfaceQT
TEMPLATE = app

QT       += core gui multimedia multimediawidgets serialport concurrent svg network dbus
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

SOURCES += main.cpp \
    device/DeviceInfo.cpp \
    device/DeviceManager.cpp \
    device/HotplugMonitor.cpp \
    device/platform/AbstractPlatformDeviceManager.cpp \
    device/platform/DeviceFactory.cpp \
    target/mouseeventdto.cpp \
    host/audiomanager.cpp \
    host/cameramanager.cpp \
    host/multimediabackend.cpp \
    host/backend/ffmpegbackendhandler.cpp \
    host/backend/gstreamerbackendhandler.cpp \
    host/backend/qtmultimediabackendhandler.cpp \
    ui/statusbar/statuswidget.cpp \
    video/videohid.cpp \
    video/firmwarewriter.cpp \
    video/firmwarereader.cpp \
    ui/help/helppane.cpp \
    ui/mainwindow.cpp \
    ui/videopane.cpp \
    ui/globalsetting.cpp \
    ui/toolbar/toolbarmanager.cpp \
    ui/toolbar/toggleswitch.cpp \
    ui/loghandler.cpp \
    ui/inputhandler.cpp \ 
    ui/preferences/settingdialog.cpp \
    ui/preferences/logpage.cpp \
    ui/preferences/targetcontrolpage.cpp \
    ui/preferences/videopage.cpp \
    ui/preferences/audiopage.cpp \
    ui/TaskManager.cpp \
    ui/statusbar/statusbarmanager.cpp \
    ui/preferences/fpsspinbox.cpp \
    ui/preferences/cameraadjust.cpp \
    ui/advance/serialportdebugdialog.cpp \
    ui/advance/DeviceSelectorDialog.cpp \
    ui/advance/scripttool.cpp \
    ui/help/versioninfomanager.cpp \ 
    ui/advance/envdialog.cpp \
    ui/advance/firmwareupdatedialog.cpp \
    ui/advance/firmwaremanagerdialog.cpp \
    ui/advance/renamedisplaydialog.cpp \
    ui/advance/updatedisplaysettingsdialog.cpp \
    host/HostManager.cpp \
    serial/SerialPortManager.cpp \
    target/KeyboardManager.cpp \
    target/MouseManager.cpp \
    host/audiothread.cpp \
    host/usbcontrol.cpp \
    scripts/Lexer.cpp \
    scripts/Parser.cpp \
    scripts/semanticAnalyzer.cpp \
    scripts/KeyboardMouse.cpp \
    target/KeyboardLayouts.cpp \
    regex/RegularExpression.cpp \
    server/tcpServer.cpp \
    scripts/scriptEditor.cpp \
    ui/languagemanager.cpp \
    ui/screensavermanager.cpp \
    ui/screenscale.cpp \
    ui/cornerwidget/cornerwidgetmanager.cpp


HEADERS  += \
    global.h \
    device/DeviceInfo.h \
    device/DeviceManager.h \
    device/HotplugMonitor.h \
    device/platform/AbstractPlatformDeviceManager.h \
    device/platform/DeviceFactory.h \
    target/mouseeventdto.h \
    host/audiomanager.h \
    host/cameramanager.h \
    host/multimediabackend.h \
    host/backend/ffmpegbackendhandler.h \
    host/backend/gstreamerbackendhandler.h \
    host/backend/qtmultimediabackendhandler.h \
    ui/statusbar/statuswidget.h \
    video/videohid.h \
    video/firmwarewriter.h \
    video/firmwarereader.h \
    ui/help/helppane.h \
    ui/mainwindow.h \
    ui/videopane.h \
    ui/globalsetting.h \
    ui/statusevents.h \
    ui/toolbar/toolbarmanager.h \
    ui/toolbar/toggleswitch.h \
    ui/loghandler.h \
    ui/inputhandler.h \
    ui/statusbar/statusbarmanager.h \
    ui/help/versioninfomanager.h \
    ui/advance/serialportdebugdialog.h \
    ui/advance/DeviceSelectorDialog.h \
    ui/advance/scripttool.h \
    ui/advance/envdialog.h \
    ui/advance/firmwareupdatedialog.h \
    ui/advance/firmwaremanagerdialog.h \
    ui/advance/renamedisplaydialog.h \
    ui/advance/updatedisplaysettingsdialog.h \
    ui/preferences/cameraadjust.h \
    ui/preferences/fpsspinbox.h \
    ui/preferences/settingdialog.h \
    ui/preferences/logpage.h \
    ui/preferences/targetcontrolpage.h \
    ui/preferences/videopage.h   \
    ui/preferences/audiopage.h \
    ui/TaskManager.h \
    host/HostManager.h \
    serial/ch9329.h \
    serial/SerialPortManager.h \
    target/KeyboardManager.h \
    target/MouseManager.h \
    target/Keymapping.h \
    resources/version.h \
    host/audiothread.h \
    host/usbcontrol.h \
    scripts/Lexer.h \
    scripts/Parser.h \
    scripts/semanticAnalyzer.h \
    scripts/KeyboardMouse.h \
    server/tcpServer.h \
    regex/RegularExpression.h \
    target/KeyboardLayouts.h \
    scripts/scriptEditor.h \
    ui/languagemanager.h \
    ui/screensavermanager.h \
    ui/screenscale.h \
    ui/cornerwidget/cornerwidgetmanager.h

FORMS    += \
    ui/mainwindow.ui \
    ui/preferences/settingdialog.ui \
    ui/advance/envdialog.ui

RESOURCES += \
    openterfaceQT.rc \
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
    SOURCES += device/platform/WindowsDeviceManager.cpp
    HEADERS += device/platform/WindowsDeviceManager.h
    
    INCLUDEPATH += $$PWD/lib
    LIBS += -L$$PWD/lib -llibusb-1.0 -loleaut32 -lwinpthread

    # Add libjpeg-turbo for Windows (commented out - not available)
    # LIBS += -ljpeg
    # DEFINES += HAVE_LIBJPEG_TURBO

    # Add libjpeg-turbo for Windows (commented out - not available)
    # LIBS += -ljpeg
    # DEFINES += HAVE_LIBJPEG_TURBO

    RESOURCES += driver/windows/drivers.qrc
}

unix {
    # Add Linux-specific device manager
    SOURCES += device/platform/LinuxDeviceManager.cpp
    HEADERS += device/platform/LinuxDeviceManager.h
    
    INCLUDEPATH += /usr/include/
    LIBS += -lusb-1.0
    
    # Add libudev for enhanced device detection
    unix:!macx {
        CONFIG += link_pkgconfig
        PKGCONFIG += libudev
        DEFINES += HAVE_LIBUDEV
        LIBS += -ludev
        
        # Add GStreamer support
        PKGCONFIG += gstreamer-1.0 gstreamer-video-1.0 gstreamer-plugins-base-1.0
        DEFINES += HAVE_GSTREAMER GSTREAMER_DYNAMIC_LINKING
        
        # Add FFmpeg support
        PKGCONFIG += libavformat libavcodec libavutil libswscale libavdevice
        DEFINES += HAVE_FFMPEG
        
        # Add libjpeg-turbo support
        DEFINES += HAVE_LIBJPEG_TURBO
        # Link both libjpeg and libturbojpeg (libturbojpeg might be named differently)
        LIBS += -ljpeg -lturbojpeg
        # Alternative library names for TurboJPEG on different systems
        # ubuntu: libturbojpeg.so, some systems: libturbojpeg.so.0
        LIBS += -L/usr/lib/x86_64-linux-gnu
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

DEPENDPATH += $$PWD/''

TRANSLATIONS += config/languages/openterface_en.ts \
                config/languages/openterface_fr.ts \
                config/languages/openterface_da.ts \
                config/languages/openterface_ja.ts \
                config/languages/openterface_se.ts \
                config/languages/openterface_de.ts \
                config/languages/openterface_zh.ts
                # Add more languages here