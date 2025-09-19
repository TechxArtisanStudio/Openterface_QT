# Configuration.cmake - Platform and architecture detection/configuration

# Option to control USB functionality
option(USE_USB "Enable USB functionality via libusb" ON)

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)

# Detect ARM64 architecture for both cross-compilation and native builds
if(OPENTERFACE_IS_ARM64)    
    # Apply ARM64-specific optimizations for native builds
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        # Debug build: Use minimal optimization for faster compilation
        set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG" CACHE STRING "Debug flags" FORCE)
        set(CMAKE_C_FLAGS_DEBUG "-O0 -g -DDEBUG" CACHE STRING "Debug flags" FORCE)
        message(STATUS "Applied ARM64 native debug build optimizations (-O0)")
    else()
        # Release build: Use O1 for balance between speed and stability
        set(CMAKE_CXX_FLAGS_RELEASE "-O1 -DNDEBUG" CACHE STRING "Release flags" FORCE)
        set(CMAKE_C_FLAGS_RELEASE "-O1 -DNDEBUG" CACHE STRING "Release flags" FORCE)
        message(STATUS "Applied ARM64 native release build optimizations (-O1)")
    endif()
    
    # Essential stability flags for ARM64 native compilation (reduced)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-aggressive-loop-optimizations -ftemplate-depth-64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-aggressive-loop-optimizations")
    
    # Enable parallel compilation for native builds
    include(ProcessorCount)
    ProcessorCount(N)
    if(NOT N EQUAL 0)
        if(N GREATER 4)
            # Limit to 4 jobs on ARM64 to prevent memory issues
            set(PARALLEL_JOBS 4)
        else()
            set(PARALLEL_JOBS ${N})
        endif()
        message(STATUS "Using ${PARALLEL_JOBS} parallel compilation jobs for native ARM64")
    endif()
endif()

# Set QT_BUILD_PATH and adjust CMAKE_PREFIX_PATH when not explicitly provided
# Do not override user-provided CMAKE_PREFIX_PATH
if(NOT DEFINED QT_BUILD_PATH)
    if(DEFINED OPENTERFACE_IS_ARM64 AND OPENTERFACE_IS_ARM64)
        set(QT_BUILD_PATH "/opt/Qt6-arm64" CACHE PATH "Qt6 installation directory for builds" FORCE)
    else()
        set(QT_BUILD_PATH "/opt/Qt6" CACHE PATH "Qt6 installation directory for builds" FORCE)
    endif()
    message(STATUS "QT_BUILD_PATH set to ${QT_BUILD_PATH}")
endif()

if(NOT DEFINED CMAKE_PREFIX_PATH OR CMAKE_PREFIX_PATH STREQUAL "")
    # Initialize CMAKE_PREFIX_PATH to our chosen Qt build path
    set(CMAKE_PREFIX_PATH "${QT_BUILD_PATH}" CACHE PATH "Prefix path for CMake to find Qt" FORCE)
    message(STATUS "CMAKE_PREFIX_PATH set to ${CMAKE_PREFIX_PATH}")
else()
    message(STATUS "CMAKE_PREFIX_PATH already set: ${CMAKE_PREFIX_PATH}")
endif()


# Find pkg-config dependencies required by Qt components
find_package(PkgConfig REQUIRED)
pkg_check_modules(Libudev REQUIRED IMPORTED_TARGET libudev)

# Include FFmpeg configuration
include(cmake/FFmpeg.cmake)

# Include GStreamer configuration  
include(cmake/GStreamer.cmake)

