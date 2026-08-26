cmake_minimum_required(VERSION 3.16)
project(openterface-hotplug-tests LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Test Concurrent SerialPort)

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
