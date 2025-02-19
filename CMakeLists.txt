cmake_minimum_required(VERSION 3.16)
project(openterfaceQT VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Concurrent Gui Multimedia MultimediaWidgets Network SerialPort Svg)
find_package(Qt${QT_VERSION_MAJOR} OPTIONAL_COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(openterfaceQT WIN32 MACOSX_BUNDLE
    global.h
    host/HostManager.cpp host/HostManager.h
    host/audiomanager.cpp host/audiomanager.h
    host/audiothread.cpp host/audiothread.h
    host/cameramanager.cpp host/cameramanager.h
    host/usbcontrol.cpp host/usbcontrol.h
    main.cpp
    regex/RegularExpression.cpp regex/RegularExpression.h
    resources/version.h
    scripts/KeyboardMouse.cpp scripts/KeyboardMouse.h
    scripts/Lexer.cpp scripts/Lexer.h
    scripts/Parser.cpp scripts/Parser.h
    scripts/semanticAnalyzer.cpp scripts/semanticAnalyzer.h
    serial/SerialPortManager.cpp serial/SerialPortManager.h
    serial/ch9329.h
    server/tcpServer.cpp server/tcpServer.h
    target/KeyboardLayouts.cpp target/KeyboardLayouts.h
    target/KeyboardManager.cpp target/KeyboardManager.h
    target/Keymapping.h
    target/MouseManager.cpp target/MouseManager.h
    target/mouseeventdto.cpp target/mouseeventdto.h
    ui/TaskManager.cpp ui/TaskManager.h
    ui/audiopage.cpp ui/audiopage.h
    ui/cameraajust.cpp ui/cameraajust.h
    ui/driverdialog.cpp ui/driverdialog.h ui/driverdialog.ui
    ui/fpsspinbox.cpp ui/fpsspinbox.h
    ui/globalsetting.cpp ui/globalsetting.h
    ui/hardwarepage.cpp ui/hardwarepage.h
    ui/helppane.cpp ui/helppane.h
    ui/inputhandler.cpp ui/inputhandler.h
    ui/loghandler.cpp ui/loghandler.h
    ui/logpage.cpp ui/logpage.h
    ui/mainwindow.cpp ui/mainwindow.h ui/mainwindow.ui
    ui/metadatadialog.cpp ui/metadatadialog.h
    ui/scripttool.cpp ui/scripttool.h
    ui/serialportdebugdialog.cpp ui/serialportdebugdialog.h
    ui/settingdialog.cpp ui/settingdialog.h ui/settingdialog.ui
    ui/statusbarmanager.cpp ui/statusbarmanager.h
    ui/statusevents.h
    ui/statuswidget.cpp ui/statuswidget.h
    ui/toggleswitch.cpp ui/toggleswitch.h
    ui/toolbarmanager.cpp ui/toolbarmanager.h
    ui/versioninfomanager.cpp ui/versioninfomanager.h
    ui/videopage.cpp ui/videopage.h
    ui/videopane.cpp ui/videopane.h
    video/videohid.cpp video/videohid.h
)
target_link_libraries(openterfaceQT PRIVATE
    Qt::Concurrent
    Qt::Core
    Qt::Gui
    Qt::Multimedia
    Qt::MultimediaWidgets
    Qt::Network
    Qt::SerialPort
    Qt::Svg
)

if(DEFINED LIBXRENDER_LIBRARY)
    message(STATUS "Using custom libXrender library: ${LIBXRENDER_LIBRARY}")
    target_link_libraries(openterfaceQT PRIVATE ${LIBXRENDER_LIBRARY})
endif()


# Resources:
set(mainwindow_resource_files
    "ui/../images/capture.svg"
    "ui/../images/content_dark_eng.png"
    "ui/../images/contrast.svg"
    "ui/../images/full_screen.svg"
    "ui/../images/fullscreen.svg"
    "ui/../images/icon_128.ico"
    "ui/../images/icon_128.png"
    "ui/../images/icon_32.ico"
    "ui/../images/icon_32.png"
    "ui/../images/icon_64.ico"
    "ui/../images/icon_64.png"
    "ui/../images/keyboard-down.svg"
    "ui/../images/keyboard-pressed.svg"
    "ui/../images/keyboard-up.svg"
    "ui/../images/keyboard.svg"
    "ui/../images/mouse-default.svg"
    "ui/../images/mouse-left-button.svg"
    "ui/../images/mouse-middle-button.svg"
    "ui/../images/mouse-right-button.svg"
    "ui/../images/paste.svg"
    "ui/../images/screensaver.svg"
    "ui/../images/shutter.svg"
    "ui/../images/zoom_fit.svg"
    "ui/../images/zoom_in.svg"
    "ui/../images/zoom_out.svg"
)

qt_add_resources(openterfaceQT "mainwindow"
    PREFIX
        "/"
    BASE
        "ui"
    FILES
        ${mainwindow_resource_files}
)
set(keyboard_layouts_resource_files
    "config/keyboards/azerty_fr.json"
    "config/keyboards/japanese.json"
    "config/keyboards/qwerty_dk.json"
    "config/keyboards/qwerty_uk.json"
    "config/keyboards/qwerty_us.json"
    "config/keyboards/qwertz_de.json"
)

qt_add_resources(openterfaceQT "keyboard_layouts"
    PREFIX
        "/config/keyboards"
    BASE
        "config/keyboards"
    FILES
        ${keyboard_layouts_resource_files}
)
set(qmake_immediate_resource_files
    "openterfaceQT.rc"
)

qt_add_resources(openterfaceQT "qmake_immediate"
    PREFIX
        "/"
    FILES
        ${qmake_immediate_resource_files}
)

if((QT_VERSION_MAJOR GREATER 4))
    target_link_libraries(openterfaceQT PRIVATE
        Qt::Widgets
    )
endif()

if(WIN32)
    target_include_directories(openterfaceQT PRIVATE
        lib
    )

    target_link_libraries(openterfaceQT PRIVATE
        # Remove: L${CMAKE_CURRENT_SOURCE_DIR}/lib
        hid
        libusb-1.0
        oleaut32
        setupapi
        winpthread
    )


    # Resources:
    set_source_files_properties("driver/windows/CH341SER.INF"
        PROPERTIES QT_RESOURCE_ALIAS "CH341SER.INF"
    )
    set(drivers_resource_files
        "driver/windows/CH341SER.INF"
    )

    qt_add_resources(openterfaceQT "drivers"
        PREFIX
            "/drivers/windows"
        BASE
            "driver/windows"
        FILES
            ${drivers_resource_files}
    )
endif()

if(UNIX)
    target_include_directories(openterfaceQT PRIVATE
        /usr/include
    )

    target_link_libraries(openterfaceQT PRIVATE
        usb-1.0
    )
endif()

install(TARGETS openterfaceQT
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET openterfaceQT
    FILENAME_VARIABLE deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
