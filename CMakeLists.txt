cmake_minimum_required(VERSION 3.16)
project(openterfaceQT VERSION 1.0 LANGUAGES C CXX)

# Option to control static vs dynamic linking
option(OPENTERFACE_BUILD_STATIC "Link libraries statically where possible" OFF)
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

# Add the executable target
qt_standard_project_setup()

# Include source files from modular file
include(cmake/SourceFiles.cmake)
qt_add_executable(openterfaceQT WIN32 ${SOURCE_FILES})

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

target_link_libraries(openterfaceQT PRIVATE
    Qt::Concurrent
    Qt::Core
    Qt::Gui
    Qt::Multimedia
    Qt::MultimediaWidgets
    Qt::Network
    Qt::SerialPort
    Qt::Svg
    ${HWACCEL_LIBRARIES} # Link against hardware acceleration libraries
    ${X11_LIBRARIES}     # Link against X11 libraries
    ${EXPAT_LIBRARIES}
    ${FREETYPE_LIBRARIES}
    ${FONTCONFIG_LIBRARIES}
    ${XRENDER_LIBRARIES}
    ${BZIP2_LIBRARIES}
    ${EXTRA_LIBS}
    zstd  # Required for Qt resource compression
)

# Add TurboJPEG if available
if(TURBOJPEG_LIBRARY)
    target_link_libraries(openterfaceQT PRIVATE ${TURBOJPEG_LIBRARIES})
endif()

# Add FFmpeg libraries if available (use helper to keep CMakeLists tidy)
add_ffmpeg_static_libraries(openterfaceQT ${QT_BUILD_PATH})

# Add libjpeg libraries if available
if(LIBJPEG_FOUND)
    target_link_libraries(openterfaceQT PRIVATE ${LIBJPEG_LIBRARIES})
    target_include_directories(openterfaceQT PRIVATE ${LIBJPEG_INCLUDE_DIRS})
endif()

# Add GStreamer libraries if available
link_gstreamer_libraries()

# Add DBus on Unix systems (excluding macOS)
if(UNIX AND NOT APPLE)
    target_link_libraries(openterfaceQT PRIVATE Qt::DBus)
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

# Make sure to only include the necessary Qt components
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia MultimediaWidgets)
# Replace with only the modules you actually need