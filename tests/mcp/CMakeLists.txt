# ---------------------------------------------------------------------------
# Openterface MCP — QtTest suite
#
# Build:
#   cmake -DBUILD_TESTING=ON ..
#   cmake --build . --target mcp_tests
#   ctest --output-on-failure
#
# Each test is also individually runnable:
#   ./test_mcp_protocol
#   ./test_mcp_tool_handler
#   ./test_mcp_server
#   ./test_mcp_sse_transport
# ---------------------------------------------------------------------------

find_package(Qt6 REQUIRED COMPONENTS Test)
enable_testing()

# ---------------------------------------------------------------------------
# Common include paths & compile definitions for all MCP tests
# ---------------------------------------------------------------------------
set(MCP_TEST_INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/server/mcp
)

# ---------------------------------------------------------------------------
# 1. TestMcpProtocol — fully self-contained (no hardware deps)
# ---------------------------------------------------------------------------
qt_add_executable(test_mcp_protocol
    test_mcp_protocol.cpp
    ${PROJECT_SOURCE_DIR}/server/mcp/mcpProtocol.cpp
    # logging infrastructure (needed by mcpProtocol.cpp's OPF_LOGGING_CATEGORY)
    ${PROJECT_SOURCE_DIR}/log/logcategoryregistry.cpp
)

target_include_directories(test_mcp_protocol PRIVATE ${MCP_TEST_INCLUDE_DIRS})

target_link_libraries(test_mcp_protocol PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME McpProtocol COMMAND test_mcp_protocol)

# ---------------------------------------------------------------------------
# 2. TestMcpToolHandler — requires full app sources to link (hardware deps)
# ---------------------------------------------------------------------------
# Build a static library from all app sources (minus main.cpp) that tests can
# link against. This is defined once and reused by all integration tests.
# ---------------------------------------------------------------------------

# Gather all app source files (exclude main.cpp) and convert to absolute paths
# (SOURCE_FILES contains relative paths that only resolve from the project root)
set(APP_LIB_SOURCES ${SOURCE_FILES})
list(FILTER APP_LIB_SOURCES EXCLUDE REGEX "main\\.cpp$")

# Convert relative paths to absolute, keeping only .cpp files
set(APP_LIB_ABS_SOURCES)
foreach(src ${APP_LIB_SOURCES})
    if(src MATCHES "\\.cpp$")
        if(IS_ABSOLUTE "${src}")
            list(APPEND APP_LIB_ABS_SOURCES "${src}")
        else()
            list(APPEND APP_LIB_ABS_SOURCES "${PROJECT_SOURCE_DIR}/${src}")
        endif()
    endif()
endforeach()

add_library(openterface_lib STATIC ${APP_LIB_ABS_SOURCES})

target_include_directories(openterface_lib PUBLIC
    ${PROJECT_SOURCE_DIR}
)

# Apply same compile definitions as the main executable
if(UNIX AND NOT APPLE)
    target_compile_definitions(openterface_lib PRIVATE
        _DEFAULT_SOURCE=1
        _GNU_SOURCE=1
        _XOPEN_SOURCE=700
    )
    target_compile_options(openterface_lib PRIVATE -U_POSIX_C_SOURCE)
endif()

if(TESSERACT_FOUND AND LEPTONICA_FOUND)
    target_compile_definitions(openterface_lib PRIVATE HAVE_TESSERACT)
endif()

# Link the same libraries as the main app (minus Widgets for tests that don't need GUI)
target_link_libraries(openterface_lib PUBLIC
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    Qt6::Network
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::SerialPort
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Xml
    Qt6::Svg
    Qt6::SvgWidgets
    Qt6::Concurrent
    Qt6::DBus
    Threads::Threads
)

if(TARGET Qt6::HttpServer)
    target_link_libraries(openterface_lib PUBLIC Qt6::HttpServer)
endif()

if(TESSERACT_FOUND AND LEPTONICA_FOUND)
    target_link_libraries(openterface_lib PUBLIC ${TESSERACT_LIBRARIES} ${LEPTONICA_LIBRARIES})
    target_include_directories(openterface_lib PUBLIC ${TESSERACT_INCLUDE_DIRS} ${LEPTONICA_INCLUDE_DIRS})
endif()

if(TURBOJPEG_FOUND)
    target_link_libraries(openterface_lib PUBLIC ${TURBOJPEG_LIBRARIES})
endif()

if(OpenCV_FOUND)
    target_include_directories(openterface_lib PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(openterface_lib PUBLIC ${OpenCV_LIBRARIES})
endif()

if(UNIX AND NOT APPLE)
    # LinuxDeviceManager (added to main exe via Resources.cmake but not in SOURCE_FILES)
    target_sources(openterface_lib PRIVATE
        ${PROJECT_SOURCE_DIR}/device/platform/LinuxDeviceManager.cpp
    )

    # libudev for Linux device detection
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(UDEV libudev)
        if(UDEV_FOUND)
            target_link_libraries(openterface_lib PUBLIC ${UDEV_LIBRARIES})
            target_include_directories(openterface_lib PUBLIC ${UDEV_INCLUDE_DIRS})
            target_compile_definitions(openterface_lib PRIVATE HAVE_LIBUDEV)
        endif()
    endif()

    # GStreamer (needed by gstreamerbackendhandler.cpp)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GSTREAMER_LIBS gstreamer-1.0 gstreamer-video-1.0 gstreamer-app-1.0)
        if(GSTREAMER_LIBS_FOUND)
            target_include_directories(openterface_lib PUBLIC ${GSTREAMER_LIBS_INCLUDE_DIRS})
            target_link_libraries(openterface_lib PUBLIC ${GSTREAMER_LIBS_LIBRARIES})
        endif()
    endif()

    target_link_libraries(openterface_lib PUBLIC
        ${X11_LIBRARIES}
        xkbcommon
    )
    if(WAYLAND_CLIENT_LIBRARIES)
        target_link_libraries(openterface_lib PUBLIC ${WAYLAND_CLIENT_LIBRARIES})
    endif()
endif()

# ---------------------------------------------------------------------------
# Test: McpToolHandler
# ---------------------------------------------------------------------------
qt_add_executable(test_mcp_tool_handler
    test_mcp_tool_handler.cpp
    test_stubs.cpp
)

target_include_directories(test_mcp_tool_handler PRIVATE ${MCP_TEST_INCLUDE_DIRS})

target_link_libraries(test_mcp_tool_handler PRIVATE
    openterface_lib
    Qt6::Test
)

add_test(NAME McpToolHandler COMMAND test_mcp_tool_handler)

# ---------------------------------------------------------------------------
# Test: McpServer
# ---------------------------------------------------------------------------
qt_add_executable(test_mcp_server
    test_mcp_server.cpp
    test_stubs.cpp
)

target_include_directories(test_mcp_server PRIVATE ${MCP_TEST_INCLUDE_DIRS})

target_link_libraries(test_mcp_server PRIVATE
    openterface_lib
    Qt6::Test
)

add_test(NAME McpServer COMMAND test_mcp_server)

# ---------------------------------------------------------------------------
# Test: McpSseTransport
# ---------------------------------------------------------------------------
qt_add_executable(test_mcp_sse_transport
    test_mcp_sse_transport.cpp
    test_stubs.cpp
)

target_include_directories(test_mcp_sse_transport PRIVATE ${MCP_TEST_INCLUDE_DIRS})

target_link_libraries(test_mcp_sse_transport PRIVATE
    openterface_lib
    Qt6::Test
)

add_test(NAME McpSseTransport COMMAND test_mcp_sse_transport)
