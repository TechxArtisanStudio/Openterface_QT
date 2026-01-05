# FFmpeg.cmake - FFmpeg configuration and detection

if(DEFINED FFMPEG_FOUND AND FFMPEG_FOUND)
  return()
endif()

# Ensure MINGW_ROOT is set for Windows builds
if(WIN32 AND NOT DEFINED MINGW_ROOT)
    if(DEFINED ENV{MINGW_ROOT})
        set(MINGW_ROOT "$ENV{MINGW_ROOT}" CACHE PATH "MinGW root directory")
    else()
        # Default to standard MSYS2 location
        set(MINGW_ROOT "C:/msys64/mingw64" CACHE PATH "MinGW root directory")
    endif()
    message(STATUS "Using MINGW_ROOT: ${MINGW_ROOT}")
endif()

# Initialize FFmpeg configuration variables
set(FFMPEG_PKG_CONFIG ${USE_SHARED_FFMPEG})

# Set ZLIB_LIBRARY for static zlib
if(NOT ZLIB_LIBRARY)
    if(DEFINED MINGW_ROOT AND WIN32)
        set(ZLIB_LIBRARY "${MINGW_ROOT}/lib/libz.a" CACHE FILEPATH "Path to static zlib library")
    elseif(WIN32)
        set(ZLIB_LIBRARY "C:/msys64/mingw64/lib/libz.a" CACHE FILEPATH "Path to static zlib library")
    endif()
endif()

# Set FFMPEG_PREFIX from environment or default
if(NOT DEFINED FFMPEG_PREFIX)
    if(DEFINED ENV{FFMPEG_PREFIX})
        set(FFMPEG_PREFIX "$ENV{FFMPEG_PREFIX}" CACHE PATH "FFmpeg installation directory")
        message(STATUS "Using FFMPEG_PREFIX from environment: ${FFMPEG_PREFIX}")
    else()
        # Platform-specific defaults
        if(WIN32)
            set(FFMPEG_PREFIX "C:/ffmpeg-static" CACHE PATH "FFmpeg installation directory")
        else()
            set(FFMPEG_PREFIX "/opt/ffmpeg" CACHE PATH "FFmpeg installation directory")
        endif()
        message(STATUS "Using default FFMPEG_PREFIX: ${FFMPEG_PREFIX}")
    endif()
endif()

# Normalize FFMPEG_PREFIX to use consistent path separators
if(FFMPEG_PREFIX)
    file(TO_CMAKE_PATH "${FFMPEG_PREFIX}" FFMPEG_PREFIX)
    message(STATUS "Normalized FFMPEG_PREFIX: ${FFMPEG_PREFIX}")
endif()

# Option to control hardware acceleration libraries
option(USE_HWACCEL "Enable hardware acceleration libraries (VA-API, VDPAU)" ON)

# Option to use shared FFmpeg libraries instead of static
option(USE_SHARED_FFMPEG "Use shared FFmpeg libraries instead of static" OFF)

# Prefer static libraries for FFmpeg unless shared is requested
if(USE_SHARED_FFMPEG)
    if(WIN32)
        # On Windows, shared libraries are .dll.a (import libs) or .dll
        set(CMAKE_FIND_LIBRARY_SUFFIXES .dll.a .dll .lib)
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES .so)
    endif()
    message(STATUS "Using shared FFmpeg libraries")
else()
    set(CMAKE_FIND_STATIC_PREFER ON)
endif()

# Check for libjpeg-turbo (preferred for performance)
# Look in FFMPEG_PREFIX first for static builds
if(DEFINED FFMPEG_PREFIX)
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

# Set FFmpeg include and library directories (prefer local prefix, then system)
if(WIN32)
    set(FFMPEG_SEARCH_PATHS 
        ${FFMPEG_PREFIX}
        "C:/ffmpeg-static"
        "C:/ffmpeg"
    )
else()
    set(FFMPEG_SEARCH_PATHS 
        ${FFMPEG_PREFIX}
        "/usr/local"
        "/usr"
    )
endif()
if(WIN32)
    set(FFMPEG_SEARCH_PATHS 
        ${FFMPEG_PREFIX}
        "C:/ffmpeg-static"
        "C:/ffmpeg"
    )
