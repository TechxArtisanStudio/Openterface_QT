# SysKeyBlocker - System keyboard capture library
# Intercepts system keys (Win, PrintScreen, Alt+Tab) and forwards to target

cmake_minimum_required(VERSION 3.16)
project(SysKeyBlocker VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
endif()

set(SOURCES
    SystemKeyBlocker.cpp
)

set(HEADERS
    SystemKeyBlocker.h
)

# Platform-specific sources
if(WIN32)
    list(APPEND SOURCES SystemKeyBlocker_win.cpp)
elseif(UNIX AND NOT APPLE)
    list(APPEND SOURCES SystemKeyBlocker_x11.cpp)
endif()

add_library(SysKeyBlocker STATIC ${SOURCES} ${HEADERS})

target_include_directories(SysKeyBlocker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SysKeyBlocker PUBLIC Qt6::Core Qt6::Widgets)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(SysKeyBlocker PRIVATE user32)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(SysKeyBlocker PRIVATE ${X11_LIBRARIES})
    target_include_directories(SysKeyBlocker PRIVATE ${X11_INCLUDE_DIR})
endif()

# Logging category definitions
target_compile_definitions(SysKeyBlocker PRIVATE
    QLOGGING_CATEGORY=log_syskey="opf.systemkey"
    QLOGGING_CATEGORY=log_syskey_win="opf.systemkey.win"
    QLOGGING_CATEGORY=log_syskey_x11="opf.systemkey.x11"
)
