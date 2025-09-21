cmake_minimum_required(VERSION 3.16)
project(openterfaceQT VERSION 1.0 LANGUAGES C CXX)

# Option to control static vs dynamic linking
option(OPENTERFACE_BUILD_STATIC "Link libraries statically where possible" ON)
message(STATUS "OPENTERFACE_BUILD_STATIC: ${OPENTERFACE_BUILD_STATIC}")

# Detect architecture at top-level so users can override via -DOPENTERFACE_ARCH if desired
if(NOT DEFINED OPENTERFACE_ARCH)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(OPENTERFACE_ARCH "arm64" CACHE STRING "Openterface target architecture (arm64/amd64/other)")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
        set(OPENTERFACE_ARCH "amd64" CACHE STRING "Openterface target architecture (arm64/amd64/other)")
    else()
        set(OPENTERFACE_ARCH "${CMAKE_SYSTEM_PROCESSOR}" CACHE STRING "Openterface target architecture (arm64/amd64/other)")
    endif()
endif()

message(STATUS "OPENTERFACE_ARCH: ${OPENTERFACE_ARCH}")

# Convenience booleans derived from OPENTERFACE_ARCH for top-level logic and targets
if(NOT DEFINED OPENTERFACE_IS_ARM64)
    if(OPENTERFACE_ARCH STREQUAL "arm64")
        set(OPENTERFACE_IS_ARM64 TRUE CACHE BOOL "TRUE if target architecture is arm64")
    else()
        set(OPENTERFACE_IS_ARM64 FALSE CACHE BOOL "TRUE if target architecture is arm64")
    endif()
endif()

if(NOT DEFINED OPENTERFACE_IS_AMD64)
    if(OPENTERFACE_ARCH STREQUAL "amd64")
        set(OPENTERFACE_IS_AMD64 TRUE CACHE BOOL "TRUE if target architecture is amd64")
    else()
        set(OPENTERFACE_IS_AMD64 FALSE CACHE BOOL "TRUE if target architecture is amd64")
    endif()
endif()

message(STATUS "OPENTERFACE_IS_ARM64: ${OPENTERFACE_IS_ARM64}")
message(STATUS "OPENTERFACE_IS_AMD64: ${OPENTERFACE_IS_AMD64}")

include(cmake/Configuration.cmake)

# Provide FFmpeg helper functions (defines add_ffmpeg_static_libraries)
include(cmake/FFmpeg.cmake OPTIONAL)

# Include internationalization support (defines setup_translations function)
include(cmake/Internationalization.cmake)

# Find Qt early and set up project macros compatibly with Qt 6.2 on Ubuntu 22.04
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia MultimediaWidgets SerialPort OpenGL OpenGLWidgets Xml Svg SvgWidgets Concurrent DBus)

# Use standardized Qt setup if available (Qt >= 6.3); otherwise emulate basics for Qt 6.2
if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup()
else()
    message(STATUS "qt_standard_project_setup() not available; enabling Qt AUTOMOC/AUTOUIC/AUTORCC and C++17 manually")
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
endif()

# Include source files from modular file
include(cmake/SourceFiles.cmake)
qt_add_executable(openterfaceQT WIN32 ${SOURCE_FILES})

# Setup translations after target creation
setup_translations(openterfaceQT)

# Ensure target can find project headers (needed for includes like "serial/SerialPortManager.h" and "ui/statusevents.h")
target_include_directories(openterfaceQT PRIVATE
    ${PROJECT_SOURCE_DIR}
)

# Ensure Linux time feature macros are set per-target to avoid <ctime> visibility issues
if(UNIX AND NOT APPLE)
    # Define feature-test macros for the openterfaceQT target specifically
    target_compile_definitions(openterfaceQT PRIVATE
        _DEFAULT_SOURCE=1
        _GNU_SOURCE=1
        _XOPEN_SOURCE=700
    )
    # Also ensure POSIX source is not enforced
    target_compile_options(openterfaceQT PRIVATE -U_POSIX_C_SOURCE)
    
    # Force system time.h to be included before FFmpeg's time.h
    # FFmpeg libavutil/time.h conflicts with system time.h and breaks <ctime>
    # Use -include to force early inclusion of system time.h
    target_compile_options(openterfaceQT PRIVATE 
        -include /usr/include/time.h
    )
endif()


# Debug output for linking
message(STATUS "Hardware acceleration libraries to link: ${HWACCEL_LIBRARIES}")

