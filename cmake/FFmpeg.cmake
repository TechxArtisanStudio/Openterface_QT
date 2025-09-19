# FFmpeg.cmake - FFmpeg configuration and detection


# Initialize FFmpeg configuration variables
set(FFMPEG_PKG_CONFIG FALSE)

# Set FFMPEG_PREFIX from environment or default
if(NOT DEFINED FFMPEG_PREFIX)
    if(DEFINED ENV{FFMPEG_PREFIX})
        set(FFMPEG_PREFIX "$ENV{FFMPEG_PREFIX}" CACHE PATH "FFmpeg installation directory")
        message(STATUS "Using FFMPEG_PREFIX from environment: ${FFMPEG_PREFIX}")
    else()
        set(FFMPEG_PREFIX "/opt/ffmpeg" CACHE PATH "FFmpeg installation directory")
        message(STATUS "Using default FFMPEG_PREFIX: ${FFMPEG_PREFIX}")
    endif()
endif()

# Option to control hardware acceleration libraries
option(USE_HWACCEL "Enable hardware acceleration libraries (VA-API, VDPAU)" ON)

# Prefer static libraries
if(OPENTERFACE_BUILD_STATIC)
    set(CMAKE_FIND_STATIC_PREFER ON)
endif()

# Check for libjpeg-turbo (preferred for performance)
# For static builds, look in FFMPEG_PREFIX first
if(OPENTERFACE_BUILD_STATIC AND DEFINED FFMPEG_PREFIX)
    # Check for static libjpeg-turbo in FFmpeg prefix
    find_library(TURBOJPEG_LIBRARY 
        NAMES turbojpeg
        HINTS "${FFMPEG_PREFIX}/lib"
        NO_DEFAULT_PATH
    )
    find_path(TURBOJPEG_INCLUDE_DIR 
        NAMES turbojpeg.h
        HINTS "${FFMPEG_PREFIX}/include"
        NO_DEFAULT_PATH
    )
    if(TURBOJPEG_LIBRARY AND TURBOJPEG_INCLUDE_DIR)
        message(STATUS "Found static libjpeg-turbo in FFmpeg prefix: ${TURBOJPEG_LIBRARY}")
    endif()
endif()

# If not found in FFmpeg prefix, try system locations
if(NOT TURBOJPEG_LIBRARY OR NOT TURBOJPEG_INCLUDE_DIR)
    find_library(TURBOJPEG_LIBRARY turbojpeg)
    find_path(TURBOJPEG_INCLUDE_DIR turbojpeg.h)
endif()

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
        ${FFMPEG_PREFIX}
        "/usr/local"
        "/usr"
    )
else()
    # For dynamic builds, prioritize system packages
    set(FFMPEG_SEARCH_PATHS 
        "/usr"
        "/usr/local"
        ${FFMPEG_PREFIX}
    )
endif()

# Attempt to locate FFmpeg libraries
if(OPENTERFACE_BUILD_STATIC)
    # Prefer FFmpeg shipped inside the Qt build tree if it actually exists there.
    set(_qt_lib_dir "${FFMPEG_PREFIX}/lib")
    if(EXISTS "${_qt_lib_dir}/libavformat.a" AND EXISTS "${FFMPEG_PREFIX}/include/avformat.h")
        set(FFMPEG_LIB_DIR ${_qt_lib_dir})
        set(FFMPEG_INCLUDE_DIRS "${FFMPEG_PREFIX}/include")
        message(STATUS "Found FFmpeg static libraries in Qt build path: ${FFMPEG_LIB_DIR}")
        set(FFMPEG_FOUND TRUE)
    else()
        # Keep the previous behavior as a fallback (directory may be validated later)
        set(FFMPEG_LIB_DIR ${_qt_lib_dir})
        set(FFMPEG_INCLUDE_DIRS "${FFMPEG_PREFIX}/include")
        message(STATUS "FFmpeg static libs not found at ${_qt_lib_dir} - will try other search methods")
    endif()
