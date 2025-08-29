# FFmpeg.cmake - FFmpeg configuration and detection


# Initialize FFmpeg configuration variables
set(FFMPEG_PKG_CONFIG FALSE)

# Option to control hardware acceleration libraries
option(USE_HWACCEL "Enable hardware acceleration libraries (VA-API, VDPAU)" ON)

# Prefer static libraries
if(OPENTERFACE_BUILD_STATIC)
    set(CMAKE_FIND_STATIC_PREFER ON)
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
# For dynamic builds, prioritize system libraries; for static builds, prefer Qt6 installation
if(OPENTERFACE_BUILD_STATIC)
    set(FFMPEG_SEARCH_PATHS 
        ${QT_BUILD_PATH}
        "/usr/local"
        "/usr"
    )
else()
    # For dynamic builds, prioritize system packages
    set(FFMPEG_SEARCH_PATHS 
        "/usr"
        "/usr/local"
        ${QT_BUILD_PATH}
    )
endif()

# Attempt to locate FFmpeg libraries
if(OPENTERFACE_BUILD_STATIC)
    # Prefer FFmpeg shipped inside the Qt build tree if it actually exists there.
    set(_qt_lib_dir "${QT_BUILD_PATH}/lib")
    if(EXISTS "${_qt_lib_dir}/libavformat.a" AND EXISTS "${QT_BUILD_PATH}/include/avformat.h")
        set(FFMPEG_LIB_DIR ${_qt_lib_dir})
        set(FFMPEG_INCLUDE_DIRS "${QT_BUILD_PATH}/include")
        message(STATUS "Found FFmpeg static libraries in Qt build path: ${FFMPEG_LIB_DIR}")
        set(FFMPEG_FOUND TRUE)
    else()
        # Keep the previous behavior as a fallback (directory may be validated later)
        set(FFMPEG_LIB_DIR ${_qt_lib_dir})
        set(FFMPEG_INCLUDE_DIRS "${QT_BUILD_PATH}/include")
        message(STATUS "FFmpeg static libs not found at ${_qt_lib_dir} - will try other search methods")
    endif()
else()
    # Check if Qt build tree provides dynamic FFmpeg first; if so, prefer it and skip pkg-config
    set(_qt_lib_dir "${QT_BUILD_PATH}/lib")
    if(EXISTS "${_qt_lib_dir}/libavformat.so" AND EXISTS "${QT_BUILD_PATH}/include/avformat.h")
        set(FFMPEG_LIB_DIR ${_qt_lib_dir})
        message(STATUS "Found FFmpeg shared libraries in Qt build path: ${FFMPEG_LIB_DIR}")
        set(FFMPEG_FOUND TRUE)
    endif()

    # If not already found in Qt build tree, use pkg-config as before
    if(NOT FFMPEG_FOUND)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            message(STATUS "pkg-config found, checking for FFmpeg...")
            pkg_check_modules(PC_LIBAVFORMAT QUIET libavformat)
            if(PC_LIBAVFORMAT_FOUND)
                message(STATUS "Found FFmpeg via pkg-config")
                message(STATUS "pkg-config FFmpeg version: ${PC_LIBAVFORMAT_VERSION}")
                message(STATUS "pkg-config FFmpeg include dirs: ${PC_LIBAVFORMAT_INCLUDE_DIRS}")
                message(STATUS "pkg-config FFmpeg library dirs: ${PC_LIBAVFORMAT_LIBRARY_DIRS}")
                message(STATUS "pkg-config FFmpeg libraries: ${PC_LIBAVFORMAT_LIBRARIES}")

                # Use pkg-config library directories if available, otherwise fallback
                if(PC_LIBAVFORMAT_LIBRARY_DIRS)
                    list(GET PC_LIBAVFORMAT_LIBRARY_DIRS 0 FFMPEG_LIB_DIR)
                    message(STATUS "Using pkg-config FFmpeg library directory: ${FFMPEG_LIB_DIR}")
                else()
                    # Architecture-aware fallback for library directory
                    set(FFMPEG_LIB_DIR ${QT_BUILD_PATH}/lib)
                    message(STATUS "Using fallback FFmpeg library directory: ${FFMPEG_LIB_DIR}")
                endif()
                set(FFMPEG_FOUND TRUE)
                set(FFMPEG_PKG_CONFIG TRUE)  # Flag to indicate we used pkg-config
                # Override library paths with pkg-config results
                set(FFMPEG_INCLUDE_DIRS ${PC_LIBAVFORMAT_INCLUDE_DIRS})
                message(STATUS "Using pkg-config FFmpeg: ${PC_LIBAVFORMAT_LIBRARIES}")
            else()
                message(STATUS "FFmpeg not found via pkg-config")
            endif()
        else()
            message(STATUS "pkg-config not found")
        endif()
    else()
        message(STATUS "Skipping pkg-config: FFmpeg already found in Qt build path")
    endif()