# Ensure we use the static Qt6 build by setting the Qt6 directory explicitly
set(Qt6_DIR "${QT_BUILD_PATH}/lib/cmake/Qt6" CACHE PATH "Qt6 installation directory" FORCE)
set(Qt6Core_DIR "${QT_BUILD_PATH}/lib/cmake/Qt6Core" CACHE PATH "Qt6Core directory" FORCE)
set(Qt6Gui_DIR "${QT_BUILD_PATH}/lib/cmake/Qt6Gui" CACHE PATH "Qt6Gui directory" FORCE)
set(Qt6Widgets_DIR "${QT_BUILD_PATH}/lib/cmake/Qt6Widgets" CACHE PATH "Qt6Widgets directory" FORCE)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Concurrent Gui Multimedia MultimediaWidgets Network SerialPort Svg)
find_package(Qt${QT_VERSION_MAJOR} OPTIONAL_COMPONENTS Widgets)
if(UNIX AND NOT APPLE)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS DBus)
endif()

# Prioritize static Qt6 include directories over system Qt6 headers
# This prevents header conflicts between system Qt6 and static Qt6 build
include_directories(BEFORE SYSTEM 
    ${QT_BUILD_PATH}/include
    ${QT_BUILD_PATH}/include/QtCore
    ${QT_BUILD_PATH}/include/QtGui
    ${QT_BUILD_PATH}/include/QtWidgets
    ${QT_BUILD_PATH}/include/QtMultimedia
    ${QT_BUILD_PATH}/include/QtMultimediaWidgets
    ${QT_BUILD_PATH}/include/QtNetwork
    ${QT_BUILD_PATH}/include/QtSerialPort
    ${QT_BUILD_PATH}/include/QtSvg
    ${QT_BUILD_PATH}/include/QtConcurrent
    ${QT_BUILD_PATH}/include/QtDBus
)
# Exclude system Qt6 include directories from implicit includes
list(REMOVE_ITEM CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "/usr/include/aarch64-linux-gnu/qt6")
list(REMOVE_ITEM CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES "/usr/include/aarch64-linux-gnu/qt6")
message(STATUS "Prioritized static Qt6 include directories over system headers")



find_package(OpenGL REQUIRED COMPONENTS OpenGL)
# Try to find additional OpenGL components (optional for better compatibility)
find_package(OpenGL OPTIONAL_COMPONENTS GLX EGL)

# Find X11 libraries
find_package(X11 REQUIRED COMPONENTS Xrandr)

find_package(EXPAT REQUIRED)
find_package(Freetype REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(BZip2 REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(XRENDER REQUIRED xrender)

# Check for XCB cursor library (required for static linking)
pkg_check_modules(XCB_CURSOR REQUIRED xcb-cursor)
if(XCB_CURSOR_FOUND)
    message(STATUS "Found xcb-cursor: ${XCB_CURSOR_LIBRARIES}")
    message(STATUS "xcb-cursor include dirs: ${XCB_CURSOR_INCLUDE_DIRS}")
    message(STATUS "xcb-cursor library dirs: ${XCB_CURSOR_LIBRARY_DIRS}")
    
    # For static builds, ensure we link the static library
    if(OPENTERFACE_BUILD_STATIC)
        # Find the static library path
        find_library(XCB_CURSOR_STATIC_LIB 
            NAMES libxcb-cursor.a xcb-cursor
            PATHS ${XCB_CURSOR_LIBRARY_DIRS} /usr/local/lib /opt/Qt6/lib
            NO_DEFAULT_PATH
        )
        if(XCB_CURSOR_STATIC_LIB)
            set(XCB_CURSOR_LIBRARIES ${XCB_CURSOR_STATIC_LIB})
            message(STATUS "Using static xcb-cursor library: ${XCB_CURSOR_STATIC_LIB}")
        else()
            message(WARNING "Static xcb-cursor library not found, using dynamic")
        endif()
    endif()
else()
    message(WARNING "xcb-cursor not found - this may cause runtime linking issues")
endif()

# Check for additional XCB dependencies that xcb-cursor might need
pkg_check_modules(XCB_RENDER xcb-render)
pkg_check_modules(XCB_IMAGE xcb-image)
pkg_check_modules(XCB_XFIXES xcb-xfixes)




