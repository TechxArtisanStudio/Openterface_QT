# FindQt6Required.cmake - Ensure Qt 6.6.3+ is used for the project
#
# This module ensures that Qt 6.6.3 or higher is found and used.
# It will fail the configuration if Qt is not found or if the version is too old.
#
# Usage:
#   find_package(Qt6Required REQUIRED)
#
# This will set up all the standard Qt6 variables and ensure version 6.6.3+

cmake_minimum_required(VERSION 3.16)

# Define minimum required Qt version
set(QT6_MINIMUM_VERSION "6.6.3")

message(STATUS "Searching for Qt 6.6.3 or higher...")

# First, try to find Qt6 in the standard locations
find_package(Qt6 ${QT6_MINIMUM_VERSION} QUIET COMPONENTS Core)

# If Qt6 is not found in standard locations, try our custom installation
if(NOT Qt6_FOUND)
    message(STATUS "Qt6 not found in standard locations, trying /opt/Qt6...")
    
    # Set CMAKE_PREFIX_PATH to include our Qt installation
    list(PREPEND CMAKE_PREFIX_PATH "/opt/Qt6")
    
    # Try to find Qt6 again with our custom path
    find_package(Qt6 ${QT6_MINIMUM_VERSION} QUIET COMPONENTS Core)
    
    if(Qt6_FOUND)
        message(STATUS "Found Qt6 in custom installation: /opt/Qt6")
    endif()
endif()

# If still not found, provide detailed error message
if(NOT Qt6_FOUND)
    message(FATAL_ERROR
        "Qt6 version ${QT6_MINIMUM_VERSION} or higher is required but was not found.\n"
        "Searched locations:\n"
        "  - Standard system paths\n"
        "  - /opt/Qt6 (custom installation)\n"
        "\n"
        "To resolve this issue:\n"
        "1. Ensure Qt 6.6.3+ is installed from source in /opt/Qt6\n"
        "2. Run the installation script: ./docker/install-qt-6.6.3-from-source.sh\n"
        "3. Source the Qt environment: source /opt/Qt6/setup-qt-env.sh\n"
        "4. Set CMAKE_PREFIX_PATH to include Qt: -DCMAKE_PREFIX_PATH=/opt/Qt6\n"
    )
endif()

# Verify the version is actually 6.6.3 or higher
if(Qt6_VERSION VERSION_LESS ${QT6_MINIMUM_VERSION})
    message(FATAL_ERROR
        "Qt6 version ${Qt6_VERSION} was found, but version ${QT6_MINIMUM_VERSION} or higher is required.\n"
        "Found Qt6 installation: ${Qt6_DIR}\n"
        "\n"
        "Please install Qt ${QT6_MINIMUM_VERSION} or higher."
    )
endif()

# Print version information
message(STATUS "✅ Qt6 version ${Qt6_VERSION} found")
message(STATUS "Qt6 installation: ${Qt6_DIR}")
message(STATUS "Qt6 libraries: ${Qt6Core_DIR}/../..")

# Find all the Qt6 components we need
find_package(Qt6 ${QT6_MINIMUM_VERSION} REQUIRED COMPONENTS
    Core
    Widgets
    Gui
    Multimedia
    MultimediaWidgets
    SerialPort
    Svg
    SvgWidgets
)

# Verify all required components were found
set(QT6_REQUIRED_COMPONENTS Core Widgets Gui Multimedia MultimediaWidgets SerialPort Svg SvgWidgets)
set(QT6_MISSING_COMPONENTS "")

foreach(component ${QT6_REQUIRED_COMPONENTS})
    if(NOT TARGET Qt6::${component})
        list(APPEND QT6_MISSING_COMPONENTS ${component})
    endif()
endforeach()

if(QT6_MISSING_COMPONENTS)
    message(FATAL_ERROR
        "The following required Qt6 components were not found:\n"
        "  ${QT6_MISSING_COMPONENTS}\n"
        "\n"
        "Please ensure Qt6 is compiled with all required components:\n"
        "  - Core, Widgets, Gui (basic components)\n"
        "  - Multimedia, MultimediaWidgets (for video capture)\n"
        "  - SerialPort (for serial communication)\n"
        "  - Svg, SvgWidgets (for SVG support)\n"
    )
endif()

# Print found components
message(STATUS "✅ All required Qt6 components found:")
foreach(component ${QT6_REQUIRED_COMPONENTS})
    if(TARGET Qt6::${component})
        message(STATUS "  - Qt6::${component}")
    endif()
endforeach()

# Set up Qt6 for the project
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Make Qt6 variables available to the parent scope
set(Qt6_FOUND ${Qt6_FOUND} PARENT_SCOPE)
set(Qt6_VERSION ${Qt6_VERSION} PARENT_SCOPE)
set(Qt6_DIR ${Qt6_DIR} PARENT_SCOPE)

# Provide a Qt6Required_FOUND variable for find_package
set(Qt6Required_FOUND TRUE)

message(STATUS "✅ Qt6 ${Qt6_VERSION} setup complete")