endif()

# If pkg-config didn't find FFmpeg (or we're using static linking), fall back to path search
if(NOT FFMPEG_FOUND)
    message(STATUS "Falling back to manual path search for FFmpeg...")
    
    # Find FFmpeg installation
    message(STATUS "FFmpeg search paths: ${FFMPEG_SEARCH_PATHS}")
    foreach(SEARCH_PATH ${FFMPEG_SEARCH_PATHS})
        # Standard search - but also check architecture-specific subdirectories for dynamic builds
        if(OPENTERFACE_BUILD_STATIC)
            set(LIB_EXTENSION ".a")
            set(LIB_NAME "libavformat.a")
            set(LIB_PATHS "${SEARCH_PATH}/lib")
        else()
            set(LIB_EXTENSION ".so")
            set(LIB_NAME "libavformat.so")
            # For dynamic builds, check architecture-specific directories
            set(LIB_PATHS 
                "${SEARCH_PATH}/lib/x86_64-linux-gnu"
                "${SEARCH_PATH}/lib/aarch64-linux-gnu"
                "${SEARCH_PATH}/lib"
            )
        endif()
        
        # Check each potential library path
        foreach(LIB_PATH ${LIB_PATHS})
            message(STATUS "Checking for FFmpeg in: ${LIB_PATH}/${LIB_NAME}")
            if(EXISTS "${LIB_PATH}/${LIB_NAME}" AND EXISTS "${SEARCH_PATH}/include/libavformat/avformat.h")
                set(FFMPEG_LIB_DIR "${LIB_PATH}")
                message(STATUS "FFmpeg libraries in: ${FFMPEG_LIB_DIR}")
                message(STATUS "Using ${LIB_EXTENSION} libraries")
                set(FFMPEG_FOUND TRUE)
                break()
            endif()
        endforeach()
        
        if(FFMPEG_FOUND)
            break()
        endif()
    endforeach()
endif()

# FFmpeg configuration complete - show final status
if(FFMPEG_FOUND)
    message(STATUS "FFmpeg configuration successful")
    message(STATUS "FFmpeg library directory: ${FFMPEG_LIB_DIR}")
    message(STATUS "FFmpeg include directory: ${FFMPEG_INCLUDE_DIRS}")
    if(FFMPEG_PKG_CONFIG)
        message(STATUS "FFmpeg detected via: pkg-config")
    else()
        message(STATUS "FFmpeg detected via: manual path search")
    endif()
else()
    message(WARNING "FFmpeg configuration failed - FFmpeg backend will be disabled")
endif()

# Set include and library directories based on found prefix
if(FFMPEG_PKG_CONFIG AND FFMPEG_INCLUDE_DIRS)
    # Use pkg-config include directories if available
    message(STATUS "Using pkg-config include directories: ${FFMPEG_INCLUDE_DIRS}")
endif()

# Set library extension based on static/dynamic preference
if(OPENTERFACE_BUILD_STATIC)
    set(FFMPEG_LIB_EXT ".a")
else()
    set(FFMPEG_LIB_EXT ".so")
endif()

# Use full paths for static linking - CRITICAL: avdevice must be first
set(FFMPEG_LIBRARIES 
    "${FFMPEG_LIB_DIR}/libavdevice${FFMPEG_LIB_EXT}"
    "${FFMPEG_LIB_DIR}/libavfilter${FFMPEG_LIB_EXT}"
    "${FFMPEG_LIB_DIR}/libavformat${FFMPEG_LIB_EXT}"
    "${FFMPEG_LIB_DIR}/libavcodec${FFMPEG_LIB_EXT}"
    "${FFMPEG_LIB_DIR}/libswresample${FFMPEG_LIB_EXT}"
    "${FFMPEG_LIB_DIR}/libswscale${FFMPEG_LIB_EXT}"
    "${FFMPEG_LIB_DIR}/libavutil${FFMPEG_LIB_EXT}"
)
message(STATUS "Using FFmpeg library paths: ${FFMPEG_LIBRARIES}")

