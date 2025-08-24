cmake_minimum_required(VERSION 3.16)
project(openterfaceQT VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)

# Option to control static linking of FFmpeg
option(USE_FFMPEG_STATIC "Use static FFmpeg libraries" ON)

# Option to control hardware acceleration libraries
option(USE_HWACCEL "Enable hardware acceleration libraries (VA-API, VDPAU)" ON)

# Option to control USB functionality
option(USE_USB "Enable USB functionality via libusb" ON)

# Option to control GStreamer support
option(USE_GSTREAMER "Enable GStreamer multimedia backend" ON)

# Option to disable static GStreamer plugins (for debugging)
# Note: Static plugins are embedded in the binary for standalone deployment
# If static plugins are not found, the application will use system plugins at runtime
# This requires GStreamer to be installed on the target system
# If you encounter linking errors with static plugins, try setting this to OFF
option(USE_GSTREAMER_STATIC_PLUGINS "Link GStreamer plugins statically" ON)

# Prefer static libraries
if(USE_FFMPEG_STATIC)
    set(CMAKE_FIND_STATIC_PREFER ON)
endif()

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)

# Cross-compilation support: Set architecture-specific library paths
# This ensures that find_package commands work correctly for ARM64 cross-compilation
if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross-compilation detected:")
    message(STATUS "  Host system: ${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}")
    message(STATUS "  Target system: ${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
    
    # Determine architecture-specific library directory
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(ARCH_LIB_DIR "aarch64-linux-gnu")
        set(ARCH_SPECIFIC_PATH "/usr/lib/aarch64-linux-gnu")
        message(STATUS "  Configuring for ARM64 architecture")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
        set(ARCH_LIB_DIR "x86_64-linux-gnu")
        set(ARCH_SPECIFIC_PATH "/usr/lib/x86_64-linux-gnu")
        message(STATUS "  Configuring for AMD64 architecture")
    else()
        message(WARNING "Unknown target architecture: ${CMAKE_SYSTEM_PROCESSOR}")
        set(ARCH_LIB_DIR "")
        set(ARCH_SPECIFIC_PATH "/usr/lib")
    endif()
    
    # Set hints for find_package to locate libraries in cross-compilation environment
    if(ARCH_LIB_DIR)
        # OpenGL libraries
        set(OPENGL_INCLUDE_DIR "/usr/include" CACHE PATH "OpenGL include directory")
        set(OPENGL_EGL_INCLUDE_DIR "/usr/include/EGL" CACHE PATH "EGL include directory")
        set(OPENGL_GLX_INCLUDE_DIR "/usr/include/GL" CACHE PATH "GLX include directory")
        set(OPENGL_opengl_LIBRARY "${ARCH_SPECIFIC_PATH}/libOpenGL.so" CACHE FILEPATH "OpenGL library")
        set(OPENGL_glx_LIBRARY "${ARCH_SPECIFIC_PATH}/libGLX.so" CACHE FILEPATH "GLX library")
        set(OPENGL_egl_LIBRARY "${ARCH_SPECIFIC_PATH}/libEGL.so" CACHE FILEPATH "EGL library")
        set(OPENGL_glu_LIBRARY "${ARCH_SPECIFIC_PATH}/libGLU.so" CACHE FILEPATH "GLU library")
        
        # Vulkan headers
        set(Vulkan_INCLUDE_DIR "/usr/include/vulkan" CACHE PATH "Vulkan include directory")
        
        # X11 libraries
        set(X11_INCLUDE_DIR "/usr/include/X11" CACHE PATH "X11 include directory")
        set(X11_X11_INCLUDE_PATH "/usr/include" CACHE PATH "X11 include path")
        set(X11_X11_LIB "${ARCH_SPECIFIC_PATH}/libX11.so" CACHE FILEPATH "X11 library")
        set(X11_Xrandr_INCLUDE_PATH "/usr/include/X11/extensions" CACHE PATH "Xrandr include path")
        set(X11_Xrandr_LIB "${ARCH_SPECIFIC_PATH}/libXrandr.so" CACHE FILEPATH "Xrandr library")
        set(X11_Xext_LIB "${ARCH_SPECIFIC_PATH}/libXext.so" CACHE FILEPATH "Xext library")
        
        # EXPAT library
        set(EXPAT_INCLUDE_DIR "/usr/include" CACHE PATH "EXPAT include directory")
        set(EXPAT_LIBRARY "${ARCH_SPECIFIC_PATH}/libexpat.so" CACHE FILEPATH "EXPAT library")
        
        # Freetype library
        set(FREETYPE_INCLUDE_DIRS "/usr/include/freetype2" CACHE PATH "Freetype include directories")
        set(FREETYPE_LIBRARY "${ARCH_SPECIFIC_PATH}/libfreetype.so" CACHE FILEPATH "Freetype library")
        set(FREETYPE_INCLUDE_DIR_freetype2 "/usr/include/freetype2" CACHE PATH "Freetype2 include directory")
        set(FREETYPE_INCLUDE_DIR_ft2build "/usr/include/freetype2" CACHE PATH "ft2build include directory")
        
        # Fontconfig library
        set(Fontconfig_INCLUDE_DIR "/usr/include" CACHE PATH "Fontconfig include directory")
        set(Fontconfig_LIBRARY "${ARCH_SPECIFIC_PATH}/libfontconfig.so" CACHE FILEPATH "Fontconfig library")
        set(FONTCONFIG_INCLUDE_DIR "/usr/include" CACHE PATH "Fontconfig include directory (legacy)")
        set(FONTCONFIG_LIBRARY "${ARCH_SPECIFIC_PATH}/libfontconfig.so" CACHE FILEPATH "Fontconfig library (legacy)")
        
        # BZip2 library
        set(BZIP2_INCLUDE_DIR "/usr/include" CACHE PATH "BZip2 include directory")
        set(BZIP2_LIBRARIES "${ARCH_SPECIFIC_PATH}/libbz2.so" CACHE FILEPATH "BZip2 library")
        
        # Qt6 CMake module paths
        set(Qt6_DIR "${ARCH_SPECIFIC_PATH}/cmake/Qt6" CACHE PATH "Qt6 CMake directory")
        set(Qt6Core_DIR "${ARCH_SPECIFIC_PATH}/cmake/Qt6Core" CACHE PATH "Qt6Core CMake directory")
        set(Qt6Gui_DIR "${ARCH_SPECIFIC_PATH}/cmake/Qt6Gui" CACHE PATH "Qt6Gui CMake directory")
        set(Qt6Widgets_DIR "${ARCH_SPECIFIC_PATH}/cmake/Qt6Widgets" CACHE PATH "Qt6Widgets CMake directory")
        set(Qt6Multimedia_DIR "${ARCH_SPECIFIC_PATH}/cmake/Qt6Multimedia" CACHE PATH "Qt6Multimedia CMake directory")
        set(Qt6MultimediaWidgets_DIR "${ARCH_SPECIFIC_PATH}/cmake/Qt6MultimediaWidgets" CACHE PATH "Qt6MultimediaWidgets CMake directory")
        
        # Add architecture-specific paths to CMAKE search paths
        list(INSERT CMAKE_LIBRARY_PATH 0 "${ARCH_SPECIFIC_PATH}")
        list(INSERT CMAKE_INCLUDE_PATH 0 "/usr/include" "/usr/include/freetype2" "/usr/include/EGL" "/usr/include/GL" "/usr/include/vulkan")
        
        # Set additional CMake search paths for cross-compilation
        list(INSERT CMAKE_FIND_ROOT_PATH 0 "/usr/${ARCH_LIB_DIR}")
        set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
        set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
        
        # Set pkg-config for cross-compilation
        set(PKG_CONFIG_EXECUTABLE "/usr/bin/pkg-config" CACHE FILEPATH "pkg-config executable")
        set(ENV{PKG_CONFIG_PATH} "/usr/lib/${ARCH_LIB_DIR}/pkgconfig:/usr/share/pkgconfig")
        set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/${ARCH_LIB_DIR}/pkgconfig")
        
        message(STATUS "  Cross-compilation library paths configured for ${ARCH_LIB_DIR}")
        message(STATUS "  PKG_CONFIG_PATH: $ENV{PKG_CONFIG_PATH}")
        message(STATUS "  PKG_CONFIG_LIBDIR: $ENV{PKG_CONFIG_LIBDIR}")
    endif()
endif()

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Concurrent Gui Multimedia MultimediaWidgets Network SerialPort Svg)
find_package(Qt${QT_VERSION_MAJOR} OPTIONAL_COMPONENTS Widgets)
if(UNIX AND NOT APPLE)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS DBus)
endif()