else()
    set(FFMPEG_SEARCH_PATHS 
        ${FFMPEG_PREFIX}
        "/usr/local"
        "/usr"
    )
endif()

# Attempt to locate FFmpeg libraries (prefer static)
# Prefer FFmpeg shipped inside the configured prefix if it actually exists there.
set(_qt_lib_dir "${FFMPEG_PREFIX}/lib")
if(EXISTS "${_qt_lib_dir}/libavformat.a" AND EXISTS "${FFMPEG_PREFIX}/include/libavformat/avformat.h")
    set(FFMPEG_LIB_DIR ${_qt_lib_dir})
    set(FFMPEG_INCLUDE_DIRS "${FFMPEG_PREFIX}/include")
    message(STATUS "Found FFmpeg static libraries in prefix: ${FFMPEG_LIB_DIR}")
    set(FFMPEG_FOUND TRUE)
else()
    # Keep the previous behavior as a fallback (directory may be validated later)
    set(FFMPEG_LIB_DIR ${_qt_lib_dir})
    set(FFMPEG_INCLUDE_DIRS "${FFMPEG_PREFIX}/include")
    message(STATUS "FFmpeg static libs not found at ${_qt_lib_dir} - will try other search methods")
endif()

# If pkg-config didn't find FFmpeg (or we're using static linking), fall back to path search
if(NOT FFMPEG_FOUND)
    message(STATUS "Falling back to manual path search for FFmpeg...")

    # Find FFmpeg installation
    message(STATUS "FFmpeg search paths: ${FFMPEG_SEARCH_PATHS}")
    foreach(SEARCH_PATH ${FFMPEG_SEARCH_PATHS})
        # Choose which library file extensions to try depending on platform and preference
        if(WIN32)
            if(USE_SHARED_FFMPEG)
                # Prefer import libraries or system libs when shared is requested
                set(LIB_EXTENSIONS ".dll.a" ".lib" ".dll")
            else()
                # Try static first, fall back to MinGW import libs and system libs
                set(LIB_EXTENSIONS ".a" ".dll.a" ".lib" ".dll")
            endif()
            set(LIB_PATHS "${SEARCH_PATH}/lib" "${SEARCH_PATH}/bin")
        else()
            if(USE_SHARED_FFMPEG)
                set(LIB_EXTENSIONS ".so")
            else()
                set(LIB_EXTENSIONS ".a")
            endif()
            set(LIB_PATHS "${SEARCH_PATH}/lib/x86_64-linux-gnu" "${SEARCH_PATH}/lib/aarch64-linux-gnu" "${SEARCH_PATH}/lib")
        endif()

        # Check each potential library path with each extension
        foreach(LIB_PATH ${LIB_PATHS})
            foreach(LIB_EXT ${LIB_EXTENSIONS})
                set(LIB_NAME "libavformat${LIB_EXT}")
                message(STATUS "Checking for FFmpeg in: ${LIB_PATH}/${LIB_NAME}")

                if(EXISTS "${LIB_PATH}/${LIB_NAME}" AND EXISTS "${SEARCH_PATH}/include/libavformat/avformat.h")
                    set(FFMPEG_LIB_DIR "${LIB_PATH}")
                    set(FFMPEG_LIB_EXT "${LIB_EXT}")
                    # Ensure include directory is captured for compilation
                    set(FFMPEG_INCLUDE_DIRS "${SEARCH_PATH}/include")
                    message(STATUS "FFmpeg libraries found in: ${FFMPEG_LIB_DIR}")
                    message(STATUS "Using ${LIB_EXT} libraries")
                    set(FFMPEG_FOUND TRUE)
                    break()
                endif()

                # Special-case: if checking for plain DLLs (no lib prefix), look for avformat-*.dll
                if(LIB_EXT STREQUAL ".dll" AND EXISTS "${LIB_PATH}")
                    file(GLOB _found_avformat_dlls "${LIB_PATH}/avformat-*.dll")
                    if(_found_avformat_dlls)
                        list(GET _found_avformat_dlls 0 _found_avformat_dll)
                        set(FFMPEG_LIB_DIR "${LIB_PATH}")
                        set(FFMPEG_LIB_EXT ".dll")
                        set(FFMPEG_INCLUDE_DIRS "${SEARCH_PATH}/include")
                        message(STATUS "Found FFmpeg DLL: ${_found_avformat_dll}")
                        set(FFMPEG_FOUND TRUE)
                        break()
                    endif()
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
    # Default based on USE_SHARED_FFMPEG and platform
    if(USE_SHARED_FFMPEG)
        if(WIN32)
            # Prefer MinGW import libs for shared builds on Windows
            set(FFMPEG_LIB_EXT ".dll.a")
        else()
            set(FFMPEG_LIB_EXT ".so")
        endif()
    else()
        set(FFMPEG_LIB_EXT ".a")
    endif()
