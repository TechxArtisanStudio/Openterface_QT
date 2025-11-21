# FFmpeg.cmake - FFmpeg configuration and detection


# Initialize FFmpeg configuration variables
set(FFMPEG_PKG_CONFIG FALSE)

# Set ZLIB_LIBRARY for static zlib
if(NOT ZLIB_LIBRARY)
    set(ZLIB_LIBRARY "C:/msys64/mingw64/lib/libz.a" CACHE FILEPATH "Path to static zlib library")
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

# Option to control hardware acceleration libraries
option(USE_HWACCEL "Enable hardware acceleration libraries (VA-API, VDPAU)" ON)

# Determine which library type to search for based on build type
# For shared builds, search for .so; for static builds, search for .a
if(OPENTERFACE_BUILD_STATIC)
    set(FFMPEG_LIB_EXTENSIONS ".a")
    message(STATUS "Static build detected - searching for static FFmpeg libraries (.a)")
else()
    set(FFMPEG_LIB_EXTENSIONS ".so")
    message(STATUS "Shared build detected - searching for shared FFmpeg libraries (.so)")
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

# Attempt to locate FFmpeg libraries (prefer shared for shared builds, static for static builds)
# Prefer FFmpeg shipped inside the configured prefix if it actually exists there.
set(_qt_lib_dir "${FFMPEG_PREFIX}/lib")

# Check for appropriate library type based on build configuration
if(OPENTERFACE_BUILD_STATIC)
    set(_ffmpeg_check_lib "${_qt_lib_dir}/libavformat.a")
    set(_build_type_desc "static")
else()
    set(_ffmpeg_check_lib "${_qt_lib_dir}/libavformat.so")
    set(_build_type_desc "shared")
endif()

if(EXISTS "${_ffmpeg_check_lib}" AND EXISTS "${FFMPEG_PREFIX}/include/libavformat/avformat.h")
    set(FFMPEG_LIB_DIR ${_qt_lib_dir})
    set(FFMPEG_INCLUDE_DIRS "${FFMPEG_PREFIX}/include")
    message(STATUS "Found FFmpeg ${_build_type_desc} libraries in prefix: ${FFMPEG_LIB_DIR}")
    set(FFMPEG_FOUND TRUE)
else()
    # Keep the previous behavior as a fallback (directory may be validated later)
    set(FFMPEG_LIB_DIR ${_qt_lib_dir})
    set(FFMPEG_INCLUDE_DIRS "${FFMPEG_PREFIX}/include")
    message(STATUS "FFmpeg ${_build_type_desc} libs not found at ${_qt_lib_dir} - will try other search methods")
endif()

