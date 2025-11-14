#!/bin/bash
# Diagnostic script to identify Qt library conflicts
# Run this to understand which Qt libraries are being loaded from where

echo "=========================================="
echo "Qt6 Library Conflict Diagnostic"
echo "=========================================="
echo "Generated: $(date)"
echo ""

# Check for system Qt libraries
echo "System Qt6 Libraries Found:"
echo "=============================="
if find /lib64 /usr/lib64 -name "libQt6*.so*" -type f 2>/dev/null | head -20 | tee /tmp/system-qt-libs.txt; then
    SYSTEM_QT_COUNT=$(find /lib64 /usr/lib64 -name "libQt6*.so*" -type f 2>/dev/null | wc -l)
    echo ""
    echo "Total system Qt6 libraries: $SYSTEM_QT_COUNT"
else
    echo "(none found)"
fi
echo ""

# Check for bundled Qt libraries
echo "Bundled Qt6 Libraries Found:"
echo "=============================="
if [ -d "/usr/lib/openterfaceqt/qt6" ]; then
    if find /usr/lib/openterfaceqt/qt6 -name "libQt6*.so*" -type f 2>/dev/null | head -20 | tee /tmp/bundled-qt-libs.txt; then
        BUNDLED_QT_COUNT=$(find /usr/lib/openterfaceqt/qt6 -name "libQt6*.so*" -type f 2>/dev/null | wc -l)
        echo ""
        echo "Total bundled Qt6 libraries: $BUNDLED_QT_COUNT"
    else
        echo "(none found)"
    fi
else
    echo "Bundled Qt directory not found: /usr/lib/openterfaceqt/qt6"
fi
echo ""

# Show version information
echo "Qt Library Versions:"
echo "===================="
echo ""
echo "System libQt6Core versions:"
find /lib64 /usr/lib64 -name "libQt6Core.so*" -type f 2>/dev/null | while read lib; do
    echo "  $lib: $(strings "$lib" 2>/dev/null | grep -i "version" | head -3 | tr '\n' '; ')"
done || echo "  (none found)"
echo ""

echo "Bundled libQt6Core versions:"
find /usr/lib/openterfaceqt/qt6 -name "libQt6Core.so*" -type f 2>/dev/null | while read lib; do
    echo "  $lib: $(strings "$lib" 2>/dev/null | grep -i "version" | head -3 | tr '\n' '; ')"
done || echo "  (none found)"
echo ""

# Show what libraries the binary depends on
if [ -f "/usr/bin/openterfaceQT-bin" ]; then
    echo "Binary Dependencies for /usr/bin/openterfaceQT-bin:"
    echo "===================================================="
    echo "(ldd output - shows current dynamic link resolution):"
    echo ""
    ldd /usr/bin/openterfaceQT-bin 2>&1 | grep -i qt || echo "(no Qt dependencies shown)"
    echo ""
else
    echo "Warning: /usr/bin/openterfaceQT-bin not found"
    echo ""
fi

# Check for hidden dependencies like QmlModels
echo "Checking for Hidden Dependencies:"
echo "=================================="
echo ""
echo "libQt6QmlModels locations:"
find /lib64 /usr/lib64 /usr/lib/openterfaceqt -name "*QmlModels*" -type f 2>/dev/null
echo ""

# Generate summary report
echo "Summary Report:"
echo "==============="
echo ""
echo "⚠️  CRITICAL ISSUES TO CHECK:"
echo ""
echo "1. If libQt6QmlModels is only found in /lib64 or /usr/lib64,"
echo "   it means the system Qt has this library but bundled Qt doesn't."
echo "   Solution: Add libQt6QmlModels to the preload list AND"
echo "            ensure bundled version exists or remove system version."
echo ""
echo "2. Version conflicts happen when different Qt versions"
echo "   try to use each other's symbols."
echo "   Solution: Use LD_PRELOAD to force bundled libraries first."
echo ""
echo "3. Check if all system Qt libraries can be removed via:"
echo "   sudo yum remove -y 'qt6-*' OR"
echo "   sudo dnf remove -y 'qt6-*'"
echo ""
echo "4. If bundled libraries are incomplete, copy missing ones from system:"
echo "   sudo cp /usr/lib64/libQt6QmlModels.so.6* /usr/lib/openterfaceqt/qt6/"
echo ""
echo "=========================================="
