#-------------------------------------------------
#
# Project created by QtCreator 2024-04-02T19:21:24
#
#-------------------------------------------------

TARGET = openterfaceQT
TEMPLATE = app

QT       += core gui multimedia multimediawidgets serialport concurrent svg virtualkeyboard

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

SOURCES += main.cpp \
    target/mouseeventdto.cpp \
    host/audiomanager.cpp \
    ui/fpsspinbox.cpp \
    ui/serialportdebugdialog.cpp \
    ui/settingdialog.cpp \
    ui/statuswidget.cpp \
    video/videohid.cpp \
    ui/helppane.cpp \
    ui/imagesettings.cpp \
    ui/mainwindow.cpp \
    ui/metadatadialog.cpp \
    ui/videopane.cpp \
    ui/videosettings.cpp \
    ui/globalsetting.cpp \
    ui/virtualkeyboardwidget.cpp \
    # ui/serialportDebug.cpp \
    host/HostManager.cpp \
    serial/SerialPortManager.cpp \
    target/KeyboardManager.cpp \
    target/MouseManager.cpp

HEADERS  += \
    global.h \
    target/mouseeventdto.h \
    host/audiomanager.h \
    ui/fpsspinbox.h \
    ui/serialportdebugdialog.h \
    ui/settingdialog.h \
    ui/statuswidget.h \
    video/videohid.h \
    ui/helppane.h \
    ui/imagesettings.h \
    ui/mainwindow.h \
    ui/metadatadialog.h \
    ui/videopane.h \
    ui/videosettings.h \
    ui/globalsetting.h \
    ui/statusevents.h \
    ui/virtualkeyboardwidget.h \
    # ui/serialportDebug \
    host/HostManager.h \
    serial/ch9329.h \
    serial/SerialPortManager.h \
    target/KeyboardManager.h \
    target/MouseManager.h \
    target/Keymapping.h

FORMS    += \
    ui/imagesettings.ui \
    ui/mainwindow.ui \
    ui/serialportdebugdialog.ui \
    ui/settingdialog.ui \
    ui/videosettings.ui

RESOURCES += \
    openterfaceQT.rc \
    ui/mainwindow.qrc


# Link against the HID library
win32:LIBS += -lhid
win32:LIBS += -lsetupapi


# Set the target installation path to a directory within the build folder
target.path = $$PWD/build/install

INSTALLS += target

RC_FILE = openterfaceQT.rc

INCLUDEPATH += $$PWD/''
DEPENDPATH += $$PWD/''