else()
    # Check if Qt build tree provides dynamic FFmpeg first; if so, prefer it and skip pkg-config
    set(_qt_lib_dir "${FFMPEG_PREFIX}/lib")
    if(EXISTS "${_qt_lib_dir}/libavformat.so" AND EXISTS "${FFMPEG_PREFIX}/include/libavformat/avformat.h")
        set(FFMPEG_LIB_DIR ${_qt_lib_dir})
        set(FFMPEG_INCLUDE_DIRS "${FFMPEG_PREFIX}/include")
        message(STATUS "Found FFmpeg shared libraries in Qt build path: ${FFMPEG_LIB_DIR}")
        message((STATUS "Found header file at ${FFMPEG_PREFIX}/include/libavformat/avformat.h"))
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
                    set(FFMPEG_LIB_DIR ${FFMPEG_PREFIX}/lib)
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
        # Standard search - check both static and dynamic libraries based on build type
        if(OPENTERFACE_BUILD_STATIC)
            # For static builds, prefer .a files
            set(LIB_EXTENSIONS ".a" ".so")
            set(LIB_PATHS "${SEARCH_PATH}/lib")
        else()
            # For dynamic builds, prefer .so but fallback to .a (Docker environments)
            set(LIB_EXTENSIONS ".so" ".a")
            # For dynamic builds, check architecture-specific directories
            set(LIB_PATHS 
                "${SEARCH_PATH}/lib/x86_64-linux-gnu"
                "${SEARCH_PATH}/lib/aarch64-linux-gnu"
                "${SEARCH_PATH}/lib"
            )
        endif()
        
        # Check each potential library path with each extension
        foreach(LIB_PATH ${LIB_PATHS})
            foreach(LIB_EXT ${LIB_EXTENSIONS})
                set(LIB_NAME "libavformat${LIB_EXT}")
                message(STATUS "Checking for FFmpeg in: ${LIB_PATH}/${LIB_NAME}")
                if(EXISTS "${LIB_PATH}/${LIB_NAME}" AND EXISTS "${SEARCH_PATH}/include/libavformat/avformat.h")
                    set(FFMPEG_LIB_DIR "${LIB_PATH}")
                    set(FFMPEG_LIB_EXT "${LIB_EXT}")
                    message(STATUS "FFmpeg libraries found in: ${FFMPEG_LIB_DIR}")
                    message(STATUS "Using ${LIB_EXT} libraries")
                    set(FFMPEG_FOUND TRUE)
                    break()
                endif()
            endforeach()
            if(FFMPEG_FOUND)
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

# Set library extension and verify it was set during detection
if(NOT DEFINED FFMPEG_LIB_EXT)
    # Fallback logic if FFMPEG_LIB_EXT wasn't set during detection
    if(OPENTERFACE_BUILD_STATIC)
        set(FFMPEG_LIB_EXT ".a")
    else()
        # For dynamic builds, prefer .so but fallback to .a if available
        if(EXISTS "${FFMPEG_LIB_DIR}/libavformat.so")
            set(FFMPEG_LIB_EXT ".so")
            message(STATUS "Using dynamic FFmpeg libraries (.so)")
        elseif(EXISTS "${FFMPEG_LIB_DIR}/libavformat.a")
            set(FFMPEG_LIB_EXT ".a")
            message(STATUS "Using static FFmpeg libraries (.a) in dynamic build")
        else()
            set(FFMPEG_LIB_EXT ".so")  # Default fallback
            message(STATUS "Using default extension .so (libraries may not exist)")
        endif()
    endif()
endif()

message(STATUS "Final FFmpeg library extension: ${FFMPEG_LIB_EXT}")

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
# Determine the correct FFmpeg header to check (handle multiple include layouts)
set(FFMPEG_CHECK_FILE "")
if(FFMPEG_INCLUDE_DIRS)
    foreach(_inc_dir ${FFMPEG_INCLUDE_DIRS})
        if(EXISTS "${_inc_dir}/libavformat/avformat.h")
            set(FFMPEG_CHECK_FILE "${_inc_dir}/libavformat/avformat.h")
            break()
        elseif(EXISTS "${_inc_dir}/avformat/avformat.h")
            set(FFMPEG_CHECK_FILE "${_inc_dir}/avformat/avformat.h")
            break()
        elseif(EXISTS "${_inc_dir}/avformat.h")
            set(FFMPEG_CHECK_FILE "${_inc_dir}/avformat.h")
            break()
        endif()
    endforeach()
endif()

# Fallback: use the first include dir (or raw value) appended with avformat.h
if(NOT FFMPEG_CHECK_FILE)
    if(FFMPEG_INCLUDE_DIRS)
        list(GET FFMPEG_INCLUDE_DIRS 0 _first_inc)
        set(FFMPEG_CHECK_FILE "${_first_inc}/avformat.h")
    else()
        set(FFMPEG_CHECK_FILE "avformat.h")
    endif()
