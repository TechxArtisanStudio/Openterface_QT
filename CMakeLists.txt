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
if(UNIX AND NOT APPLE)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS DBus)
endif()

# Find X11 libraries
find_package(X11 REQUIRED COMPONENTS Xrandr)

find_package(EXPAT REQUIRED)
find_package(Freetype REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(BZip2 REQUIRED)

# Use pkg-config to find Xrender
find_package(PkgConfig REQUIRED)
pkg_check_modules(XRENDER REQUIRED xrender)

# Find additional packages required for static GStreamer linking
pkg_check_modules(GUDEV REQUIRED gudev-1.0)
pkg_check_modules(V4L2 REQUIRED libv4l2)

# Find X11 extension libraries
find_library(XI_LIBRARY Xi REQUIRED)
find_library(XV_LIBRARY Xv REQUIRED)

# Find ORC library for GStreamer
pkg_check_modules(ORC orc-0.4)

# Check for GStreamer (prefer static build from Qt6-arm64)
if(DEFINED QT_TARGET_DIR AND QT_TARGET_DIR)
    set(GSTREAMER_ROOT_DIR "${QT_TARGET_DIR}")
else()
    set(GSTREAMER_ROOT_DIR "/opt/Qt6-arm64")
endif()
set(GSTREAMER_INCLUDE_DIR "${GSTREAMER_ROOT_DIR}/include/gstreamer-1.0")
set(GSTREAMER_VIDEO_INCLUDE_DIR "${GSTREAMER_ROOT_DIR}/include/gstreamer-1.0")
set(GLIB_INCLUDE_DIR "${GSTREAMER_ROOT_DIR}/include/glib-2.0")
set(GLIB_CONFIG_INCLUDE_DIR "${GSTREAMER_ROOT_DIR}/lib/glib-2.0/include")

# Check if GStreamer headers exist in the static build location
if(EXISTS "${GSTREAMER_INCLUDE_DIR}/gst/gst.h" AND EXISTS "${GSTREAMER_VIDEO_INCLUDE_DIR}/gst/video/videooverlay.h")
    message(STATUS "GStreamer static build found - enabling direct pipeline support")
    add_definitions(-DHAVE_GSTREAMER)
    
    # Set GStreamer variables for static linking
    set(GSTREAMER_FOUND TRUE)
    set(GSTREAMER_VIDEO_FOUND TRUE)
    
    # Set include directories for static GStreamer
    set(GSTREAMER_INCLUDE_DIRS 
        "${GSTREAMER_INCLUDE_DIR}"
        "${GLIB_INCLUDE_DIR}"
        "${GLIB_CONFIG_INCLUDE_DIR}"
    )
    
    # Set static GStreamer libraries (using system GLib)
    set(GSTREAMER_LIBRARIES
        "${GSTREAMER_ROOT_DIR}/lib/libgstreamer-1.0.a"
        "${GSTREAMER_ROOT_DIR}/lib/libgstbase-1.0.a"
        "${GSTREAMER_ROOT_DIR}/lib/libgstcontroller-1.0.a"
        "${GSTREAMER_ROOT_DIR}/lib/libgstnet-1.0.a"
    )
    
    set(GSTREAMER_VIDEO_LIBRARIES
        "${GSTREAMER_ROOT_DIR}/lib/libgstvideo-1.0.a"
        "${GSTREAMER_ROOT_DIR}/lib/libgstaudio-1.0.a"
        "${GSTREAMER_ROOT_DIR}/lib/libgsttag-1.0.a"
        "${GSTREAMER_ROOT_DIR}/lib/libgstpbutils-1.0.a"
        "${GSTREAMER_ROOT_DIR}/lib/libgstapp-1.0.a"
        "${GSTREAMER_ROOT_DIR}/lib/libgstgl-1.0.a"
        "${GSTREAMER_ROOT_DIR}/lib/libgstriff-1.0.a"
        "${GSTREAMER_ROOT_DIR}/lib/libgstrtp-1.0.a"
        "${GSTREAMER_ROOT_DIR}/lib/libgstrtsp-1.0.a"
        "${GSTREAMER_ROOT_DIR}/lib/libgstsdp-1.0.a"
    )
    
    # Add static GStreamer plugin libraries that need to be linked
    set(GSTREAMER_PLUGIN_LIBRARIES
        "${GSTREAMER_ROOT_DIR}/lib/gstreamer-1.0/libgstvideo4linux2.a"  # v4l2src plugin
        "${GSTREAMER_ROOT_DIR}/lib/gstreamer-1.0/libgstcoreelements.a"  # queue, capsfilter, etc.
        "${GSTREAMER_ROOT_DIR}/lib/gstreamer-1.0/libgstvideoconvertscale.a"  # videoconvert, videoscale
        "${GSTREAMER_ROOT_DIR}/lib/gstreamer-1.0/libgsttypefindfunctions.a"  # typefind elements
        "${GSTREAMER_ROOT_DIR}/lib/gstreamer-1.0/libgstvideofilter.a"    # video filter base
        "${GSTREAMER_ROOT_DIR}/lib/gstreamer-1.0/libgstvideotestsrc.a"   # videotestsrc
        "${GSTREAMER_ROOT_DIR}/lib/gstreamer-1.0/libgstjpeg.a"           # jpegdec, jpegenc
        "${GSTREAMER_ROOT_DIR}/lib/gstreamer-1.0/libgstximagesink.a"     # ximagesink
        "${GSTREAMER_ROOT_DIR}/lib/gstreamer-1.0/libgstxvimagesink.a"    # xvimagesink
        "${GSTREAMER_ROOT_DIR}/lib/gstreamer-1.0/libgstautodetect.a"     # autovideosink
    )
    
    # Add system GLib libraries using pkg-config
    pkg_check_modules(GLIB_PKG REQUIRED glib-2.0 gobject-2.0 gio-2.0)
    if(GLIB_PKG_FOUND)
        list(APPEND GSTREAMER_LIBRARIES ${GLIB_PKG_LIBRARIES})
        list(APPEND GSTREAMER_INCLUDE_DIRS ${GLIB_PKG_INCLUDE_DIRS})
        message(STATUS "Using system GLib libraries: ${GLIB_PKG_LIBRARIES}")
    endif()
    
    # Add additional system libraries that GStreamer needs
    list(APPEND GSTREAMER_LIBRARIES
        z
        m
        pthread
        dl
        rt
        # Additional GStreamer dependencies for static linking
        ffi
        mount
        blkid
        resolv
        gmodule-2.0
        gobject-2.0
        glib-2.0
        pcre2-8
        orc-0.4
        # Required for v4l2 plugin static linking
        ${V4L2_LIBRARIES}  # Video4Linux2 utilities
        udev              # Device enumeration
        ${GUDEV_LIBRARIES} # GUdev for device monitoring
        # Required for X11 video sinks
        ${XI_LIBRARY}     # X11 Input extension
        ${XV_LIBRARY}     # X11 Video extension
    )
    
    message(STATUS "Using static GStreamer from: ${GSTREAMER_ROOT_DIR}")
    message(STATUS "GStreamer include dirs: ${GSTREAMER_INCLUDE_DIRS}")
else()
    # Fallback to system GStreamer using pkg-config
    pkg_check_modules(GSTREAMER gstreamer-1.0)
    if(GSTREAMER_FOUND)
        pkg_check_modules(GSTREAMER_VIDEO gstreamer-video-1.0)
        if(GSTREAMER_VIDEO_FOUND)
            message(STATUS "System GStreamer found - enabling direct pipeline support")
            add_definitions(-DHAVE_GSTREAMER)
            
            if(ORC_FOUND)
                message(STATUS "ORC library found - will link for GStreamer support")
            else()
                message(WARNING "ORC library not found - GStreamer may have linking issues")
            endif()

            # Also check for system GStreamer to ensure we have all headers
            pkg_check_modules(PC_GSTREAMER_VIDEO gstreamer-video-1.0 IMPORTED_TARGET)
            
            # Prefer system include directories for headers
            if(PC_GSTREAMER_VIDEO_FOUND)
                message(STATUS "Using system GStreamer video includes: ${PC_GSTREAMER_VIDEO_INCLUDE_DIRS}")
            endif()
        else()
            message(STATUS "GStreamer video not found - using fallback mode")
        endif()
    else()
        message(STATUS "GStreamer not found - using QProcess fallback")
    endif()
endif()

# Check for libjpeg-turbo (preferred for performance)
find_library(TURBOJPEG_LIBRARY turbojpeg)
find_path(TURBOJPEG_INCLUDE_DIR turbojpeg.h)

if(TURBOJPEG_LIBRARY AND TURBOJPEG_INCLUDE_DIR)
    message(STATUS "Found libjpeg-turbo: ${TURBOJPEG_LIBRARY}")
    add_definitions(-DHAVE_LIBJPEG_TURBO)
    set(TURBOJPEG_LIBRARIES ${TURBOJPEG_LIBRARY})
    include_directories(${TURBOJPEG_INCLUDE_DIR})
else()
    message(STATUS "libjpeg-turbo not found - JPEG acceleration disabled")
endif()

# Set FFmpeg include and library directories
if(NOT FFMPEG_INCLUDE_DIRS)
set(FFMPEG_INCLUDE_DIRS "/usr/local/include")
endif()
# Force use of our static FFmpeg libraries
set(FFMPEG_LIBRARIES 
    "/usr/local/lib/libavdevice.a"
    "/usr/local/lib/libavfilter.a"
    "/usr/local/lib/libavformat.a"
    "/usr/local/lib/libavcodec.a"
    "/usr/local/lib/libswresample.a"
    "/usr/local/lib/libswscale.a"
    "/usr/local/lib/libavutil.a"
)

# Add hardware acceleration libraries required by FFmpeg
set(HWACCEL_LIBRARIES
    va-drm
    va-x11
    va
    vdpau
    X11
    atomic
    pthread
    m
)

# Check if FFmpeg is available and enable it
if(EXISTS "/usr/local/include/libavformat/avformat.h")
    message(STATUS "FFmpeg found - enabling FFmpeg backend")
    add_definitions(-DHAVE_FFMPEG)
else()
    message(STATUS "FFmpeg not found - FFmpeg backend disabled")
endif()

# Include FFmpeg directories
include_directories(${FFMPEG_INCLUDE_DIRS})

# Add the executable target
qt_standard_project_setup()

qt_add_executable(openterfaceQT WIN32 MACOSX_BUNDLE
    global.h
    # Device management
    device/DeviceInfo.cpp device/DeviceInfo.h
    device/DeviceManager.cpp device/DeviceManager.h
    device/HotplugMonitor.cpp device/HotplugMonitor.h
    device/platform/AbstractPlatformDeviceManager.cpp device/platform/AbstractPlatformDeviceManager.h
    device/platform/DeviceFactory.cpp device/platform/DeviceFactory.h
    # Host management
    host/HostManager.cpp host/HostManager.h
    host/audiomanager.cpp host/audiomanager.h
    host/audiothread.cpp host/audiothread.h
    host/cameramanager.cpp host/cameramanager.h
    host/usbcontrol.cpp host/usbcontrol.h
    host/multimediabackend.cpp host/multimediabackend.h
    host/backend/ffmpegbackendhandler.cpp host/backend/ffmpegbackendhandler.h
    host/backend/gstreamerbackendhandler.cpp host/backend/gstreamerbackendhandler.h
    host/backend/qtmultimediabackendhandler.cpp host/backend/qtmultimediabackendhandler.h
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
    ui/advance/DeviceSelectorDialog.cpp ui/advance/DeviceSelectorDialog.h
    ui/advance/envdialog.cpp ui/advance/envdialog.h ui/advance/envdialog.ui
    ui/statusbar/statusbarmanager.cpp ui/statusbar/statusbarmanager.h
    ui/help/versioninfomanager.cpp ui/help/versioninfomanager.h
    ui/statusevents.h
    ui/screenscale.h ui/screenscale.cpp
    ui/cornerwidget/cornerwidgetmanager.h ui/cornerwidget/cornerwidgetmanager.cpp
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
    ui/advance/firmwaremanagerdialog.cpp ui/advance/firmwaremanagerdialog.h
    ui/advance/renamedisplaydialog.cpp ui/advance/renamedisplaydialog.h
    ui/advance/updatedisplaysettingsdialog.cpp ui/advance/updatedisplaysettingsdialog.h
    video/firmwarewriter.cpp video/firmwarewriter.h
    video/firmwarereader.cpp video/firmwarereader.h
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
    ${FFMPEG_LIBRARIES}  # Link against FFmpeg libraries
    ${HWACCEL_LIBRARIES} # Link against hardware acceleration libraries
    ${X11_LIBRARIES}     # Link against X11 libraries
    ${EXPAT_LIBRARIES}
    ${FREETYPE_LIBRARIES}
    ${FONTCONFIG_LIBRARIES}
    ${XRENDER_LIBRARIES}
    ${BZIP2_LIBRARIES}
    /opt/orc-static/lib/aarch64-linux-gnu/liborc-0.4.a  # Static ORC library
    ${EXTRA_LIBS}
)

# Add TurboJPEG if available
if(TURBOJPEG_LIBRARY)
    target_link_libraries(openterfaceQT PRIVATE ${TURBOJPEG_LIBRARIES})
endif()

# Add libjpeg libraries if available
if(LIBJPEG_FOUND)
    target_link_libraries(openterfaceQT PRIVATE ${LIBJPEG_LIBRARIES})
    target_include_directories(openterfaceQT PRIVATE ${LIBJPEG_INCLUDE_DIRS})
endif()

# Add GStreamer libraries if available
if(GSTREAMER_FOUND AND GSTREAMER_VIDEO_FOUND)
    if(EXISTS "${GSTREAMER_ROOT_DIR}/include/gstreamer-1.0/gst/gst.h")
        # Add static ORC include path and library path
        target_include_directories(openterfaceQT PRIVATE 
            ${GSTREAMER_INCLUDE_DIRS}
            /opt/orc-static/include/orc-0.4
            ${GUDEV_INCLUDE_DIRS}
            ${V4L2_INCLUDE_DIRS}
        )
        
        # Static GStreamer linking with proper library grouping
        target_link_libraries(openterfaceQT PRIVATE 
            -Wl,--whole-archive
            /opt/orc-static/lib/aarch64-linux-gnu/liborc-0.4.a
            -Wl,--no-whole-archive
            -Wl,--start-group
            ${GSTREAMER_LIBRARIES}
            ${GSTREAMER_VIDEO_LIBRARIES}
            ${GSTREAMER_PLUGIN_LIBRARIES}
            -Wl,--end-group
            -lm -pthread
        )
        
        message(STATUS "Added static GStreamer libraries and include directories")
    else()
        # System GStreamer linking
        target_link_libraries(openterfaceQT PRIVATE 
            ${GSTREAMER_LIBRARIES}
            ${GSTREAMER_VIDEO_LIBRARIES}
        )
        
        # Use system include directories for better header compatibility
        if(PC_GSTREAMER_VIDEO_FOUND)
            target_include_directories(openterfaceQT PRIVATE 
                ${PC_GSTREAMER_VIDEO_INCLUDE_DIRS}
            )
            message(STATUS "Added system GStreamer video include directories")
        else()
            target_include_directories(openterfaceQT PRIVATE 
                ${GSTREAMER_INCLUDE_DIRS}
                ${GSTREAMER_VIDEO_INCLUDE_DIRS}
            )
        endif()
    endif()
        
    # Add ORC library if available (required by GStreamer video components)
    if(ORC_FOUND)
        target_link_libraries(openterfaceQT PRIVATE ${ORC_LIBRARIES})
        target_include_directories(openterfaceQT PRIVATE ${ORC_INCLUDE_DIRS})
    endif()
endif()

# Add DBus on Unix systems (excluding macOS)
if(UNIX AND NOT APPLE)
    target_link_libraries(openterfaceQT PRIVATE Qt::DBus)
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
    "ui/../images/screen_scale.svg"
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
    "config/languages/openterface_zh.qm"
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
    # Add Windows-specific device manager
    target_sources(openterfaceQT PRIVATE
        device/platform/WindowsDeviceManager.cpp device/platform/WindowsDeviceManager.h
    )
    
    target_include_directories(openterfaceQT PRIVATE
        lib
    )

    target_link_libraries(openterfaceQT PRIVATE
        hid
        libusb-1.0
        ole32
        oleaut32
        setupapi
        cfgmgr32
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
    # Add Linux-specific device manager
    target_sources(openterfaceQT PRIVATE
        device/platform/LinuxDeviceManager.cpp device/platform/LinuxDeviceManager.h
    )
    
    target_include_directories(openterfaceQT PRIVATE
        /usr/include
        /usr/local/include
    )

    target_link_libraries(openterfaceQT PRIVATE
        usb-1.0
    )
    
    # Add libudev dependency for enhanced Linux device detection
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UDEV REQUIRED libudev)
    if(UDEV_FOUND)
        target_link_libraries(openterfaceQT PRIVATE ${UDEV_LIBRARIES})
        target_include_directories(openterfaceQT PRIVATE ${UDEV_INCLUDE_DIRS})
        target_compile_definitions(openterfaceQT PRIVATE HAVE_LIBUDEV)
        target_compile_options(openterfaceQT PRIVATE ${UDEV_CFLAGS_OTHER})
        target_link_directories(openterfaceQT PRIVATE ${UDEV_LIBRARY_DIRS})
    endif()

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