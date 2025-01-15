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
    host/HostManager.cpp \
    serial/SerialPortManager.cpp \
    target/KeyboardManager.cpp \
    target/MouseManager.cpp \
    host/audiothread.cpp \
    host/usbcontrol.cpp \
    scripts/Lexer.cpp \
    scripts/Parser.cpp \
    scripts/semanticAnalyzer.cpp \
    scripts/KeyboardMouse.cpp 

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
    scripts/KeyboardMouse.h
FORMS    += \
    ui/mainwindow.ui \
    ui/settingdialog.ui 

RESOURCES += \
    openterfaceQT.rc \
    ui/mainwindow.qrc


# Link against the HID library
win32:LIBS += -lhid
win32:LIBS += -lsetupapi

win32 {
    INCLUDEPATH += $$PWD/lib
    LIBS += -L$$PWD/lib -llibusb-1.0
}

unix {
    INCLUDEPATH += /usr/include/
    LIBS += -lusb-1.0
}

# Set the target installation path to a directory within the build folder
target.path = $$PWD/build/install

INSTALLS += target

RC_FILE = openterfaceQT.rc

DEPENDPATH += $$PWD/''
