#-------------------------------------------------
#
# Project created by QtCreator 2024-04-02T19:21:24
#
#-------------------------------------------------

TARGET = openterfaceQT
TEMPLATE = app

QT       += core gui multimedia multimediawidgets serialport concurrent svg

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
    ui/globalsetting.cpp \
    ui/toolbarmanager.cpp \
    ui/toggleswitch.cpp \
    ui/loghandler.cpp \
    ui/inputhandler.cpp \
    ui/cameramanager.cpp \
    ui/versioninfomanager.cpp \    
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
    ui/globalsetting.h \
    ui/statusevents.h \
    ui/toolbarmanager.h \
    ui/toggleswitch.h \
    ui/loghandler.h \
    ui/inputhandler.h \
    ui/cameramanager.h \
    ui/versioninfomanager.h \
    host/HostManager.h \
    serial/ch9329.h \
    serial/SerialPortManager.h \
    target/KeyboardManager.h \
    target/MouseManager.h \
    target/Keymapping.h

FORMS    += \
    ui/imagesettings.ui \
    ui/mainwindow.ui \
    ui/settingdialog.ui 

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

# Define the version file
VERSION_FILE = version.txt

# Copy the version file to the build directory
version_file.files = $$VERSION_FILE
version_file.path = $$OUT_PWD
COPIES += version_file

# Make sure the version file is included in the distribution
DISTFILES += $$VERSION_FILE

# Remove version.h if it's listed in your HEADERS
# HEADERS -= version.h

# Make sure global.h is included in your HEADERS if it's not already there
HEADERS += global.h
