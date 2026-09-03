cmake_minimum_required(VERSION 3.16)
project(openterface-hotplug-tests LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Test Concurrent SerialPort Widgets Gui Network)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Include paths for production code
include_directories(
    ${PROJECT_ROOT}
    ${PROJECT_ROOT}/device
    ${PROJECT_ROOT}/device/platform
    ${PROJECT_ROOT}/log
)

# Include paths for test mocks
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/hotplug/mock)

# Production sources under test
set(HOTPLUG_SOURCES
    ${PROJECT_ROOT}/device/HotplugMonitor.cpp
    ${PROJECT_ROOT}/device/HotplugDebounceManager.cpp
    ${PROJECT_ROOT}/device/DeviceInfo.cpp
    ${PROJECT_ROOT}/log/logcategoryregistry.cpp
)

# Test 1: HotplugDebounceManager
add_executable(test_hotplug_debounce
    hotplug/test_hotplug_debounce.cpp
    ${PROJECT_ROOT}/device/HotplugDebounceManager.cpp
    ${PROJECT_ROOT}/log/logcategoryregistry.cpp
)
target_link_libraries(test_hotplug_debounce PRIVATE Qt6::Core Qt6::Test)

# Test 2: HotplugMonitor
add_executable(test_hotplug_monitor
    hotplug/test_hotplug_monitor.cpp
    ${HOTPLUG_SOURCES}
)
target_link_libraries(test_hotplug_monitor PRIVATE Qt6::Core Qt6::Test Qt6::Concurrent)

# Test 3: End-to-end scenarios
add_executable(test_hotplug_scenarios
    hotplug/test_hotplug_scenarios.cpp
    ${HOTPLUG_SOURCES}
)
target_link_libraries(test_hotplug_scenarios PRIVATE Qt6::Core Qt6::Test Qt6::Concurrent)

enable_testing()
add_test(NAME HotplugDebounce COMMAND test_hotplug_debounce)
add_test(NAME HotplugMonitor COMMAND test_hotplug_monitor)
add_test(NAME HotplugScenarios COMMAND test_hotplug_scenarios)

# Test 4: Four-subsystem integration
add_executable(test_subsystem_integration
    hotplug/test_subsystem_integration.cpp
    hotplug/mock/SubsystemHandlerSimulator.h
    ${HOTPLUG_SOURCES}
)
target_link_libraries(test_subsystem_integration PRIVATE Qt6::Core Qt6::Test Qt6::Concurrent)
add_test(NAME SubsystemIntegration COMMAND test_subsystem_integration)

# Test 5: Serial port race condition tests (Windows 11 crash scenario)
add_executable(test_serial_port_race
    hotplug/test_serial_port_race.cpp
    hotplug/mock/SerialPortRaceSimulator.h
    ${HOTPLUG_SOURCES}
)
target_link_libraries(test_serial_port_race PRIVATE Qt6::Core Qt6::Test Qt6::Concurrent Qt6::SerialPort)
add_test(NAME SerialPortRace COMMAND test_serial_port_race)

# Test 6: Firmware flashing systematic tests (Linux serial port reconnection)
add_executable(test_firmware_flashing
    hotplug/test_firmware_flashing.cpp
    ${HOTPLUG_SOURCES}
)
target_link_libraries(test_firmware_flashing PRIVATE Qt6::Core Qt6::Test Qt6::Concurrent Qt6::SerialPort)
add_test(NAME FirmwareFlashing COMMAND test_firmware_flashing)

# Test 7: Real firmware flashing integration test (requires hardware)
# This test actually flashes firmware to a real device
# Requires WCH library and actual hardware
find_library(WCH_LIB NAMES WCH CH375DLL64 PATHS ${PROJECT_ROOT}/lib)
if(WCH_LIB OR UNIX)  # On Unix, we use libusb which should be available
    add_executable(test_real_firmware_flashing
        hotplug/test_real_firmware_flashing.cpp
        ${PROJECT_ROOT}/wch/WCHFlasher.cpp
        ${PROJECT_ROOT}/wch/WCHUSBTransport.cpp
        ${PROJECT_ROOT}/wch/WCHProtocol.cpp
        ${PROJECT_ROOT}/wch/WCHHexParser.cpp
        ${PROJECT_ROOT}/wch/WCHDevice.cpp
    )
    target_link_libraries(test_real_firmware_flashing
        PRIVATE Qt6::Core Qt6::Test Qt6::SerialPort)

    # Link libusb on Linux/Mac, WCH driver on Windows
    if(UNIX)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
        target_include_directories(test_real_firmware_flashing PRIVATE ${LIBUSB_INCLUDE_DIRS})
        target_link_libraries(test_real_firmware_flashing PRIVATE ${LIBUSB_LIBRARIES})
    elseif(WIN32)
        if(WCH_LIB)
            target_link_libraries(test_real_firmware_flashing PRIVATE ${WCH_LIB})
        endif()
        target_link_libraries(test_real_firmware_flashing PRIVATE setupapi)
    endif()

    add_test(NAME RealFirmwareFlashing COMMAND test_real_firmware_flashing)
endif()

# Test 8: Keyboard routing tests (SystemKeyBlocker ON/OFF)
add_executable(test_keyboard_routing
    keyboard/test_keyboard_routing.cpp
    ${PROJECT_ROOT}/SysKeyBlocker/SystemKeyBlocker.cpp
    ${PROJECT_ROOT}/log/logcategoryregistry.cpp
)

# Platform-specific SystemKeyBlocker implementation
if(WIN32)
    target_sources(test_keyboard_routing PRIVATE
        ${PROJECT_ROOT}/SysKeyBlocker/SystemKeyBlocker_win.cpp
    )
else()
    target_sources(test_keyboard_routing PRIVATE
        ${PROJECT_ROOT}/SysKeyBlocker/SystemKeyBlocker_x11.cpp
    )
    target_link_libraries(test_keyboard_routing PRIVATE X11)
endif()

target_include_directories(test_keyboard_routing PRIVATE
    ${PROJECT_ROOT}
    ${PROJECT_ROOT}/SysKeyBlocker
)
target_link_libraries(test_keyboard_routing PRIVATE Qt6::Core Qt6::Test Qt6::Widgets Qt6::Gui)
add_test(NAME KeyboardRouting COMMAND test_keyboard_routing)

# Test 9: Keyboard event handling logic (no GUI required)
add_executable(test_keyboard_logic
    keyboard/test_keyboard_logic.cpp
)
target_link_libraries(test_keyboard_logic PRIVATE Qt6::Core Qt6::Test Qt6::Gui)
add_test(NAME KeyboardLogic COMMAND test_keyboard_logic)

# Test 10: Web Search Providers and Manager
add_executable(test_web_search_providers
    test_web_search_providers.cpp
    ${PROJECT_ROOT}/ai/WebSearchManager.cpp
    ${PROJECT_ROOT}/ai/WebSearchProviders.cpp
    ${PROJECT_ROOT}/ai/WebSearchProvider.cpp
    ${PROJECT_ROOT}/ui/globalsetting.cpp
    ${PROJECT_ROOT}/log/logcategoryregistry.cpp
)
target_include_directories(test_web_search_providers PRIVATE
    ${PROJECT_ROOT}
    ${PROJECT_ROOT}/ai
    ${PROJECT_ROOT}/ui
    ${PROJECT_ROOT}/log
)
target_link_libraries(test_web_search_providers PRIVATE Qt6::Core Qt6::Test Qt6::Network)
add_test(NAME WebSearchProviders COMMAND test_web_search_providers)
