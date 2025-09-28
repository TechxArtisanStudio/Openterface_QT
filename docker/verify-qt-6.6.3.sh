#!/bin/bash

# Qt 6.6.3 Version Verification Script
# This script ensures Qt 6.6.3 is properly installed and available

set -e

echo "============================================"
echo "Qt 6.6.3 Installation Verification"
echo "============================================"

# Check if Qt environment is sourced
if [ -f "/opt/qt6.6.3/setup-qt-env.sh" ]; then
    echo "Sourcing Qt 6.6.3 environment..."
    source /opt/qt6.6.3/setup-qt-env.sh
fi

# Function to check version
check_qt_version() {
    local expected_version="6.6.3"
    local actual_version
    
    if command -v qmake >/dev/null 2>&1; then
        actual_version=$(qmake -query QT_VERSION 2>/dev/null || echo "unknown")
        echo "Qt version detected: $actual_version"
        
        if [ "$actual_version" = "$expected_version" ]; then
            echo "✅ Qt version matches expected version ($expected_version)"
            return 0
        else
            echo "❌ Qt version mismatch!"
            echo "   Expected: $expected_version"
            echo "   Found: $actual_version"
            return 1
        fi
    else
        echo "❌ qmake command not found!"
        return 1
    fi
}

# Function to check Qt installation paths
check_qt_paths() {
    echo ""
    echo "Checking Qt installation paths..."
    
    local qt_prefix=$(qmake -query QT_INSTALL_PREFIX 2>/dev/null || echo "")
    local qt_libs=$(qmake -query QT_INSTALL_LIBS 2>/dev/null || echo "")
    local qt_plugins=$(qmake -query QT_INSTALL_PLUGINS 2>/dev/null || echo "")
    local qt_headers=$(qmake -query QT_INSTALL_HEADERS 2>/dev/null || echo "")
    
    echo "Qt installation prefix: $qt_prefix"
    echo "Qt libraries path: $qt_libs"
    echo "Qt plugins path: $qt_plugins"
    echo "Qt headers path: $qt_headers"
    
    # Verify expected installation prefix
    if [ "$qt_prefix" = "/opt/qt6.6.3" ]; then
        echo "✅ Qt installation prefix is correct"
    else
        echo "❌ Qt installation prefix is incorrect!"
        echo "   Expected: /opt/qt6.6.3"
        echo "   Found: $qt_prefix"
        return 1
    fi
}

# Function to check essential Qt modules
check_qt_modules() {
    echo ""
    echo "Checking Qt modules..."
    
    local modules=("Core" "Widgets" "Gui" "Multimedia" "SerialPort" "Svg")
    local qt_lib_path="/opt/qt6.6.3/lib"
    local failed=0
    
    for module in "${modules[@]}"; do
        local lib_file="${qt_lib_path}/libQt6${module}.so"
        if [ -f "$lib_file" ]; then
            echo "✅ Qt6${module} library found"
        else
            echo "❌ Qt6${module} library NOT found at $lib_file"
            failed=1
        fi
    done
    
    return $failed
}

# Function to check pkg-config files
check_pkg_config() {
    echo ""
    echo "Checking pkg-config modules..."
    
    local modules=("Qt6Core" "Qt6Widgets" "Qt6Gui" "Qt6Multimedia" "Qt6SerialPort" "Qt6Svg")
    local failed=0
    
    for module in "${modules[@]}"; do
        if pkg-config --exists "$module" 2>/dev/null; then
            local version=$(pkg-config --modversion "$module" 2>/dev/null)
            echo "✅ $module pkg-config: OK (version $version)"
        else
            echo "❌ $module pkg-config: FAILED"
            failed=1
        fi
    done
    
    return $failed
}

# Function to test qmake functionality
test_qmake() {
    echo ""
    echo "Testing qmake functionality..."
    
    local temp_dir="/tmp/qt-test-$$"
    mkdir -p "$temp_dir"
    cd "$temp_dir"
    
    # Create a simple test project
    cat > test.pro << 'EOF'
QT += core widgets
CONFIG += c++17
TARGET = qttest
TEMPLATE = app
SOURCES += main.cpp
EOF
    
    cat > main.cpp << 'EOF'
#include <QtCore/QCoreApplication>
#include <QtWidgets/QApplication>
#include <QtCore/QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    qDebug() << "Qt version:" << QT_VERSION_STR;
    return 0;
}
EOF
    
    # Test qmake
    if qmake test.pro >/dev/null 2>&1; then
        echo "✅ qmake can generate Makefile"
        
        # Test if we can at least start the make process (don't need to complete)
        if make -n >/dev/null 2>&1; then
            echo "✅ Generated Makefile is valid"
        else
            echo "⚠️  Generated Makefile has issues"
        fi
    else
        echo "❌ qmake failed to generate Makefile"
        cd /
        rm -rf "$temp_dir"
        return 1
    fi
    
    # Cleanup
    cd /
    rm -rf "$temp_dir"
    return 0
}

# Function to verify environment variables
check_environment() {
    echo ""
    echo "Checking environment variables..."
    
    local vars=("QTDIR" "QT_INSTALL_PREFIX" "PATH" "LD_LIBRARY_PATH" "PKG_CONFIG_PATH" "QT_PLUGIN_PATH")
    
    for var in "${vars[@]}"; do
        local value="${!var}"
        if [ -n "$value" ]; then
            echo "✅ $var is set"
            if [[ "$var" == *"PATH"* ]]; then
                # For PATH variables, check if Qt directories are included
                if [[ "$value" == *"/opt/qt6.6.3"* ]]; then
                    echo "   ✅ Contains Qt 6.6.3 paths"
                else
                    echo "   ⚠️  Does not contain Qt 6.6.3 paths"
                fi
            fi
        else
            echo "❌ $var is not set"
        fi
    done
}

# Main verification process
main() {
    local exit_code=0
    
    echo "Starting Qt 6.6.3 verification..."
    echo ""
    
    check_qt_version || exit_code=1
    check_qt_paths || exit_code=1
    check_qt_modules || exit_code=1
    check_pkg_config || exit_code=1
    check_environment
    test_qmake || exit_code=1
    
    echo ""
    echo "============================================"
    if [ $exit_code -eq 0 ]; then
        echo "✅ Qt 6.6.3 verification PASSED"
        echo "Qt 6.6.3 is properly installed and ready for use"
    else
        echo "❌ Qt 6.6.3 verification FAILED"
        echo "Some issues were found with the Qt installation"
    fi
    echo "============================================"
    
    return $exit_code
}

# Run main function
main "$@"