endif()

# Normalize the library directory path to use consistent separators
if(FFMPEG_LIB_DIR)
    file(TO_CMAKE_PATH "${FFMPEG_LIB_DIR}" FFMPEG_LIB_DIR)
    message(STATUS "Normalized FFmpeg library directory: ${FFMPEG_LIB_DIR}")
endif()

message(STATUS "Final FFmpeg library extension: ${FFMPEG_LIB_EXT}")

# Build the list of FFmpeg libraries depending on detected extension
if(FFMPEG_LIB_EXT STREQUAL ".dll")
    # If we only found real DLLs, glob the actual DLL filenames (e.g. avdevice-*.dll)
    file(GLOB _avdevice_dlls "${FFMPEG_LIB_DIR}/avdevice-*.dll" "${FFMPEG_LIB_DIR}/libavdevice-*.dll")
    file(GLOB _avfilter_dlls "${FFMPEG_LIB_DIR}/avfilter-*.dll" "${FFMPEG_LIB_DIR}/libavfilter-*.dll")
    file(GLOB _avformat_dlls "${FFMPEG_LIB_DIR}/avformat-*.dll" "${FFMPEG_LIB_DIR}/libavformat-*.dll")
    file(GLOB _avcodec_dlls "${FFMPEG_LIB_DIR}/avcodec-*.dll" "${FFMPEG_LIB_DIR}/libavcodec-*.dll")
    file(GLOB _swresample_dlls "${FFMPEG_LIB_DIR}/swresample-*.dll" "${FFMPEG_LIB_DIR}/libswresample-*.dll")
    file(GLOB _swscale_dlls "${FFMPEG_LIB_DIR}/swscale-*.dll" "${FFMPEG_LIB_DIR}/libswscale-*.dll")
    file(GLOB _avutil_dlls "${FFMPEG_LIB_DIR}/avutil-*.dll" "${FFMPEG_LIB_DIR}/libavutil-*.dll")

    list(GET _avdevice_dlls 0 _avdevice)   
    list(GET _avfilter_dlls 0 _avfilter)   
    list(GET _avformat_dlls 0 _avformat)   
    list(GET _avcodec_dlls 0 _avcodec)     
    list(GET _swresample_dlls 0 _swresample)
    list(GET _swscale_dlls 0 _swscale)     
    list(GET _avutil_dlls 0 _avutil)       

    set(FFMPEG_LIBRARIES
        "${_avdevice}"
        "${_avfilter}"
        "${_avformat}"
        "${_avcodec}"
        "${_swresample}"
        "${_swscale}"
        "${_avutil}"
    )
else()
    # Default behavior covers static (.a), import (.dll.a) and .so/.lib
    set(FFMPEG_LIBRARIES 
        "${FFMPEG_LIB_DIR}/libavdevice${FFMPEG_LIB_EXT}"
        "${FFMPEG_LIB_DIR}/libavfilter${FFMPEG_LIB_EXT}"
        "${FFMPEG_LIB_DIR}/libavformat${FFMPEG_LIB_EXT}"
        "${FFMPEG_LIB_DIR}/libavcodec${FFMPEG_LIB_EXT}"
        "${FFMPEG_LIB_DIR}/libswresample${FFMPEG_LIB_EXT}"
        "${FFMPEG_LIB_DIR}/libswscale${FFMPEG_LIB_EXT}"
        "${FFMPEG_LIB_DIR}/libavutil${FFMPEG_LIB_EXT}"
    )
