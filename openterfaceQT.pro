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
    target/mouseeventdto.cpp \
    host/audiomanager.cpp \
    host/cameramanager.cpp \
    ui/statusbar/statuswidget.cpp \
    video/videohid.cpp \
    video/firmwarewriter.cpp \
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
    ui/advance/scripttool.cpp \
    ui/help/versioninfomanager.cpp \ 
    ui/advance/envdialog.cpp \
    ui/advance/firmwareupdatedialog.cpp \
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
    ui/screensavermanager.cpp


HEADERS  += \
    global.h \
    target/mouseeventdto.h \
    host/audiomanager.h \
    host/cameramanager.h \
    ui/statusbar/statuswidget.h \
    video/videohid.h \
    video/firmwarewriter.h \
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
    ui/advance/scripttool.h \
    ui/advance/envdialog.h \
    ui/advance/firmwareupdatedialog.h \
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
    ui/screensavermanager.h

FORMS    += \
    ui/mainwindow.ui \
    ui/preferences/settingdialog.ui \
    ui/advance/envdialog.ui

RESOURCES += \
    openterfaceQT.rc \
    ui/mainwindow.qrc \
    config/keyboards/keyboard_layouts.qrc \
    config/languages/language.qrc


# Copy keyboard layout files to build directory
CONFIG += file_copies
COPIES += keyboard_layouts
keyboard_layouts.files = $$files($$PWD/config/keyboards/*.json)
keyboard_layouts.path = $$OUT_PWD/config/keyboards

COPIES += keyboard_layouts_debug
keyboard_layouts.files = $$files($$PWD/config/keyboards/*.json)
keyboard_layouts.path = $$OUT_PWD/debug/config/keyboards

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

# INSTALLS += target

RC_FILE = openterfaceQT.rc

DEPENDPATH += $$PWD/''

DEFINES += ONLINE_VERSION

TRANSLATIONS += config/languages/openterface_en.ts \
                config/languages/openterface_fr.ts \
                config/languages/openterface_da.ts \
                config/languages/openterface_ja.ts \
                config/languages/openterface_se.ts \
                config/languages/openterface_de.ts 
                # Add more languages here

#COPIES += translations
#translations.files = $$files($$PWD/config/languages/*.qm)
#translations.path = $$OUT_PWD/config/languages

#system($$QMAKE_MKDIR $$shell_path($$PWD/debug/config/languages))
#system($$QMAKE_MKDIR $$shell_path($$OUT_PWD/debug/config/languages))