# Link Qt6 image format plugins statically for static builds
if(OPENTERFACE_BUILD_STATIC)
    # Link Qt6 image format plugins statically
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        Qt6::QJpegPlugin 
        Qt6::QGifPlugin 
        Qt6::QICOPlugin 
        Qt6::QSvgPlugin
    )
    
    # Force static JPEG linking for Qt6 plugins - override system jpeg with our static version
    if(STATIC_JPEG_LIBRARIES)
        set_target_properties(Qt6::QJpegPlugin PROPERTIES
            INTERFACE_LINK_LIBRARIES "${STATIC_JPEG_LIBRARIES}"
        )
    endif()
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Gui Qt6::Network Qt6::Multimedia Qt6::MultimediaWidgets Qt6::SerialPort Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Xml Qt6::Svg Qt6::SvgWidgets Qt6::Concurrent Qt6::DBus)

# For static Qt builds, we need to link platform plugins and their dependencies
if(OPENTERFACE_BUILD_STATIC)
    # Link XCB platform plugin directly using file path
    set(QT_XCB_PLUGIN_PATH "/opt/Qt6/plugins/platforms/libqxcb.a")
    if(EXISTS ${QT_XCB_PLUGIN_PATH})
        target_link_libraries(${PROJECT_NAME} PRIVATE ${QT_XCB_PLUGIN_PATH})
        message(STATUS "Linking XCB platform plugin: ${QT_XCB_PLUGIN_PATH}")
    else()
        message(WARNING "XCB platform plugin not found at: ${QT_XCB_PLUGIN_PATH}")
    endif()

    # Link Offscreen platform plugin for headless environments
    set(QT_OFFSCREEN_PLUGIN_PATH "/opt/Qt6/plugins/platforms/libqoffscreen.a")
    if(EXISTS ${QT_OFFSCREEN_PLUGIN_PATH})
        target_link_libraries(${PROJECT_NAME} PRIVATE ${QT_OFFSCREEN_PLUGIN_PATH})
        message(STATUS "Linking Offscreen platform plugin: ${QT_OFFSCREEN_PLUGIN_PATH}")
    else()
        message(WARNING "Offscreen platform plugin not found at: ${QT_OFFSCREEN_PLUGIN_PATH}")
    endif()

    # Link Wayland platform plugin for Wayland sessions
    set(QT_WAYLAND_PLUGIN_EGL "/opt/Qt6/plugins/platforms/libqwayland-egl.a")
    set(QT_WAYLAND_PLUGIN "/opt/Qt6/plugins/platforms/libqwayland.a")
    if(EXISTS ${QT_WAYLAND_PLUGIN_EGL})
        target_link_libraries(${PROJECT_NAME} PRIVATE ${QT_WAYLAND_PLUGIN_EGL})
        message(STATUS "Linking Wayland platform plugin: ${QT_WAYLAND_PLUGIN_EGL}")
    elseif(EXISTS ${QT_WAYLAND_PLUGIN})
        target_link_libraries(${PROJECT_NAME} PRIVATE ${QT_WAYLAND_PLUGIN})
        message(STATUS "Linking Wayland platform plugin: ${QT_WAYLAND_PLUGIN}")
    else()
        message(WARNING "Wayland platform plugin not found under /opt/Qt6/plugins/platforms")
    endif()

    # Link Wayland client support libraries if available
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND_CLIENT wayland-client)
    if(WAYLAND_CLIENT_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${WAYLAND_CLIENT_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIRS})
        message(STATUS "Wayland client libs: ${WAYLAND_CLIENT_LIBRARIES}")
    endif()
    pkg_check_modules(WAYLAND_EGL wayland-egl)
    if(WAYLAND_EGL_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${WAYLAND_EGL_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PRIVATE ${WAYLAND_EGL_INCLUDE_DIRS})
        message(STATUS "Wayland EGL libs: ${WAYLAND_EGL_LIBRARIES}")
    endif()
    pkg_check_modules(XKBCOMMON xkbcommon)
    if(XKBCOMMON_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${XKBCOMMON_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PRIVATE ${XKBCOMMON_INCLUDE_DIRS})
    endif()

    # If Qt WaylandClient module exists in the static toolchain, link it
    find_package(Qt6 QUIET COMPONENTS WaylandClient)
    if(TARGET Qt6::WaylandClient)
        target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::WaylandClient)
        message(STATUS "Linking Qt6::WaylandClient module")
    endif()
    
    # Link Qt XCB QPA library (required by XCB plugin)
    set(QT_XCB_QPA_PATH "/opt/Qt6/lib/libQt6XcbQpa.a")
    if(EXISTS ${QT_XCB_QPA_PATH})
        target_link_libraries(${PROJECT_NAME} PRIVATE ${QT_XCB_QPA_PATH})
        message(STATUS "Linking Qt6 XCB QPA library: ${QT_XCB_QPA_PATH}")
    endif()
    
    # Force static linking of xcb-cursor with whole-archive to avoid dynamic dependency at runtime
    if(DEFINED XCB_CURSOR_LIBRARIES AND XCB_CURSOR_LIBRARIES)
        message(STATUS "Linking xcb-cursor statically: ${XCB_CURSOR_LIBRARIES}")
        target_link_libraries(${PROJECT_NAME} PRIVATE 
            -Wl,--whole-archive
            ${XCB_CURSOR_LIBRARIES}
            -Wl,--no-whole-archive
        )
    else()
        message(WARNING "Static xcb-cursor library not detected by Configuration.cmake; falling back may cause runtime dependency on libxcb-cursor.so.0")
        # Optionally enforce static-only:
        # message(FATAL_ERROR "Static build requested but libxcb-cursor.a not found. Ensure static libxcb-cursor is installed and detected.")
    endif()
    
    # Link other XCB static libraries normally
    set(XCB_STATIC_LIBS
        /usr/lib/aarch64-linux-gnu/libxcb-icccm.a
        /usr/lib/aarch64-linux-gnu/libxcb-image.a
        /usr/lib/aarch64-linux-gnu/libxcb-keysyms.a
        /usr/lib/aarch64-linux-gnu/libxcb-randr.a
        /usr/lib/aarch64-linux-gnu/libxcb-render-util.a
        /usr/lib/aarch64-linux-gnu/libxcb-shm.a
        /usr/lib/aarch64-linux-gnu/libxcb-sync.a
        /usr/lib/aarch64-linux-gnu/libxcb-xfixes.a
        /usr/lib/aarch64-linux-gnu/libxcb-render.a
        /usr/lib/aarch64-linux-gnu/libxcb-shape.a
        /usr/lib/aarch64-linux-gnu/libxcb-xkb.a
        /usr/lib/aarch64-linux-gnu/libxcb.a
        /usr/lib/aarch64-linux-gnu/libxcb-util.a
    )
    
    # Check if static libraries exist and link them
    foreach(XCB_LIB ${XCB_STATIC_LIBS})
        if(EXISTS ${XCB_LIB})
            target_link_libraries(${PROJECT_NAME} PRIVATE ${XCB_LIB})
            message(STATUS "Linking static XCB library: ${XCB_LIB}")
        else()
            message(WARNING "Static XCB library not found: ${XCB_LIB}")
        endif()
    endforeach()
    
    # Link additional dependencies needed by XCB plugins
    target_link_libraries(${PROJECT_NAME} PRIVATE xkbcommon xkbcommon-x11 Xau Xdmcp)
endif()

# Add XCB cursor library for static linking (Linux only) - Legacy support
if(UNIX AND NOT APPLE AND XCB_CURSOR_FOUND)
    # This is redundant now but keeping for compatibility
    message(STATUS "XCB cursor already linked via static approach above")
endif()

# Add TurboJPEG if available
if(TURBOJPEG_LIBRARY)
    target_link_libraries(openterfaceQT PRIVATE ${TURBOJPEG_LIBRARIES})
endif()

# Add FFmpeg libraries if available
link_ffmpeg_libraries()

# Add libjpeg libraries if available
if(LIBJPEG_FOUND)
    target_link_libraries(openterfaceQT PRIVATE ${LIBJPEG_LIBRARIES})
    target_include_directories(openterfaceQT PRIVATE ${LIBJPEG_INCLUDE_DIRS})
endif()

# Add GStreamer libraries if available
link_gstreamer_libraries()

# Add DBus on Unix systems (excluding macOS)
if(UNIX AND NOT APPLE)
    target_link_libraries(openterfaceQT PRIVATE Qt6::DBus)
endif()

# Resources:
include(cmake/Resources.cmake)

# Add these lines to enable size optimization
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Enable Link Time Optimization (but not for ARM64 to avoid segfaults)
    if(OPENTERFACE_IS_AMD64)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT LTO_ERROR)
        if(LTO_SUPPORTED)
            message(STATUS "IPO / LTO enabled")
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        else()
            message(STATUS "IPO / LTO not supported: ${LTO_ERROR}")
        endif()
        
        # Optimize for size (only for non-ARM64 builds)
        add_compile_options(-Os)
        message(STATUS "Using -Os optimization for non-ARM64 build")

        add_compile_options(-ffunction-sections -fdata-sections)
        message(STATUS "Using function/data sections for non-ARM64 build")
    else()
        message(STATUS "Skipping LTO and -Os optimization for ARM64 to prevent segfaults")
    endif()
    
    # Strip unused symbols
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        if(OPENTERFACE_IS_AMD64)
            add_link_options(-Wl,--gc-sections)
        else()
            message(STATUS "Skipping --gc-sections for ARM64 to prevent linker issues")
        endif()
    endif()
endif()

# Qt components already found above