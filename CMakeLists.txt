cmake_minimum_required(VERSION 3.16)
project(openterfaceQT VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)

# Option to control static linking of FFmpeg
option(USE_FFMPEG_STATIC "Use static FFmpeg libraries" ON)

# Prefer static libraries
if(USE_FFMPEG_STATIC)
    set(CMAKE_FIND_STATIC_PREFER ON)
endif()

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Concurrent Gui Multimedia MultimediaWidgets Network SerialPort Svg)
find_package(Qt${QT_VERSION_MAJOR} OPTIONAL_COMPONENTS Widgets)

# Find X11 libraries
find_package(X11 REQUIRED COMPONENTS Xrandr)

find_package(EXPAT REQUIRED)
find_package(Freetype REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(BZip2 REQUIRED)

# Use pkg-config to find Xrender
find_package(PkgConfig REQUIRED)
pkg_check_modules(XRENDER REQUIRED xrender)

# Set FFmpeg include and library directories
if(NOT FFMPEG_INCLUDE_DIRS)
set(FFMPEG_INCLUDE_DIRS "/usr/local/include")
endif()
if(NOT FFMPEG_LIBRARIES)
set(FFMPEG_LIBRARIES "/usr/local/lib/libavformat.a;/usr/local/lib/libavcodec.a;/usr/local/lib/libavutil.a;/usr/local/lib/libswresample.a;/usr/local/lib/libswscale.a")
endif()

# Include FFmpeg directories
include_directories(${FFMPEG_INCLUDE_DIRS})

# Add the executable target
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
    scripts/scriptEditor.cpp scripts/scriptEditor.h
    serial/SerialPortManager.cpp serial/SerialPortManager.h
    serial/ch9329.h
    server/tcpServer.cpp server/tcpServer.h
    target/KeyboardLayouts.cpp target/KeyboardLayouts.h
    target/KeyboardManager.cpp target/KeyboardManager.h
    target/Keymapping.h
    target/MouseManager.cpp target/MouseManager.h
    target/mouseeventdto.cpp target/mouseeventdto.h
    video/videohid.cpp video/videohid.h
    ui/TaskManager.cpp ui/TaskManager.h
    ui/globalsetting.cpp ui/globalsetting.h
    ui/help/helppane.cpp ui/help/helppane.h
    ui/inputhandler.cpp ui/inputhandler.h
    ui/loghandler.cpp ui/loghandler.h
    ui/mainwindow.cpp ui/mainwindow.h ui/mainwindow.ui
    ui/advance/scripttool.cpp ui/advance/scripttool.h
    ui/advance/serialportdebugdialog.cpp ui/advance/serialportdebugdialog.h
    ui/advance/envdialog.cpp ui/advance/envdialog.h ui/advance/envdialog.ui
    ui/statusbar/statusbarmanager.cpp ui/statusbar/statusbarmanager.h
    ui/help/versioninfomanager.cpp ui/help/versioninfomanager.h
    ui/statusevents.h
    ui/statusbar/statuswidget.cpp ui/statusbar/statuswidget.h
    ui/toolbar/toggleswitch.cpp ui/toolbar/toggleswitch.h
    ui/toolbar/toolbarmanager.cpp ui/toolbar/toolbarmanager.h
    ui/preferences/cameraadjust.cpp ui/preferences/cameraadjust.h
    ui/preferences/fpsspinbox.cpp ui/preferences/fpsspinbox.h
    ui/preferences/settingdialog.cpp ui/preferences/settingdialog.h ui/preferences/settingdialog.ui
    ui/preferences/logpage.cpp ui/preferences/logpage.h
    ui/preferences/videopage.cpp ui/preferences/videopage.h
    ui/preferences/audiopage.cpp ui/preferences/audiopage.h
    ui/preferences/targetcontrolpage.cpp ui/preferences/targetcontrolpage.h
    ui/videopane.cpp ui/videopane.h
    ui/languagemanager.cpp ui/languagemanager.h
    ui/screensavermanager.cpp ui/screensavermanager.h
    ui/advance/firmwareupdatedialog.cpp ui/advance/firmwareupdatedialog.h
    video/firmwarewriter.cpp video/firmwarewriter.h
    video/ms2109.h
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
    Qt::DBus
    ${FFMPEG_LIBRARIES}  # Link against FFmpeg libraries
    ${EXPAT_LIBRARIES}
    ${FREETYPE_LIBRARIES}
    ${FONTCONFIG_LIBRARIES}
    ${XRENDER_LIBRARIES}
    ${BZIP2_LIBRARIES}
    ${EXTRA_LIBS}
)

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

set(languages_resources_files
    "config/languages/openterface_da.qm"
    "config/languages/openterface_de.qm"
    "config/languages/openterface_en.qm"
    "config/languages/openterface_fr.qm"
    "config/languages/openterface_ja.qm"
    "config/languages/openterface_se.qm"
)

qt_add_resources(openterfaceQT "languages"
    PREFIX
        "/config/languages"
    BASE
        "config/languages"
    FILES
        ${languages_resources_files}
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

set(app_icons_resource_files
    "images/icon_128.png"
    "images/icon_64.png"
    "images/icon_32.png"
)

qt_add_resources(openterfaceQT "app_icons"
    PREFIX
        "/icons"
    FILES
        ${app_icons_resource_files}
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
        /usr/local/include
    )

    target_link_libraries(openterfaceQT PRIVATE
        usb-1.0
    )

    set(drivers_resource_files
        "driver/linux/ch341.c"
        "driver/linux/ch341.h"
        "driver/linux/Makefile"
    )

    qt_add_resources(openterfaceQT "drivers"
        PREFIX
            "/drivers/linux"
        BASE
            "driver/linux"
        FILES
            ${drivers_resource_files}
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

# Add these lines to enable size optimization
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Enable Link Time Optimization
    include(CheckIPOSupported)
    check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT LTO_ERROR)
    if(LTO_SUPPORTED)
        message(STATUS "IPO / LTO enabled")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "IPO / LTO not supported: ${LTO_ERROR}")
    endif()
    
    # Optimize for size
    add_compile_options(-Os)
    
    # Remove unused functions and data
    add_compile_options(-ffunction-sections -fdata-sections)
    
    # Strip unused symbols
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_link_options(-Wl,--gc-sections)
    endif()
endif()

# Make sure to only include the necessary Qt components
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia MultimediaWidgets)
# Replace with only the modules you actually need