endif()

message(STATUS "Using FFmpeg library paths: ${FFMPEG_LIBRARIES}")

# Verify all FFmpeg libraries exist
message(STATUS "Checking FFmpeg library existence...")
foreach(FFMPEG_LIB ${FFMPEG_LIBRARIES})
    # Normalize the library path
    file(TO_CMAKE_PATH "${FFMPEG_LIB}" FFMPEG_LIB_NORMALIZED)
    message(STATUS "Checking: ${FFMPEG_LIB_NORMALIZED}")
    
    if(EXISTS "${FFMPEG_LIB_NORMALIZED}")
        message(STATUS "✓ Found: ${FFMPEG_LIB_NORMALIZED}")
    else()
        message(STATUS "✗ Missing: ${FFMPEG_LIB_NORMALIZED}")
        # Also try checking if the parent directory exists for debugging
        get_filename_component(LIB_DIR "${FFMPEG_LIB_NORMALIZED}" DIRECTORY)
        if(EXISTS "${LIB_DIR}")
            message(STATUS "  Directory exists: ${LIB_DIR}")
            file(GLOB LIB_DIR_CONTENTS "${LIB_DIR}/*")
            message(STATUS "  Directory contents: ${LIB_DIR_CONTENTS}")
        else()
            message(STATUS "  Directory does not exist: ${LIB_DIR}")
        endif()
        message(FATAL_ERROR "✗ Missing: ${FFMPEG_LIB_NORMALIZED}")
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

# Add essential libraries (platform-specific)
if(NOT WIN32)
    # Linux libraries
    list(APPEND HWACCEL_LIBRARIES
        X11
        atomic
        pthread
        m
    )
else()
    # Windows libraries for FFmpeg
    list(APPEND HWACCEL_LIBRARIES
        ws2_32
        secur32
        bcrypt
        mfplat
        mfuuid
        ole32
        strmiids
    )
    
    # Add Intel QSV library if available (prefer static)
    find_library(MFX_STATIC_LIBRARY
        NAMES libmfx.a
        PATHS
            "C:/ffmpeg-static/lib"
            "${FFMPEG_PREFIX}/lib"
            "$ENV{FFMPEG_PREFIX}/lib"
            "C:/msys64/mingw64/lib"
        NO_DEFAULT_PATH
    )
    if(MFX_STATIC_LIBRARY)
        list(APPEND HWACCEL_LIBRARIES ${MFX_STATIC_LIBRARY})
        message(STATUS "Found static Intel QSV library: ${MFX_STATIC_LIBRARY}")
    else()
        # Fallback to dynamic library
        find_library(MFX_LIBRARY
            NAMES mfx libmfx
            PATHS
                "C:/ffmpeg-static/lib"
                "${FFMPEG_PREFIX}/lib"
                "$ENV{FFMPEG_PREFIX}/lib"
                "C:/msys64/mingw64/lib"
            NO_DEFAULT_PATH
        )
        if(MFX_LIBRARY)
            list(APPEND HWACCEL_LIBRARIES ${MFX_LIBRARY})
            message(STATUS "Found dynamic Intel QSV library: ${MFX_LIBRARY}")
        else()
            message(STATUS "Intel QSV library (libmfx) not found - QSV support may be limited")
        endif()
    endif()
endif()
# Add essential libraries (platform-specific)
if(NOT WIN32)
    # Linux libraries
    list(APPEND HWACCEL_LIBRARIES
        X11
        atomic
        pthread
        m
    )