endif()

message(STATUS "Using FFmpeg header check file: ${FFMPEG_CHECK_FILE}")

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
set(EXISTS_HEADER EXISTS "${FFMPEG_CHECK_FILE}")
set(EXISTS_LIB EXISTS "${FFMPEG_LIB_CHECK}")

if(FFMPEG_PKG_CONFIG OR (EXISTS_HEADER AND EXISTS_LIB))
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

# Link FFmpeg libraries to target using detected configuration
function(link_ffmpeg_libraries)
    if(FFMPEG_FOUND AND FFMPEG_LIBRARIES)
        message(STATUS "Linking FFmpeg libraries to openterfaceQT: ${FFMPEG_LIBRARIES}")
        
        if(FFMPEG_LIB_EXT STREQUAL ".a")
            # Static FFmpeg libraries - use special linking flags
            message(STATUS "Linking static FFmpeg libraries with whole-archive for avdevice")
            set(JPEG_STATIC_PATH "/opt/ffmpeg/lib/libjpeg.a")
            set(TURBOJPEG_STATIC_PATH "/opt/ffmpeg/lib/libturbojpeg.a")
            
            if(EXISTS "${JPEG_STATIC_PATH}")
                message(STATUS "Using static libjpeg: ${JPEG_STATIC_PATH}")
                set(JPEG_LINK "${JPEG_STATIC_PATH}")
            else()
                message(WARNING "Static libjpeg.a not found at ${JPEG_STATIC_PATH}, falling back to -ljpeg")
                set(JPEG_LINK "-ljpeg")
            endif()
            
            if(EXISTS "${TURBOJPEG_STATIC_PATH}")
                message(STATUS "Using static libturbojpeg: ${TURBOJPEG_STATIC_PATH}")
                set(TURBOJPEG_LINK "${TURBOJPEG_STATIC_PATH}")
            else()
                message(WARNING "Static libturbojpeg.a not found at ${TURBOJPEG_STATIC_PATH}, falling back to -lturbojpeg")
                set(TURBOJPEG_LINK "-lturbojpeg")
            endif()
            
            target_link_libraries(openterfaceQT PRIVATE
                -Wl,--whole-archive
                "${FFMPEG_LIB_DIR}/libavdevice.a"
                -Wl,--no-whole-archive
                "${FFMPEG_LIB_DIR}/libavfilter.a"
                "${FFMPEG_LIB_DIR}/libavformat.a"
                "${FFMPEG_LIB_DIR}/libavcodec.a"
                "${FFMPEG_LIB_DIR}/libswresample.a"
                "${FFMPEG_LIB_DIR}/libswscale.a"
                "${FFMPEG_LIB_DIR}/libavutil.a"
                # Use static JPEG libraries if available
                ${JPEG_LINK}
                ${TURBOJPEG_LINK}
                -lpthread -lm -lz -llzma -lbz2 -ldrm -lva -lva-drm -lva-x11
            )
        else()
            # Dynamic FFmpeg libraries - simple linking
            message(STATUS "Linking dynamic FFmpeg libraries")
            target_link_libraries(openterfaceQT PRIVATE ${FFMPEG_LIBRARIES})
        endif()
        
        message(STATUS "FFmpeg libraries linked successfully")
    else()
        message(STATUS "FFmpeg not found - skipping FFmpeg library linking")
    endif()
endfunction()


# Public helper: add FFmpeg static libraries to a target (legacy function for compatibility)
# Usage: add_ffmpeg_static_libraries(<target> <ffmpeg_root>)
function(add_ffmpeg_static_libraries _target _ffmpeg_root)
    message(STATUS "add_ffmpeg_static_libraries called - delegating to automatic FFmpeg detection")
    # This function is now a wrapper that delegates to the automatic detection
    # The FFmpeg detection and linking is handled by the main FFmpeg.cmake logic
    if(FFMPEG_FOUND)
        message(STATUS "FFmpeg already detected - linking will be handled by link_ffmpeg_libraries()")
    else()
        message(STATUS "FFmpeg not detected - please check FFmpeg installation at common paths like /usr/local, /usr")
    endif()
endfunction()
