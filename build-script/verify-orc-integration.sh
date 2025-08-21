#!/bin/bash

echo "ORC Integration Verification"
echo "============================"

# Check if the build script contains the ORC building section
BUILD_SCRIPT="$(dirname "$0")/build-static-qt-for-arm64.sh"

if [ ! -f "$BUILD_SCRIPT" ]; then
    echo "✗ Build script not found: $BUILD_SCRIPT"
    exit 1
fi

echo "Checking build script integration..."

# Check for ORC building section
if grep -q "Building static ORC library" "$BUILD_SCRIPT"; then
    echo "✓ ORC building section found in build script"
else
    echo "✗ ORC building section missing from build script"
    exit 1
fi

# Check for ORC download
if grep -q "orc-0.4.33.tar.xz" "$BUILD_SCRIPT"; then
    echo "✓ ORC download section found"
else
    echo "✗ ORC download section missing"
fi

# Check for static ORC library path
if grep -q "/opt/orc-static" "$BUILD_SCRIPT"; then
    echo "✓ Static ORC installation path configured"
else
    echo "✗ Static ORC installation path missing"
fi

# Check for meson configuration
if grep -q "meson setup build --prefix=/opt/orc-static --default-library=static" "$BUILD_SCRIPT"; then
    echo "✓ Meson static configuration found"
else
    echo "✗ Meson static configuration missing"
fi

# Check if ORC installation is mentioned in the summary
if grep -q "Static ORC library installed" "$BUILD_SCRIPT"; then
    echo "✓ ORC installation mentioned in build summary"
else
    echo "✗ ORC installation not mentioned in build summary"
fi

# Check if ORC troubleshooting is included
if grep -q "ORC library linking errors" "$BUILD_SCRIPT"; then
    echo "✓ ORC troubleshooting section found"
else
    echo "✗ ORC troubleshooting section missing"
fi

echo ""
echo "Checking if static ORC library exists (if already built)..."
if [ -f "/opt/orc-static/lib/aarch64-linux-gnu/liborc-0.4.a" ]; then
    echo "✓ Static ORC library already exists:"
    ls -la /opt/orc-static/lib/aarch64-linux-gnu/liborc-0.4.a
    echo "  Library info:"
    file /opt/orc-static/lib/aarch64-linux-gnu/liborc-0.4.a
else
    echo "ℹ Static ORC library not yet built (this is normal for a fresh environment)"
    echo "  It will be built when the build script runs"
fi

echo ""
echo "Checking build script syntax..."
if bash -n "$BUILD_SCRIPT"; then
    echo "✓ Build script syntax is valid"
else
    echo "✗ Build script has syntax errors"
    exit 1
fi

echo ""
echo "Integration verification complete!"
echo ""
echo "To build the static Qt environment with ORC support in a new environment, run:"
echo "  bash $(realpath "$BUILD_SCRIPT")"
echo ""
echo "This will:"
echo "1. Install system dependencies including ORC dev headers"
echo "2. Download and build static ORC library to /opt/orc-static/"
echo "3. Build GStreamer with static ORC linking"
echo "4. Build Qt with multimedia support"
echo "5. Copy all libraries to ${QT_TARGET_DIR:-/opt/Qt6-arm64}"