# If pkg-config didn't find FFmpeg (or we're using specific linking), fall back to path search
if(NOT FFMPEG_FOUND)
    message(STATUS "Falling back to manual path search for FFmpeg (${_build_type_desc})...")

    # Find FFmpeg installation
    message(STATUS "FFmpeg search paths: ${FFMPEG_SEARCH_PATHS}")
    foreach(SEARCH_PATH ${FFMPEG_SEARCH_PATHS})
        # For static builds, prefer .a files; check common lib directories
        set(LIB_EXTENSIONS ".a")
        
        # Platform-specific library paths
        if(WIN32)
            set(LIB_PATHS 
                "${SEARCH_PATH}/lib"
                "${SEARCH_PATH}/bin"
            )
        else()
            set(LIB_PATHS 
                "${SEARCH_PATH}/lib/x86_64-linux-gnu"
                "${SEARCH_PATH}/lib/aarch64-linux-gnu"
                "${SEARCH_PATH}/lib"
            )
        endif()
        
        # Platform-specific library paths
        if(WIN32)
            set(LIB_PATHS 
                "${SEARCH_PATH}/lib"
                "${SEARCH_PATH}/bin"
            )
        else()
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
                    # Ensure include directory is captured for compilation
                    set(FFMPEG_INCLUDE_DIRS "${SEARCH_PATH}/include")
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
    # Default based on build type
    if(OPENTERFACE_BUILD_STATIC)
        set(FFMPEG_LIB_EXT ".a")
        message(STATUS "Static build: defaulting to .a libraries")
    else()
        set(FFMPEG_LIB_EXT ".so")
        message(STATUS "Shared build: defaulting to .so libraries")
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
        message(STATUS "OPENTERFACE_BUILD_STATIC: ${OPENTERFACE_BUILD_STATIC}")
        
        if(FFMPEG_LIB_EXT STREQUAL ".a" AND OPENTERFACE_BUILD_STATIC)
            # Static FFmpeg libraries - use special linking flags (only when static build is enabled)
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
                message(STATUS "Using static libturbojpeg: ${TURBO                # For shared library build:
                cmake -B build -DOPENTERFACE_BUILD_STATIC=OFF
                cmake --build build
                
                # You should now see:
                # -- Shared build detected - searching for shared FFmpeg libraries (.so)
                # -- Final FFmpeg library extension: .so
                # -- Linking FFmpeg libraries to openterfaceQT: /opt/ffmpeg/lib/libavdevice.so;...JPEG_STATIC_PATH}")
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
                set(_FFMPEG_STATIC_DEPS
                    ${JPEG_LINK}
                    ${TURBOJPEG_LINK}
                    # Windows system libraries for FFmpeg
                    # Check for static zlib
                    -lvfw32        # Video for Windows capture
                    -lshlwapi      # Shell API (for SHCreateStreamOnFileA)
                    ${ZLIB_LIBRARY}      # zlib for compression
                    "C:/msys64/mingw64/lib/libbz2.a"    # bzip2 for compression
                    "C:/msys64/mingw64/lib/liblzma.a"   # lzma/xz for compression
                    "C:/msys64/mingw64/lib/libmfx.a"    # Intel Media SDK for QSV
                    -lmingwex       # MinGW extensions for setjmp etc.
                    "C:/msys64/mingw64/lib/libwinpthread.a"  # Windows pthreads for 64-bit time functions
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
                if(EXISTS "C:/msys64/mingw64/lib/libiconv.a")
                    list(APPEND _FFMPEG_STATIC_DEPS "C:/msys64/mingw64/lib/libiconv.a")
                    message(STATUS "Found libiconv library: C:/msys64/mingw64/lib/libiconv.a")
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
            # Dynamic FFmpeg libraries or shared build - use dynamic linking
            message(STATUS "Linking dynamic FFmpeg libraries")
            
            # For shared builds, use shared JPEG libraries from /opt/ffmpeg
            if(NOT OPENTERFACE_BUILD_STATIC)
                message(STATUS "Shared build detected - looking for shared JPEG libraries in ${FFMPEG_PREFIX}")
                
                # Look for shared libjpeg in FFMPEG_PREFIX
                find_library(JPEG_SHARED 
                    NAMES jpeg
                    HINTS "${FFMPEG_PREFIX}/lib"
                    NO_DEFAULT_PATH
                )
                if(JPEG_SHARED)
                    message(STATUS "Found shared libjpeg: ${JPEG_SHARED}")
                    set(JPEG_LINK "${JPEG_SHARED}")
                else()
                    message(FATAL_ERROR "Shared libjpeg not found in ${FFMPEG_PREFIX}/lib")
                endif()
                
                # Look for shared libturbojpeg in FFMPEG_PREFIX
                find_library(TURBOJPEG_SHARED 
                    NAMES turbojpeg
                    HINTS "${FFMPEG_PREFIX}/lib"
                    NO_DEFAULT_PATH
                )
                if(TURBOJPEG_SHARED)
                    message(STATUS "Found shared libturbojpeg: ${TURBOJPEG_SHARED}")
                    set(TURBOJPEG_LINK "${TURBOJPEG_SHARED}")
                else()
                    message(FATAL_ERROR "Shared libturbojpeg not found in ${FFMPEG_PREFIX}/lib")
                endif()
            else()
                # Static build - use empty for now (already handled in static branch)
                set(JPEG_LINK "")
                set(TURBOJPEG_LINK "")
            endif()
            
            target_link_libraries(openterfaceQT PRIVATE 
                ${FFMPEG_LIBRARIES}
                ${JPEG_LINK}
                ${TURBOJPEG_LINK}
                -lpthread -lm -ldl -lz -llzma -lbz2
                -ldrm -lva -lva-drm -lva-x11 -lvdpau -lX11
                -lxcb -lxcb-shm -lxcb-xfixes -lxcb-shape -lxcb-image
                -lpulse -lpulse-simple
            )
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
