#-------------------------------------------------
#
# Project created by QtCreator 2024-04-02T19:21:24
#
#-------------------------------------------------

TARGET = openterfaceQT
TEMPLATE = app

QT       += core gui multimedia multimediawidgets serialport concurrent svg network
QT       += core gui multimedia multimediawidgets serialport concurrent svg network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

SOURCES += main.cpp \
    target/mouseeventdto.cpp \
    host/audiomanager.cpp \
    host/cameramanager.cpp \
    ui/fpsspinbox.cpp \
    ui/serialportdebugdialog.cpp \
    ui/settingdialog.cpp \
    ui/statuswidget.cpp \
    video/videohid.cpp \
    ui/helppane.cpp \
    ui/mainwindow.cpp \
    ui/metadatadialog.cpp \
    ui/videopane.cpp \
    ui/globalsetting.cpp \
    ui/toolbarmanager.cpp \
    ui/toggleswitch.cpp \
    ui/loghandler.cpp \
    ui/inputhandler.cpp \
    ui/versioninfomanager.cpp \    
    ui/statusbarmanager.cpp \
    ui/logpage.cpp \
    ui/hardwarepage.cpp \
    ui/videopage.cpp \
    ui/audiopage.cpp \
    ui/cameraajust.cpp \
    ui/scripttool.cpp \
    ui/TaskManager.cpp \
    ui/envdialog.cpp \
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
    server/tcpServer.cpp

HEADERS  += \
    global.h \
    target/mouseeventdto.h \
    host/audiomanager.h \
    host/cameramanager.h \
    ui/fpsspinbox.h \
    ui/serialportdebugdialog.h \
    ui/settingdialog.h \
    ui/statuswidget.h \
    video/videohid.h \
    ui/helppane.h \
    ui/mainwindow.h \
    ui/metadatadialog.h \
    ui/videopane.h \
    ui/globalsetting.h \
    ui/statusevents.h \
    ui/toolbarmanager.h \
    ui/toggleswitch.h \
    ui/loghandler.h \
    ui/inputhandler.h \
    ui/versioninfomanager.h \
    ui/statusbarmanager.h \
    ui/logpage.h \
    ui/hardwarepage.h \
    ui/videopage.h   \
    ui/audiopage.h \
    ui/cameraajust.h \
    ui/scripttool.h \
    ui/TaskManager.h \
    ui/envdialog.h \
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
    target/KeyboardLayouts.h 


FORMS    += \
    ui/mainwindow.ui \
    ui/settingdialog.ui \
    ui/envdialog.ui

RESOURCES += \
    openterfaceQT.rc \
    ui/mainwindow.qrc \
    config/keyboards/keyboard_layouts.qrc 


# Copy keyboard layout files to build directory
CONFIG += file_copies
COPIES += keyboard_layouts
keyboard_layouts.files = $$files($$PWD/config/keyboards/*.json)
keyboard_layouts.path = $$OUT_PWD/config/keyboards

# Create directories if they don't exist
system($$QMAKE_MKDIR $$shell_path($$PWD/config/keyboards))
system($$QMAKE_MKDIR $$shell_path($$OUT_PWD/config/keyboards))

# Link against the HID library
win32:LIBS += -lhid
win32:LIBS += -lsetupapi

win32 {
    INCLUDEPATH += $$PWD/lib
    LIBS += -L$$PWD/lib -llibusb-1.0 -loleaut32 -lwinpthread

    RESOURCES += driver/windows/drivers.qrc
}

unix {
    INCLUDEPATH += /usr/include/
    LIBS += -lusb-1.0

    RESOURCES += driver/linux/drivers.qrc
}

# Set platform-specific installation paths
win32 {
    target.path = $$(PROGRAMFILES)/openterfaceQT
} else {
    target.path = /usr/local/bin
}

INSTALLS += target

RC_FILE = openterfaceQT.rc

DEPENDPATH += $$PWD/''
