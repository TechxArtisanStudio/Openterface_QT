#!/bin/bash
set -e

INSTALL_PREFIX=/opt/Qt6
CMAKE_CONFIG_DIR="$INSTALL_PREFIX/lib/cmake/Qt6"

# Create a BundledHarfbuzz config file
cat > /tmp/Qt6BundledHarfbuzzConfig.cmake << 'EOF'
if(NOT TARGET Qt6::BundledHarfbuzz)
    find_package(harfbuzz CONFIG QUIET)
    if(harfbuzz_FOUND)
        set(QT_BUNDLED_HARFBUZZ_FOUND TRUE)
        add_library(Qt6::BundledHarfbuzz INTERFACE IMPORTED)
        target_link_libraries(Qt6::BundledHarfbuzz INTERFACE harfbuzz::harfbuzz)
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(HARFBUZZ harfbuzz)
            if(HARFBUZZ_FOUND)
                set(QT_BUNDLED_HARFBUZZ_FOUND TRUE)
                add_library(Qt6::BundledHarfbuzz INTERFACE IMPORTED)
                target_include_directories(Qt6::BundledHarfbuzz INTERFACE ${HARFBUZZ_INCLUDE_DIRS})
                target_link_libraries(Qt6::BundledHarfbuzz INTERFACE ${HARFBUZZ_LIBRARIES})
            endif()
        endif()
    endif()

    # Fallback to system harfbuzz if no harfbuzz found through other methods
    if(NOT QT_BUNDLED_HARFBUZZ_FOUND)
        set(QT_BUNDLED_HARFBUZZ_FOUND TRUE)
        add_library(Qt6::BundledHarfbuzz INTERFACE IMPORTED)
        target_link_libraries(Qt6::BundledHarfbuzz INTERFACE harfbuzz)
    endif()
endif()
EOF

sudo mv /tmp/Qt6BundledHarfbuzzConfig.cmake "$CMAKE_CONFIG_DIR/"
echo "Created Qt6BundledHarfbuzzConfig.cmake in $CMAKE_CONFIG_DIR"
