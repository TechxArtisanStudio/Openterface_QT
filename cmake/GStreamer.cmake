# GStreamer.cmake - GStreamer configuration and detection

# Option to control GStreamer support
option(USE_GSTREAMER "Enable GStreamer multimedia backend" ON)

# Option to disable static GStreamer plugins (for debugging)
# Note: Static plugins are embedded in the binary for standalone deployment
# If static plugins are not found, the application will use system plugins at runtime
# This requires GStreamer to be installed on the target system
# If you encounter linking errors with static plugins, try setting this to OFF
option(USE_GSTREAMER_STATIC_PLUGINS "Link GStreamer plugins statically" ON)

# Find additional packages required for static GStreamer linking
pkg_check_modules(GUDEV REQUIRED gudev-1.0)
pkg_check_modules(V4L2 REQUIRED libv4l2)

# Find X11 extension libraries required by GStreamer
find_library(XI_LIBRARY Xi REQUIRED)
find_library(XV_LIBRARY Xv REQUIRED)

# Find ORC library for GStreamer - only use pkg-config for dynamic builds
if(NOT OPENTERFACE_BUILD_STATIC)
    pkg_check_modules(ORC orc-0.4)
else()
    # For static builds, we'll manually find the static ORC library later
    message(STATUS "Static build detected - skipping pkg-config for ORC, will use static ORC library")
endif()