else()
    # Windows libraries for FFmpeg
    list(APPEND HWACCEL_LIBRARIES
        ws2_32
        secur32
        bcrypt
        mfplat
        mfuuid
        ole32
        strmiids
    )
    
    # Add Intel QSV library if available (prefer static)
    find_library(MFX_STATIC_LIBRARY
        NAMES libmfx.a
        PATHS
            "C:/ffmpeg-static/lib"
            "${FFMPEG_PREFIX}/lib"
            "$ENV{FFMPEG_PREFIX}/lib"
            "C:/msys64/mingw64/lib"
        NO_DEFAULT_PATH
    )
    if(MFX_STATIC_LIBRARY)
        list(APPEND HWACCEL_LIBRARIES ${MFX_STATIC_LIBRARY})
        message(STATUS "Found static Intel QSV library: ${MFX_STATIC_LIBRARY}")
    else()
        # Fallback to dynamic library
        find_library(MFX_LIBRARY
            NAMES mfx libmfx
            PATHS
                "C:/ffmpeg-static/lib"
                "${FFMPEG_PREFIX}/lib"
                "$ENV{FFMPEG_PREFIX}/lib"
                "C:/msys64/mingw64/lib"
            NO_DEFAULT_PATH
        )
        if(MFX_LIBRARY)
            list(APPEND HWACCEL_LIBRARIES ${MFX_LIBRARY})
            message(STATUS "Found dynamic Intel QSV library: ${MFX_LIBRARY}")
        else()
            message(STATUS "Intel QSV library (libmfx) not found - QSV support may be limited")
        endif()
    endif()
endif()

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

# Determine which specific library file to check based on detected extension
set(FFMPEG_LIB_CHECK "${FFMPEG_LIB_DIR}/libavformat${FFMPEG_LIB_EXT}")

