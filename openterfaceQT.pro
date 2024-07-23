#-------------------------------------------------
#
# Project created by QtCreator 2024-04-02T19:21:24
#
#-------------------------------------------------

TARGET = openterfaceQT
TEMPLATE = app

QT       += core gui multimedia multimediawidgets serialport concurrent

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

SOURCES += main.cpp \
    video/videohid.cpp \
    ui/helppane.cpp \
    ui/imagesettings.cpp \
    ui/mainwindow.cpp \
    ui/metadatadialog.cpp \
    ui/transwindow.cpp \
    ui/videopane.cpp \
    ui/videosettings.cpp \
    host/HostManager.cpp \
    serial/SerialPortManager.cpp \
    target/KeyboardManager.cpp \
    target/MouseManager.cpp

HEADERS  += \
    global.h \
    video/videohid.h \
    ui/helppane.h \
    ui/imagesettings.h \
    ui/mainwindow.h \
    ui/metadatadialog.h \
    ui/transwindow.h \
    ui/videopane.h \
    ui/videosettings.h \
    host/HostManager.h \
    serial/serialportevents.h \
    serial/SerialPortManager.h \
    target/KeyboardManager.h \
    target/MouseManager.h \
    target/Keymapping.h

FORMS    += \
    ui/imagesettings.ui \
    ui/mainwindow.ui \
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