# Check for GStreamer (prefer static build from Qt6 installation)
# Use the same search paths as FFmpeg since they're often built together
set(GSTREAMER_SEARCH_PATHS 
    ${QT_BUILD_PATH}
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
        message(STATUS "Static GStreamer plugins requested - enabling static plugin linking")
        add_definitions(-DGSTREAMER_STATIC_LINKING)
        message(STATUS "Will use static GStreamer plugins linked into the binary")
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
    
    # List of core GStreamer libraries to check (with appropriate extension)
    if(OPENTERFACE_BUILD_STATIC)
        # Static build - look for .a files
        set(GSTREAMER_LIB_EXT ".a")
        set(GSTREAMER_PLUGIN_EXT ".a")
        message(STATUS "Using static GStreamer core libraries")
    else()
        # Dynamic build - look for .so files
        set(GSTREAMER_LIB_EXT ".so")
        set(GSTREAMER_PLUGIN_EXT ".so")
        message(STATUS "Using dynamic GStreamer core libraries")
    endif()
    
    set(GSTREAMER_CORE_LIB_CANDIDATES
        "libgstreamer-1.0${GSTREAMER_LIB_EXT}"
        "libgstbase-1.0${GSTREAMER_LIB_EXT}"
        "libgstcontroller-1.0${GSTREAMER_LIB_EXT}"
        "libgstnet-1.0${GSTREAMER_LIB_EXT}"
        "libgstallocators-1.0${GSTREAMER_LIB_EXT}"  # Contains dmabuf and fd allocator functions
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
    
    # List of potential GStreamer video libraries to check (with appropriate extension)
    set(GSTREAMER_VIDEO_LIB_CANDIDATES
        "libgstvideo-1.0${GSTREAMER_LIB_EXT}"
        "libgstaudio-1.0${GSTREAMER_LIB_EXT}"
        "libgsttag-1.0${GSTREAMER_LIB_EXT}"           # Contains tag utilities like gst_tag_list_to_vorbiscomment_buffer
        "libgstpbutils-1.0${GSTREAMER_LIB_EXT}"       # Playback utilities
        "libgstapp-1.0${GSTREAMER_LIB_EXT}"
        "libgstgl-1.0${GSTREAMER_LIB_EXT}"
        "libgstriff-1.0${GSTREAMER_LIB_EXT}"
        "libgstrtp-1.0${GSTREAMER_LIB_EXT}"
        "libgstrtsp-1.0${GSTREAMER_LIB_EXT}"
        "libgstsdp-1.0${GSTREAMER_LIB_EXT}"
        "libgstcodecparsers-1.0${GSTREAMER_LIB_EXT}"  # Codec parsers (may contain bit/byte readers)
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
                file(GLOB AVAILABLE_PLUGINS "${PLUGIN_DIR}/lib*${GSTREAMER_PLUGIN_EXT}")
                if(AVAILABLE_PLUGINS)
                    message(STATUS "    Found plugins: ${AVAILABLE_PLUGINS}")
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
            # Note: DMA buffer and allocator support is provided by libgstallocators-1.0 core library
        )
        
        # Combine all plugins for searching
        set(PLUGIN_CANDIDATES ${ESSENTIAL_PLUGIN_CANDIDATES} ${OPTIONAL_PLUGIN_CANDIDATES})
        
        # Check for each plugin and add if found
        foreach(PLUGIN ${PLUGIN_CANDIDATES})
            # Try multiple possible locations for GStreamer plugins
            set(PLUGIN_PATH_CANDIDATES
                "${GSTREAMER_ROOT_DIR}/lib/gstreamer-1.0/lib${PLUGIN}${GSTREAMER_PLUGIN_EXT}"
                "${GSTREAMER_ROOT_DIR}/lib/x86_64-linux-gnu/gstreamer-1.0/lib${PLUGIN}${GSTREAMER_PLUGIN_EXT}"
                "${GSTREAMER_ROOT_DIR}/lib/aarch64-linux-gnu/gstreamer-1.0/lib${PLUGIN}${GSTREAMER_PLUGIN_EXT}"
                "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/lib${PLUGIN}${GSTREAMER_PLUGIN_EXT}"
                "/usr/lib/aarch64-linux-gnu/gstreamer-1.0/lib${PLUGIN}${GSTREAMER_PLUGIN_EXT}"
                "/usr/local/lib/gstreamer-1.0/lib${PLUGIN}${GSTREAMER_PLUGIN_EXT}"
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

# Configure ORC for static builds
function(configure_gstreamer_orc)
    if(GSTREAMER_FOUND)
        # Add appropriate ORC include and library paths based on build type
        if(OPENTERFACE_BUILD_STATIC)
            # Static build - use static ORC
            target_include_directories(openterfaceQT PRIVATE 
                ${GSTREAMER_INCLUDE_DIRS}
                /opt/orc-static/include/orc-0.4
                ${GUDEV_INCLUDE_DIRS}
                ${V4L2_INCLUDE_DIRS}
            )
            
            # Find the correct static ORC library path
            set(ORC_LIB_CANDIDATES
                "/opt/orc-static/lib/x86_64-linux-gnu/liborc-0.4.a"
                "/opt/orc-static/lib/aarch64-linux-gnu/liborc-0.4.a"
                "/opt/orc-static/lib/liborc-0.4.a"
                "/usr/lib/x86_64-linux-gnu/liborc-0.4.a"
                "/usr/lib/aarch64-linux-gnu/liborc-0.4.a"
            )
        else()
            # Dynamic build - use system ORC
            target_include_directories(openterfaceQT PRIVATE 
                ${GSTREAMER_INCLUDE_DIRS}
                ${GUDEV_INCLUDE_DIRS}
                ${V4L2_INCLUDE_DIRS}
            )
            
            # Find the correct dynamic ORC library path
            set(ORC_LIB_CANDIDATES
                "/usr/lib/x86_64-linux-gnu/liborc-0.4.so"
                "/usr/lib/aarch64-linux-gnu/liborc-0.4.so"
                "/usr/lib/liborc-0.4.so"
                "/usr/local/lib/liborc-0.4.so"
            )
        endif()
        
        # Find the correct ORC library path
        set(ORC_LIB "")
        foreach(ORC_PATH ${ORC_LIB_CANDIDATES})
            if(EXISTS "${ORC_PATH}")
                set(ORC_LIB "${ORC_PATH}")
                message(STATUS "Found ORC library: ${ORC_LIB}")
                break()
            endif()
        endforeach()
        
        # Make ORC_LIB available to parent scope
        set(ORC_LIB ${ORC_LIB} PARENT_SCOPE)
    endif()
endfunction()

# Link GStreamer libraries to target
function(link_gstreamer_libraries)
    if(GSTREAMER_FOUND AND GSTREAMER_VIDEO_FOUND)
        if(EXISTS "${GSTREAMER_ROOT_DIR}/include/gstreamer-1.0/gst/gst.h")
            configure_gstreamer_orc()
            
            # Link GStreamer core libraries and static plugins
            # For static plugins, we need to link them explicitly
            if(USE_GSTREAMER_STATIC_PLUGINS AND GSTREAMER_PLUGIN_LIBRARIES)
                message(STATUS "Linking static GStreamer plugins: ${GSTREAMER_PLUGIN_LIBRARIES}")
                target_link_libraries(openterfaceQT PRIVATE 
                    # Force static ORC first
                    -Wl,--whole-archive
                    ${ORC_LIB}
                    # Link static plugins with whole-archive to ensure registration functions are included
                    ${GSTREAMER_PLUGIN_LIBRARIES}
                    -Wl,--no-whole-archive
                    # Then GStreamer core libraries
                    -Wl,--start-group
                    ${GSTREAMER_LIBRARIES}
                    ${GSTREAMER_VIDEO_LIBRARIES}
                    -Wl,--end-group
                    -lm -pthread
                    -Wl,--allow-multiple-definition
                )
            else()
                message(STATUS "Linking GStreamer core libraries only (no static plugins)")
                target_link_libraries(openterfaceQT PRIVATE 
                    # Force static ORC first
                    -Wl,--whole-archive
                    ${ORC_LIB}
                    -Wl,--no-whole-archive
                    # Then GStreamer libraries
                    -Wl,--start-group
                    ${GSTREAMER_LIBRARIES}
                    ${GSTREAMER_VIDEO_LIBRARIES}
                    -Wl,--end-group
                    -lm -pthread
                    -Wl,--allow-multiple-definition
                )
            endif()
            
            # Add strong linker flags to force static ORC and prevent dynamic loading
            target_link_options(openterfaceQT PRIVATE
                -Wl,--no-as-needed
                -Wl,--exclude-libs,ALL
                -static-libgcc
                -static-libstdc++
                -Wl,--no-undefined-version
                -Wl,--wrap=dlopen
            )
            message(STATUS "Linked static GStreamer with ORC library: ${ORC_LIB}")
            if(USE_GSTREAMER_STATIC_PLUGINS AND GSTREAMER_PLUGIN_LIBRARIES)
                message(STATUS "Added static GStreamer core libraries and static plugins")
            else()
                message(STATUS "Added static GStreamer core libraries (plugins will be loaded dynamically)")
            endif()
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
endfunction()