# If we found FFmpeg via pkg-config, trust it; otherwise verify files exist manually
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
    # Show check results
    if(EXISTS "${FFMPEG_CHECK_FILE}")
        message(STATUS "Checked header: ${FFMPEG_CHECK_FILE} (exists: TRUE)")
    else()
        message(STATUS "Checked header: ${FFMPEG_CHECK_FILE} (exists: FALSE)")
    endif()
    if(EXISTS "${FFMPEG_LIB_CHECK}")
        message(STATUS "Checked library: ${FFMPEG_LIB_CHECK} (exists: TRUE)")
    else()
        message(STATUS "Checked library: ${FFMPEG_LIB_CHECK} (exists: FALSE)")
    endif()
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
            
            # Platform-specific JPEG library paths
            if(WIN32)
                set(JPEG_STATIC_PATH "${FFMPEG_PREFIX}/lib/libjpeg.a")
                set(TURBOJPEG_STATIC_PATH "${FFMPEG_PREFIX}/lib/libturbojpeg.a")
            else()
                set(JPEG_STATIC_PATH "/opt/ffmpeg/lib/libjpeg.a")
                set(TURBOJPEG_STATIC_PATH "/opt/ffmpeg/lib/libturbojpeg.a")
            endif()
            
            # Platform-specific JPEG library paths
            if(WIN32)
                set(JPEG_STATIC_PATH "${FFMPEG_PREFIX}/lib/libjpeg.a")
                set(TURBOJPEG_STATIC_PATH "${FFMPEG_PREFIX}/lib/libturbojpeg.a")
            else()
                set(JPEG_STATIC_PATH "/opt/ffmpeg/lib/libjpeg.a")
                set(TURBOJPEG_STATIC_PATH "/opt/ffmpeg/lib/libturbojpeg.a")
            endif()
            
            if(EXISTS "${JPEG_STATIC_PATH}")
                message(STATUS "Using static libjpeg: ${JPEG_STATIC_PATH}")
                set(JPEG_LINK "${JPEG_STATIC_PATH}")
            else()
                message(WARNING "Static libjpeg.a not found at ${JPEG_STATIC_PATH}, falling back to -ljpeg")
                if(WIN32)
                    # On Windows, skip -ljpeg if not found (likely included in FFmpeg build)
                    set(JPEG_LINK "")
                else()
                    set(JPEG_LINK "-ljpeg")
                endif()
                if(WIN32)
                    # On Windows, skip -ljpeg if not found (likely included in FFmpeg build)
                    set(JPEG_LINK "")
                else()
                    set(JPEG_LINK "-ljpeg")
                endif()
            endif()
            
            if(EXISTS "${TURBOJPEG_STATIC_PATH}")
                message(STATUS "Using static libturbojpeg: ${TURBOJPEG_STATIC_PATH}")
                set(TURBOJPEG_LINK "${TURBOJPEG_STATIC_PATH}")
            else()
                message(WARNING "Static libturbojpeg.a not found at ${TURBOJPEG_STATIC_PATH}, falling back to -lturbojpeg")
                if(WIN32)
                    # On Windows, skip -lturbojpeg if not found (likely included in FFmpeg build)
                    set(TURBOJPEG_LINK "")
                else()
                    set(TURBOJPEG_LINK "-lturbojpeg")
                endif()
                if(WIN32)
                    # On Windows, skip -lturbojpeg if not found (likely included in FFmpeg build)
                    set(TURBOJPEG_LINK "")
                else()
                    set(TURBOJPEG_LINK "-lturbojpeg")
                endif()
            endif()
            
            # Platform-specific FFmpeg dependencies
            if(WIN32)
                # Windows-specific FFmpeg dependencies
                # Verify MINGW_ROOT is set
                if(NOT DEFINED MINGW_ROOT OR "${MINGW_ROOT}" STREQUAL "")
                    message(FATAL_ERROR "MINGW_ROOT is not set. Please define it via -DMINGW_ROOT=C:/msys64/mingw64")
                endif()
                
                message(STATUS "Building FFmpeg dependencies with MINGW_ROOT: ${MINGW_ROOT}")
                
                # Verify critical libraries exist
                set(_REQUIRED_LIBS
                    "${MINGW_ROOT}/lib/libbz2.a"
                    "${MINGW_ROOT}/lib/liblzma.a"
                    "${MINGW_ROOT}/lib/libwinpthread.a"
                )
                foreach(_lib ${_REQUIRED_LIBS})
                    if(NOT EXISTS "${_lib}")
                        message(WARNING "Required library not found: ${_lib}")
                    else()
                        message(STATUS "Found: ${_lib}")
                    endif()
                endforeach()
                  
                set(_FFMPEG_STATIC_DEPS
                    ${JPEG_LINK}
                    ${TURBOJPEG_LINK}
                    # Windows system libraries for FFmpeg
                    # Check for static zlib
                    -lvfw32        # Video for Windows capture
                    -lshlwapi      # Shell API (for SHCreateStreamOnFileA)
                    ${ZLIB_LIBRARY}      # zlib for compression
                    "${MINGW_ROOT}/lib/libbz2.a"    # bzip2 for compression
                    "${MINGW_ROOT}/lib/liblzma.a"   # lzma/xz for compression
                    "${MINGW_ROOT}/lib/libzstd.a"   # zstd for compression
                    "${MINGW_ROOT}/lib/libbrotlidec.a"  # Brotli decompression
                    "${MINGW_ROOT}/lib/libbrotlienc.a"  # Brotli compression
                    "${MINGW_ROOT}/lib/libbrotlicommon.a"  # Brotli common
                    "${MINGW_ROOT}/lib/libmfx.a"    # Intel Media SDK for QSV (optional)
                    -lmingwex       # MinGW extensions for setjmp etc.
                    "${MINGW_ROOT}/lib/libwinpthread.a"  # Windows pthreads for 64-bit time functions
                    # -liconv        # Character encoding conversion
                )
                
                # Use MSYS2's winpthread for 64-bit time functions
                # if(EXISTS "C:/msys64/mingw64/lib/libwinpthread.a")
                #     list(APPEND _FFMPEG_STATIC_DEPS "C:/msys64/mingw64/lib/libwinpthread.a")
                #     message(STATUS "Using MSYS2 winpthread library")
                # else()
                #     list(APPEND _FFMPEG_STATIC_DEPS -lpthread)
                # endif()
                
                # Check for libpostproc in FFmpeg directory
                if(EXISTS "${FFMPEG_PREFIX}/lib/libpostproc.a")
                    list(APPEND _FFMPEG_STATIC_DEPS "${FFMPEG_PREFIX}/lib/libpostproc.a")
                    message(STATUS "Found postproc library: ${FFMPEG_PREFIX}/lib/libpostproc.a")
                endif()
                
                # Add MSYS2 libraries (used when FFmpeg was built with MSYS2)
                # if(EXISTS "C:/msys64/mingw64/lib/libbz2.a")
                #     list(APPEND _FFMPEG_STATIC_DEPS "C:/msys64/mingw64/lib/libbz2.a")
                #     message(STATUS "Found bz2 library in MSYS2")
                # endif()
                
                # if(EXISTS "C:/msys64/mingw64/lib/liblzma.a")
                #     list(APPEND _FFMPEG_STATIC_DEPS "C:/msys64/mingw64/lib/liblzma.a")
                #     message(STATUS "Found lzma library in MSYS2")
                # endif()
                
                # Check for libiconv (required for FFmpeg character encoding)
                if(EXISTS "${MINGW_ROOT}/lib/libiconv.a")
                    list(APPEND _FFMPEG_STATIC_DEPS "${MINGW_ROOT}/lib/libiconv.a")
                    message(STATUS "Found libiconv library: ${MINGW_ROOT}/lib/libiconv.a")
                else()
                    message(WARNING "libiconv.a not found - character encoding may not work properly")
                endif()
                
                # Check for static zlib (required for FFmpeg compression)
                # if(EXISTS "C:/msys64/mingw64/lib/libz.a")
                #     list(APPEND _FFMPEG_STATIC_DEPS "C:/msys64/mingw64/lib/libz.a")
                #     message(STATUS "Found static zlib library: C:/msys64/mingw64/lib/libz.a")
                # else()
                #     message(WARNING "libz.a not found - compression may not work properly")
                # endif()
                
                # Add stack protection library LAST (required by MSYS2-compiled libraries)
                # Use full path to static library to avoid linking to DLL
                # Try multiple possible locations for libssp.a
                set(SSP_PATHS
                    "E:/Qt/Tools/mingw1120_64/lib/gcc/x86_64-w64-mingw32/11.2.0/libssp.a"
                    "E:/Qt/Tools/mingw1120_64/x86_64-w64-mingw32/lib/libssp.a"
                    "${MINGW_PATH}/lib/gcc/x86_64-w64-mingw32/11.2.0/libssp.a"
                )
                foreach(SSP_PATH ${SSP_PATHS})
                    if(EXISTS "${SSP_PATH}")
                        list(APPEND _FFMPEG_STATIC_DEPS "${SSP_PATH}")
                        message(STATUS "Found static ssp library: ${SSP_PATH}")
                        break()
                    endif()
                endforeach()
                
            else()
                # Linux-specific FFmpeg dependencies
                set(_FFMPEG_STATIC_DEPS
                    ${JPEG_LINK}
                    ${TURBOJPEG_LINK}
                    # Core system libs
                    -lpthread -lm -ldl -lz -llzma -lbz2
                    # DRM/VA/VDPAU/X11 stack (vdpa_device_create_x11 lives in libvdpau and needs X11)
                    -ldrm -lva -lva-drm -lva-x11 -lvdpau -lX11 -lXext
                    # XCB is required by avdevice xcbgrab; ensure core xcb gets linked
                    -lxcb
                    # XCB extensions used by xcbgrab (shared memory, xfixes for cursor, shape for OSD)
                    -lxcb-shm -lxcb-xfixes -lxcb-shape -lxcb-image
                    # PulseAudio is required by avdevice pulse input/output
                    -lpulse -lpulse-simple
                )
                
                # Check for libpostproc in FFmpeg directory
                if(EXISTS "${FFMPEG_PREFIX}/lib/libpostproc.a")
                    list(APPEND _FFMPEG_STATIC_DEPS "${FFMPEG_PREFIX}/lib/libpostproc.a")
                    message(STATUS "Found postproc library: ${FFMPEG_PREFIX}/lib/libpostproc.a")
                endif()
                
                # Add libmfx if available
                find_library(MFX_LIBRARY mfx)
                if(MFX_LIBRARY)
                    list(APPEND _FFMPEG_STATIC_DEPS ${MFX_LIBRARY})
                    message(STATUS "Found MFX library: ${MFX_LIBRARY}")
                else()
                    message(STATUS "MFX library not found - QSV support may be limited")
                endif()
            endif()

            # If we probed additional HW libs (full paths), append them too to be safe
            if(HWACCEL_LIBRARIES)
                list(APPEND _FFMPEG_STATIC_DEPS ${HWACCEL_LIBRARIES})
                message(STATUS "Appending HWACCEL_LIBRARIES to FFmpeg link: ${HWACCEL_LIBRARIES}")
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
                ${_FFMPEG_STATIC_DEPS}
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
