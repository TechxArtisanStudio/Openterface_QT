# Resources.cmake - Resource management
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
    "ui/../image/laptop.svg"
    "ui/../image/monitor.svg"
    "ui/../image/usbplug.svg"
)

qt_add_resources(openterfaceQT "mainwindow"
    PREFIX
        "/"
    BASE
        "ui"
    FILES
        ${mainwindow_resource_files}
    OPTIONS
        --compress-algo none
)

set(keyboard_layouts_resource_files
    "config/keyboards/azerty_fr.json"
    "config/keyboards/azerty_be.json"
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
    OPTIONS
        --compress-algo none
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
    OPTIONS
        --compress-algo none
)

set(qmake_immediate_resource_files
    "openterfaceQT.rc"
)

qt_add_resources(openterfaceQT "qmake_immediate"
    PREFIX
        "/"
    FILES
        ${qmake_immediate_resource_files}
    OPTIONS
        --compress-algo none
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
    OPTIONS
        --compress-algo none
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

    # Find libusb-1.0 library for Windows
    if(USE_USB)
        find_library(LIBUSB_LIBRARY 
            NAMES libusb-1.0 usb-1.0
            PATHS ${CMAKE_CURRENT_SOURCE_DIR}/lib
        )
        
        if(LIBUSB_LIBRARY)
            message(STATUS "Found libusb-1.0 for Windows: ${LIBUSB_LIBRARY}")
            target_link_libraries(openterfaceQT PRIVATE
                hid
                ${LIBUSB_LIBRARY}
                ole32
                oleaut32
                setupapi
                cfgmgr32
                winpthread
            )
        else()
            message(WARNING "libusb-1.0 not found for Windows - trying with default name")
            target_link_libraries(openterfaceQT PRIVATE
                hid
                libusb-1.0
                ole32
                oleaut32
                setupapi
                cfgmgr32
                winpthread
            )
        endif()
    else()
        message(STATUS "USB functionality disabled by USE_USB=OFF")
        target_link_libraries(openterfaceQT PRIVATE
            hid
            ole32
            oleaut32
            setupapi
            cfgmgr32
            winpthread
        )
    endif()

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
        OPTIONS
            --compress-algo none
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

    # Find libusb-1.0 library
    if(USE_USB)
        # For static builds, force static libusb libraries
        if(USE_FFMPEG_STATIC OR BUILD_STATIC)
            # Force static libusb linking for static builds
            find_library(LIBUSB_STATIC_LIBRARY 
                NAMES libusb-1.0.a usb-1.0.a
                PATHS ${FFMPEG_PREFIX}/lib /usr/lib /usr/lib/aarch64-linux-gnu /usr/local/lib ${QT_BUILD_PATH}/lib
                NO_DEFAULT_PATH
            )
            
            if(LIBUSB_STATIC_LIBRARY)
                message(STATUS "Found static libusb-1.0: ${LIBUSB_STATIC_LIBRARY}")
                # Use whole-archive to ensure all libusb symbols are included
                target_link_libraries(openterfaceQT PRIVATE 
                    -Wl,--whole-archive
                    ${LIBUSB_STATIC_LIBRARY}
                    -Wl,--no-whole-archive
                )
                # Add required system dependencies for static libusb
                target_link_libraries(openterfaceQT PRIVATE udev pthread)
            else()
                message(WARNING "Static libusb-1.0 not found - falling back to dynamic linking")
                # Fall back to dynamic search
                find_library(LIBUSB_LIBRARY 
                    NAMES usb-1.0 libusb-1.0
                    PATHS ${FFMPEG_PREFIX}/lib /usr/lib /usr/local/lib
                    PATH_SUFFIXES x86_64-linux-gnu aarch64-linux-gnu arm-linux-gnueabihf
                )
                if(LIBUSB_LIBRARY)
                    message(STATUS "Found dynamic libusb-1.0: ${LIBUSB_LIBRARY}")
                    target_link_libraries(openterfaceQT PRIVATE ${LIBUSB_LIBRARY})
                endif()
            endif()
        else()
            # Dynamic build - search for dynamic libraries
            find_library(LIBUSB_LIBRARY 
                NAMES usb-1.0 libusb-1.0
                PATHS ${FFMPEG_PREFIX}/lib /usr/lib /usr/local/lib
                PATH_SUFFIXES x86_64-linux-gnu aarch64-linux-gnu arm-linux-gnueabihf
            )
            
            if(LIBUSB_LIBRARY)
                message(STATUS "Found libusb-1.0: ${LIBUSB_LIBRARY}")
                target_link_libraries(openterfaceQT PRIVATE ${LIBUSB_LIBRARY})
            else()
                message(WARNING "libusb-1.0 not found - USB functionality may be limited")
                message(STATUS "Searched paths: ${FFMPEG_PREFIX}/lib, /usr/lib, /usr/local/lib")
                # Try to link with the simple name as fallback
                target_link_libraries(openterfaceQT PRIVATE usb-1.0)
            endif()
        endif()
    else()
        message(STATUS "USB functionality disabled by USE_USB=OFF")
    endif()
    
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
        OPTIONS
            --compress-algo none
    )
endif()

install(TARGETS openterfaceQT
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install application icons for different sizes
install(FILES ${CMAKE_SOURCE_DIR}/images/icon_32.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/32x32/apps
    RENAME com.openterface.openterfaceQT.png
)

install(FILES ${CMAKE_SOURCE_DIR}/images/icon_64.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/64x64/apps
    RENAME com.openterface.openterfaceQT.png
)

install(FILES ${CMAKE_SOURCE_DIR}/images/icon_128.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps
    RENAME com.openterface.openterfaceQT.png
)

install(FILES ${CMAKE_SOURCE_DIR}/images/icon_256.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    RENAME com.openterface.openterfaceQT.png
)

install(FILES ${CMAKE_SOURCE_DIR}/images/icon_256.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME com.openterface.openterfaceQT.svg
)

# Install desktop file
install(FILES ${CMAKE_SOURCE_DIR}/com.openterface.openterfaceQT.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

# Install metainfo file (for AppStream)
install(FILES ${CMAKE_SOURCE_DIR}/com.openterface.openterfaceQT.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)

# Guard deploy script generation for Qt < 6.3 on Ubuntu 22.04
if(COMMAND qt_generate_deploy_app_script)
    qt_generate_deploy_app_script(
        TARGET openterfaceQT
        FILENAME_VARIABLE deploy_script    
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_script})
else()
    message(STATUS "qt_generate_deploy_app_script not available; skipping deploy script generation on this Qt version")
endif()
