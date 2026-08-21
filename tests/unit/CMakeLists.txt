# Unit tests. Either opt in from the main build
#   cmake -DOPENTERFACE_BUILD_UNIT_TESTS=ON -S . -B build && ctest --test-dir build
# or build this directory standalone:
#   cmake -S tests/unit -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
cmake_minimum_required(VERSION 3.16)
if(NOT PROJECT_NAME)
    project(openterface_unit_tests LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    enable_testing()
endif()
get_filename_component(OPF_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

find_package(Qt6 REQUIRED COMPONENTS Core Test)

add_executable(tst_edididentity
    tst_edididentity.cpp
    ${OPF_REPO_ROOT}/ui/advance/edid/edididentity.cpp
    ${OPF_REPO_ROOT}/ui/advance/edid/edidutils.cpp
    ${OPF_REPO_ROOT}/ui/advance/edid/edidresolutionparser.cpp
)
target_include_directories(tst_edididentity PRIVATE ${OPF_REPO_ROOT})
target_link_libraries(tst_edididentity PRIVATE Qt6::Core Qt6::Test)
set_target_properties(tst_edididentity PROPERTIES AUTOMOC ON)
add_test(NAME edididentity COMMAND tst_edididentity)

add_executable(tst_deviceinfo
    tst_deviceinfo.cpp
    ${OPF_REPO_ROOT}/device/DeviceInfo.cpp
)
target_include_directories(tst_deviceinfo PRIVATE ${OPF_REPO_ROOT})
target_link_libraries(tst_deviceinfo PRIVATE Qt6::Core Qt6::Test)
set_target_properties(tst_deviceinfo PROPERTIES AUTOMOC ON)
add_test(NAME deviceinfo COMMAND tst_deviceinfo)