# Find OpenGL libraries (required by Qt6)
if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross-compilation: Looking for OpenGL libraries in ${ARCH_SPECIFIC_PATH}")
    message(STATUS "  OPENGL_EGL_INCLUDE_DIR: ${OPENGL_EGL_INCLUDE_DIR}")
    message(STATUS "  OPENGL_GLX_INCLUDE_DIR: ${OPENGL_GLX_INCLUDE_DIR}")
    message(STATUS "  Vulkan_INCLUDE_DIR: ${Vulkan_INCLUDE_DIR}")
endif()

find_package(OpenGL REQUIRED COMPONENTS OpenGL)
# Try to find additional OpenGL components (optional for better compatibility)
find_package(OpenGL OPTIONAL_COMPONENTS GLX EGL)

# Find X11 libraries
find_package(X11 REQUIRED COMPONENTS Xrandr)

find_package(EXPAT REQUIRED)
find_package(Freetype REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(BZip2 REQUIRED)

# Use pkg-config to find Xrender
# First ensure pkg-config is properly configured for cross-compilation
if(CMAKE_CROSSCOMPILING)
    # Make sure pkg-config executable is found
    find_program(PKG_CONFIG_EXECUTABLE pkg-config REQUIRED)
    if(PKG_CONFIG_EXECUTABLE)
        message(STATUS "Found pkg-config: ${PKG_CONFIG_EXECUTABLE}")
    endif()
endif()

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

# Check for GStreamer (prefer static build from Qt6 installation)
# Use the same search paths as FFmpeg since they're often built together
set(GSTREAMER_SEARCH_PATHS 
    "/opt/Qt6"
    "/opt/Qt6-arm64"
    "/usr/local"
    "/usr"
)

# Also check if QT_TARGET_DIR was set via environment or CMake
if(DEFINED QT_TARGET_DIR AND QT_TARGET_DIR)
    list(INSERT GSTREAMER_SEARCH_PATHS 0 "${QT_TARGET_DIR}")
endif()

# Check CMAKE_PREFIX_PATH for potential GStreamer locations
if(CMAKE_PREFIX_PATH)
    foreach(PREFIX_PATH ${CMAKE_PREFIX_PATH})
        list(INSERT GSTREAMER_SEARCH_PATHS 0 "${PREFIX_PATH}")
    endforeach()
endif()

# Find GStreamer installation
set(GSTREAMER_ROOT_DIR "/opt/Qt6")  # Default fallback
foreach(SEARCH_PATH ${GSTREAMER_SEARCH_PATHS})
    if(EXISTS "${SEARCH_PATH}/include/gstreamer-1.0/gst/gst.h")
        set(GSTREAMER_ROOT_DIR "${SEARCH_PATH}")
        message(STATUS "Found GStreamer installation at: ${GSTREAMER_ROOT_DIR}")
        break()
    endif()
endforeach()

set(GSTREAMER_INCLUDE_DIR "${GSTREAMER_ROOT_DIR}/include/gstreamer-1.0")
set(GSTREAMER_VIDEO_INCLUDE_DIR "${GSTREAMER_ROOT_DIR}/include/gstreamer-1.0")
set(GLIB_INCLUDE_DIR "${GSTREAMER_ROOT_DIR}/include/glib-2.0")
set(GLIB_CONFIG_INCLUDE_DIR "${GSTREAMER_ROOT_DIR}/lib/glib-2.0/include")

if(USE_GSTREAMER AND EXISTS "${GSTREAMER_INCLUDE_DIR}/gst/gst.h" AND EXISTS "${GSTREAMER_VIDEO_INCLUDE_DIR}/gst/video/videooverlay.h")
    message(STATUS "GStreamer static build found - enabling direct pipeline support")
    add_definitions(-DHAVE_GSTREAMER)
    
    # Define linking mode based on static plugin usage
    if(USE_GSTREAMER_STATIC_PLUGINS)
        message(STATUS "Static GStreamer plugins requested")
        message(WARNING "Static plugin linking often causes issues - forcing dynamic linking for now")
        message(STATUS "Will use dynamic GStreamer plugins at runtime")
        add_definitions(-DGSTREAMER_DYNAMIC_LINKING)
        set(USE_GSTREAMER_STATIC_PLUGINS OFF)
    else()
        message(STATUS "Using dynamic GStreamer plugins - disabling static plugin registration")
        add_definitions(-DGSTREAMER_DYNAMIC_LINKING)
    endif()
    
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
    set(GSTREAMER_LIBRARIES)
    
    # List of core GStreamer libraries to check
    set(GSTREAMER_CORE_LIB_CANDIDATES
        "libgstreamer-1.0.a"
        "libgstbase-1.0.a"
        "libgstcontroller-1.0.a"
        "libgstnet-1.0.a"
        "libgstallocators-1.0.a"  # Contains dmabuf and fd allocator functions
    )
    
    # Check for each core library and add if found
    foreach(LIB_NAME ${GSTREAMER_CORE_LIB_CANDIDATES})
        set(LIB_PATH "${GSTREAMER_ROOT_DIR}/lib/${LIB_NAME}")
        # Also check architecture-specific subdirectories
        set(LIB_PATH_ARCH "${GSTREAMER_ROOT_DIR}/lib/x86_64-linux-gnu/${LIB_NAME}")
        set(LIB_PATH_AARCH64 "${GSTREAMER_ROOT_DIR}/lib/aarch64-linux-gnu/${LIB_NAME}")
        
        if(EXISTS "${LIB_PATH}")
            list(APPEND GSTREAMER_LIBRARIES "${LIB_PATH}")
            message(STATUS "Found GStreamer core library: ${LIB_PATH}")
        elseif(EXISTS "${LIB_PATH_ARCH}")
            list(APPEND GSTREAMER_LIBRARIES "${LIB_PATH_ARCH}")
            message(STATUS "Found GStreamer core library: ${LIB_PATH_ARCH}")
        elseif(EXISTS "${LIB_PATH_AARCH64}")
            list(APPEND GSTREAMER_LIBRARIES "${LIB_PATH_AARCH64}")
            message(STATUS "Found GStreamer core library: ${LIB_PATH_AARCH64}")
        else()
            message(WARNING "GStreamer core library not found: ${LIB_PATH}")
        endif()
    endforeach()
    
    set(GSTREAMER_VIDEO_LIBRARIES)
    
    # List of potential GStreamer video libraries to check
    set(GSTREAMER_VIDEO_LIB_CANDIDATES
        "libgstvideo-1.0.a"
        "libgstaudio-1.0.a"
        "libgsttag-1.0.a"           # Contains tag utilities like gst_tag_list_to_vorbiscomment_buffer
        "libgstpbutils-1.0.a"       # Playback utilities
        "libgstapp-1.0.a"
        "libgstgl-1.0.a"
        "libgstriff-1.0.a"
        "libgstrtp-1.0.a"
        "libgstrtsp-1.0.a"
        "libgstsdp-1.0.a"
        "libgstcodecparsers-1.0.a"  # Codec parsers (may contain bit/byte readers)
    )
    
    # Check for each library and add if found
    foreach(LIB_NAME ${GSTREAMER_VIDEO_LIB_CANDIDATES})
        set(LIB_PATH "${GSTREAMER_ROOT_DIR}/lib/${LIB_NAME}")
        # Also check architecture-specific subdirectories
        set(LIB_PATH_ARCH "${GSTREAMER_ROOT_DIR}/lib/x86_64-linux-gnu/${LIB_NAME}")
        set(LIB_PATH_AARCH64 "${GSTREAMER_ROOT_DIR}/lib/aarch64-linux-gnu/${LIB_NAME}")
        
        if(EXISTS "${LIB_PATH}")
            list(APPEND GSTREAMER_VIDEO_LIBRARIES "${LIB_PATH}")
            message(STATUS "Found GStreamer video library: ${LIB_PATH}")
        elseif(EXISTS "${LIB_PATH_ARCH}")
            list(APPEND GSTREAMER_VIDEO_LIBRARIES "${LIB_PATH_ARCH}")
            message(STATUS "Found GStreamer video library: ${LIB_PATH_ARCH}")
        elseif(EXISTS "${LIB_PATH_AARCH64}")
            list(APPEND GSTREAMER_VIDEO_LIBRARIES "${LIB_PATH_AARCH64}")
            message(STATUS "Found GStreamer video library: ${LIB_PATH_AARCH64}")
        else()
            message(WARNING "GStreamer video library not found: ${LIB_PATH}")
        endif()
    endforeach()
    
    # Add static GStreamer plugin libraries that need to be linked
    set(GSTREAMER_PLUGIN_LIBRARIES)
    
    if(USE_GSTREAMER_STATIC_PLUGINS)
        # First, let's check what plugin directories exist
        set(PLUGIN_SEARCH_DIRS
            "${GSTREAMER_ROOT_DIR}/lib/gstreamer-1.0"
            "${GSTREAMER_ROOT_DIR}/lib/x86_64-linux-gnu/gstreamer-1.0"
            "${GSTREAMER_ROOT_DIR}/lib/aarch64-linux-gnu/gstreamer-1.0"
            "/usr/lib/x86_64-linux-gnu/gstreamer-1.0"
            "/usr/lib/aarch64-linux-gnu/gstreamer-1.0"
            "/usr/local/lib/gstreamer-1.0"
        )
        
        message(STATUS "Searching for GStreamer plugins in:")
        foreach(PLUGIN_DIR ${PLUGIN_SEARCH_DIRS})
            if(EXISTS "${PLUGIN_DIR}")
                message(STATUS "  - ${PLUGIN_DIR} (exists)")
                file(GLOB AVAILABLE_PLUGINS "${PLUGIN_DIR}/lib*.a")
                if(AVAILABLE_PLUGINS)
                    message(STATUS "    Found static plugins: ${AVAILABLE_PLUGINS}")
                endif()
            else()
                message(STATUS "  - ${PLUGIN_DIR} (not found)")
            endif()
        endforeach()
        
        # List of plugins to try to find
        # Essential plugins - core functionality
        set(ESSENTIAL_PLUGIN_CANDIDATES
            "gstvideo4linux2"     # v4l2src plugin - required for video input
            "gstcoreelements"     # queue, capsfilter, etc. - essential elements
            "gstjpeg"             # JPEG codec support - often required
        )
        
        # Optional plugins - enhanced functionality
        set(OPTIONAL_PLUGIN_CANDIDATES
            "gstvideoconvertscale" # videoconvert, videoscale
            "gsttypefindfunctions" # typefind elements
            "gstvideofilter"      # video filter base
            "gstvideotestsrc"     # videotestsrc
            "gstximagesink"       # ximagesink
            "gstxvimagesink"      # xvimagesink
            "gstautodetect"       # autovideosink
            "gstplayback"         # playback elements
            "gstdmabuf"          # DMA buffer support
            "gstalloc"           # Memory allocators
        )
        
        # Combine all plugins for searching
        set(PLUGIN_CANDIDATES ${ESSENTIAL_PLUGIN_CANDIDATES} ${OPTIONAL_PLUGIN_CANDIDATES})
        
        # Check for each plugin and add if found
        foreach(PLUGIN ${PLUGIN_CANDIDATES})
            # Try multiple possible locations for GStreamer plugins
            set(PLUGIN_PATH_CANDIDATES
                "${GSTREAMER_ROOT_DIR}/lib/gstreamer-1.0/lib${PLUGIN}.a"
                "${GSTREAMER_ROOT_DIR}/lib/x86_64-linux-gnu/gstreamer-1.0/lib${PLUGIN}.a"
                "${GSTREAMER_ROOT_DIR}/lib/aarch64-linux-gnu/gstreamer-1.0/lib${PLUGIN}.a"
                "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/lib${PLUGIN}.a"
                "/usr/lib/aarch64-linux-gnu/gstreamer-1.0/lib${PLUGIN}.a"
                "/usr/local/lib/gstreamer-1.0/lib${PLUGIN}.a"
            )
            
            set(PLUGIN_FOUND FALSE)
            foreach(PLUGIN_PATH ${PLUGIN_PATH_CANDIDATES})
                if(EXISTS "${PLUGIN_PATH}")
                    list(APPEND GSTREAMER_PLUGIN_LIBRARIES "${PLUGIN_PATH}")
                    message(STATUS "Found GStreamer plugin: ${PLUGIN_PATH}")
                    set(PLUGIN_FOUND TRUE)
                    break()
                endif()
            endforeach()
            
            if(NOT PLUGIN_FOUND)
                message(WARNING "GStreamer plugin not found: ${PLUGIN} (searched multiple locations)")
            endif()
        endforeach()
        
        if(NOT GSTREAMER_PLUGIN_LIBRARIES)
            message(WARNING "No GStreamer static plugins found - disabling static plugin linking")
            message(STATUS "Will rely on system GStreamer plugins at runtime")
            set(USE_GSTREAMER_STATIC_PLUGINS OFF)
        else()
            list(LENGTH GSTREAMER_PLUGIN_LIBRARIES PLUGIN_COUNT)
            message(STATUS "Found ${PLUGIN_COUNT} GStreamer static plugins for linking:")
            foreach(FOUND_PLUGIN ${GSTREAMER_PLUGIN_LIBRARIES})
                message(STATUS "  - ${FOUND_PLUGIN}")
            endforeach()
        endif()
    else()
        message(STATUS "GStreamer static plugins disabled by USE_GSTREAMER_STATIC_PLUGINS=OFF")
        message(STATUS "Will rely on system GStreamer plugins at runtime")
    endif()
    
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
    if(USE_GSTREAMER)
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
    else()
        message(STATUS "GStreamer disabled by USE_GSTREAMER=OFF")
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
# Try to find FFmpeg in Qt6 installation first, then fallback to system locations
set(FFMPEG_SEARCH_PATHS 
    "/opt/Qt6"
    "/usr/local"
    "/usr"
)

# Also check if FFMPEG_PREFIX was set via CMAKE_PREFIX_PATH or environment
if(DEFINED ENV{FFMPEG_PREFIX})
    list(INSERT FFMPEG_SEARCH_PATHS 0 "$ENV{FFMPEG_PREFIX}")
endif()

# Check CMAKE_PREFIX_PATH for potential FFmpeg locations
if(CMAKE_PREFIX_PATH)
    foreach(PREFIX_PATH ${CMAKE_PREFIX_PATH})
        list(INSERT FFMPEG_SEARCH_PATHS 0 "${PREFIX_PATH}")
    endforeach()
endif()

# Find FFmpeg installation
foreach(SEARCH_PATH ${FFMPEG_SEARCH_PATHS})
    if(EXISTS "${SEARCH_PATH}/lib/libavformat.a" AND EXISTS "${SEARCH_PATH}/include/libavformat/avformat.h")
        set(FFMPEG_PREFIX "${SEARCH_PATH}")
        message(STATUS "Found FFmpeg installation at: ${FFMPEG_PREFIX}")
        break()
    endif()
endforeach()

# Set default if not found
if(NOT FFMPEG_PREFIX)
    set(FFMPEG_PREFIX "/usr/local")
    message(WARNING "FFmpeg not found in standard locations, defaulting to: ${FFMPEG_PREFIX}")
endif()

# Set include and library directories based on found prefix
set(FFMPEG_INCLUDE_DIRS "${FFMPEG_PREFIX}/include")
set(FFMPEG_LIBRARIES 
    "${FFMPEG_PREFIX}/lib/libavdevice.a"
    "${FFMPEG_PREFIX}/lib/libavfilter.a"
    "${FFMPEG_PREFIX}/lib/libavformat.a"
    "${FFMPEG_PREFIX}/lib/libavcodec.a"
    "${FFMPEG_PREFIX}/lib/libswresample.a"
    "${FFMPEG_PREFIX}/lib/libswscale.a"
    "${FFMPEG_PREFIX}/lib/libavutil.a"
)

# Add hardware acceleration libraries required by FFmpeg (optional)
set(HWACCEL_LIBRARIES)

if(USE_HWACCEL)
    # Check for hardware acceleration libraries and add them if available
    find_library(VADRM_LIB va-drm)
    find_library(VAX11_LIB va-x11) 
    find_library(VA_LIB va)
    find_library(VDPAU_LIB vdpau)

    if(VADRM_LIB)
        list(APPEND HWACCEL_LIBRARIES ${VADRM_LIB})
        message(STATUS "Found VA-DRM library: ${VADRM_LIB}")
    else()
        message(STATUS "VA-DRM library not found - hardware acceleration may be limited")
    endif()

    if(VAX11_LIB)
        list(APPEND HWACCEL_LIBRARIES ${VAX11_LIB})
        message(STATUS "Found VA-X11 library: ${VAX11_LIB}")
    else()
        message(STATUS "VA-X11 library not found - hardware acceleration may be limited")
    endif()

    if(VA_LIB)
        list(APPEND HWACCEL_LIBRARIES ${VA_LIB})
        message(STATUS "Found VA library: ${VA_LIB}")
    else()
        message(STATUS "VA library not found - hardware acceleration may be limited")
    endif()

    if(VDPAU_LIB)
        list(APPEND HWACCEL_LIBRARIES ${VDPAU_LIB})
        message(STATUS "Found VDPAU library: ${VDPAU_LIB}")
    else()
        message(STATUS "VDPAU library not found - hardware acceleration may be limited")
    endif()
else()
    message(STATUS "Hardware acceleration disabled by USE_HWACCEL=OFF")
endif()

# Always include these essential libraries
list(APPEND HWACCEL_LIBRARIES
    X11
    atomic
    pthread
    m
)

# Check if FFmpeg is available and enable it
if(EXISTS "${FFMPEG_PREFIX}/include/libavformat/avformat.h")
    message(STATUS "FFmpeg found - enabling FFmpeg backend")
    message(STATUS "FFmpeg include directory: ${FFMPEG_INCLUDE_DIRS}")
    message(STATUS "FFmpeg libraries: ${FFMPEG_LIBRARIES}")
    add_definitions(-DHAVE_FFMPEG)
else()
    message(STATUS "FFmpeg not found - FFmpeg backend disabled")
    message(STATUS "Checked path: ${FFMPEG_PREFIX}/include/libavformat/avformat.h")
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

# Debug output for linking
message(STATUS "Hardware acceleration libraries to link: ${HWACCEL_LIBRARIES}")

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
        
        # Static GStreamer linking - plugins will be loaded dynamically at runtime
        # Find the correct ORC library path
        set(ORC_LIB_CANDIDATES
            "/opt/orc-static/lib/x86_64-linux-gnu/liborc-0.4.a"
            "/opt/orc-static/lib/aarch64-linux-gnu/liborc-0.4.a"
            "/opt/orc-static/lib/liborc-0.4.a"
            "/usr/lib/x86_64-linux-gnu/liborc-0.4.a"
            "/usr/lib/aarch64-linux-gnu/liborc-0.4.a"
        )
        
        set(ORC_LIB "")
        foreach(ORC_PATH ${ORC_LIB_CANDIDATES})
            if(EXISTS "${ORC_PATH}")
                set(ORC_LIB "${ORC_PATH}")
                message(STATUS "Found ORC library: ${ORC_LIB}")
                break()
            endif()
        endforeach()
        
        # Link GStreamer core libraries statically, plugins will be loaded dynamically
        target_link_libraries(openterfaceQT PRIVATE 
            -Wl,--start-group
            ${GSTREAMER_LIBRARIES}
            ${GSTREAMER_VIDEO_LIBRARIES}
            -Wl,--end-group
            ${ORC_LIB}
            -lm -pthread
        )
        message(STATUS "Added static GStreamer core libraries (plugins will be loaded dynamically)")
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
        # Search in the same locations where we found FFmpeg, since libusb may be built there too
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