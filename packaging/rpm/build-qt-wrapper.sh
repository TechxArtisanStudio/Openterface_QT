#!/bin/bash
# Build and install Qt version wrapper library for OpenterfaceQT
# This wrapper prevents system Qt6 libraries (e.g., Fedora's Qt 6.9) from conflicting
# with bundled Qt 6.6.3

set -e

# Script location
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WRAPPER_SOURCE="$SCRIPT_DIR/qt_version_wrapper.c"
WRAPPER_OUTPUT="$SCRIPT_DIR/qt_version_wrapper.so"

# Installation paths
BUNDLED_QT_PATH="${1:-/usr/lib/openterfaceqt/qt6}"
INSTALL_PATH="${2:-/usr/lib/openterfaceqt}"

echo "=========================================="
echo "Building Qt Version Wrapper"
echo "=========================================="
echo "Source: $WRAPPER_SOURCE"
echo "Output: $WRAPPER_OUTPUT"
echo "Bundled Qt path: $BUNDLED_QT_PATH"
echo "Install path: $INSTALL_PATH"
echo ""

# Check if source exists
if [ ! -f "$WRAPPER_SOURCE" ]; then
    echo "ERROR: Source file not found: $WRAPPER_SOURCE"
    exit 1
fi

# Compile the wrapper
echo "Compiling qt_version_wrapper.c..."
gcc -shared -fPIC \
    -DBUNDLED_QT_PATH=\"$BUNDLED_QT_PATH\" \
    -o "$WRAPPER_OUTPUT" \
    "$WRAPPER_SOURCE" \
    -ldl

if [ ! -f "$WRAPPER_OUTPUT" ]; then
    echo "ERROR: Compilation failed"
    exit 1
fi

echo "✅ Compiled successfully: $WRAPPER_OUTPUT"
echo "Size: $(stat -f%z "$WRAPPER_OUTPUT" 2>/dev/null || stat -c%s "$WRAPPER_OUTPUT") bytes"
echo ""

# Optional: Install to system
if [ "$3" = "--install" ]; then
    echo "Installing to $INSTALL_PATH..."
    
    # Create directory if needed
    sudo mkdir -p "$INSTALL_PATH"
    
    # Copy the library
    sudo cp "$WRAPPER_OUTPUT" "$INSTALL_PATH/qt_version_wrapper.so"
    
    # Set permissions
    sudo chmod 644 "$INSTALL_PATH/qt_version_wrapper.so"
    
    echo "✅ Installed to $INSTALL_PATH/qt_version_wrapper.so"
    echo ""
    echo "You can now use the launcher:"
    echo "  /usr/bin/openterfaceQT"
    exit 0
fi

echo "To install the wrapper:"
echo "  $0 \"$BUNDLED_QT_PATH\" \"$INSTALL_PATH\" --install"
echo ""
echo "Or manually install:"
echo "  sudo cp $WRAPPER_OUTPUT $INSTALL_PATH/qt_version_wrapper.so"
echo "  sudo chmod 644 $INSTALL_PATH/qt_version_wrapper.so"