# Verify all FFmpeg libraries exist
foreach(FFMPEG_LIB ${FFMPEG_LIBRARIES})
    if(EXISTS "${FFMPEG_LIB}")
        message(STATUS "✓ Found: ${FFMPEG_LIB}")
    else()
        message(FATAL_ERROR "✗ Missing: ${FFMPEG_LIB}")
    endif()
endforeach()

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
set(FFMPEG_CHECK_FILE "${FFMPEG_INCLUDE_DIRS}/avformat.h")

if(OPENTERFACE_BUILD_STATIC)
    set(FFMPEG_LIB_CHECK "${FFMPEG_LIB_DIR}/libavformat.a")
else()
    set(FFMPEG_LIB_CHECK "${FFMPEG_LIB_DIR}/libavformat.so")
endif()

# If we found FFmpeg via pkg-config, trust it and skip file checks
# Otherwise, verify files exist manually
message(STATUS "Debug: FFMPEG_PKG_CONFIG = ${FFMPEG_PKG_CONFIG}")
message(STATUS "Debug: FFMPEG_CHECK_FILE = ${FFMPEG_CHECK_FILE}")
message(STATUS "Debug: FFMPEG_LIB_CHECK = ${FFMPEG_LIB_CHECK}")


if(FFMPEG_PKG_CONFIG OR (EXISTS "${FFMPEG_CHECK_FILE}" AND EXISTS "${FFMPEG_LIB_CHECK}"))
    message(STATUS "FFmpeg found - enabling FFmpeg backend")
    if(FFMPEG_PKG_CONFIG)
        message(STATUS "FFmpeg verified via pkg-config")
    else()
        message(STATUS "FFmpeg verified via file system check")
        message(STATUS "Header file found: ${FFMPEG_CHECK_FILE}")
        message(STATUS "Library file found: ${FFMPEG_LIB_CHECK}")
    endif()
    message(STATUS "FFmpeg include directory: ${FFMPEG_INCLUDE_DIRS}")
    message(STATUS "FFmpeg libraries: ${FFMPEG_LIBRARIES}")
    add_definitions(-DHAVE_FFMPEG)
else()
    message(STATUS "FFmpeg not found - FFmpeg backend disabled")
    message(STATUS "Checked header: ${FFMPEG_CHECK_FILE} (exists: ${EXISTS_HEADER})")
    message(STATUS "Checked library: ${FFMPEG_LIB_CHECK} (exists: ${EXISTS_LIB})")
    message(FATAL_ERROR "FFmpeg is required but not found. Please ensure FFmpeg libraries are installed at ${CMAKE_PREFIX_PATH}")
endif()

# Include FFmpeg directories
include_directories(${FFMPEG_INCLUDE_DIRS})


# Public helper: add FFmpeg static libraries to a target
# Usage: add_ffmpeg_static_libraries(<target> <ffmpeg_root>)
function(add_ffmpeg_static_libraries _target _ffmpeg_root)
    if(NOT _target)
        message(FATAL_ERROR "add_ffmpeg_static_libraries: target argument is required")
    endif()
    if(NOT _ffmpeg_root)
        message(FATAL_ERROR "add_ffmpeg_static_libraries: ffmpeg_root argument is required")
    endif()

    # Resolve paths
    set(_ff_inc "${_ffmpeg_root}/include/avformat.h")
    set(_libdir "${_ffmpeg_root}/lib")

    if(EXISTS "${_ff_inc}")
        message(STATUS "FFmpeg headers found at ${_ff_inc}; adding static FFmpeg libs for target ${_target}")

        # Link order: ensure avdevice is linked with --whole-archive so avdevice_register_all is pulled in
        target_link_libraries(${_target} PRIVATE
            -Wl,--whole-archive
            "${_libdir}/libavdevice.a"
            -Wl,--no-whole-archive
            "${_libdir}/libavfilter.a"
            "${_libdir}/libavformat.a"
            "${_libdir}/libavcodec.a"
            "${_libdir}/libswresample.a"
            "${_libdir}/libswscale.a"
            "${_libdir}/libavutil.a"
            jpeg
            turbojpeg
            pthread
            m
            z
            lzma
            bz2
            drm
            va
            va-drm
            va-x11
        )

        message(STATUS "Fixed FFmpeg libraries linked with avdevice support for ${_target}")
    else()
        message(STATUS "FFmpeg headers not found at ${_ff_inc}; skipping static FFmpeg linking for ${_target}")
    endif()
endfunction()
