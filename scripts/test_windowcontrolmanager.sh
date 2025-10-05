#!/bin/bash
# Quick test script to verify WindowControlManager compilation

echo "=========================================="
echo "Window Control Manager - Compilation Test"
echo "=========================================="
echo ""

# Check if we're in the right directory
if [ ! -f "openterfaceQT.pro" ]; then
    echo "Error: Please run this script from the Openterface_QT project root directory"
    exit 1
fi

echo "Step 1: Checking new files exist..."
if [ -f "ui/windowcontrolmanager.h" ] && [ -f "ui/windowcontrolmanager.cpp" ]; then
    echo "✓ WindowControlManager files found"
else
    echo "✗ WindowControlManager files missing!"
    exit 1
fi

echo ""
echo "Step 2: Checking build configuration files..."
if grep -q "windowcontrolmanager" openterfaceQT.pro; then
    echo "✓ openterfaceQT.pro updated"
else
    echo "✗ openterfaceQT.pro not updated"
    exit 1
fi

if grep -q "windowcontrolmanager" cmake/SourceFiles.cmake; then
    echo "✓ cmake/SourceFiles.cmake updated"
else
    echo "✗ cmake/SourceFiles.cmake not updated"
    exit 1
fi

echo ""
echo "Step 3: Checking MainWindow integration..."
if grep -q "WindowControlManager" ui/mainwindow.h; then
    echo "✓ MainWindow.h includes WindowControlManager"
else
    echo "✗ MainWindow.h not updated"
    exit 1
fi

if grep -q "m_windowControlManager" ui/mainwindow.cpp; then
    echo "✓ MainWindow.cpp uses WindowControlManager"
else
    echo "✗ MainWindow.cpp not updated"
    exit 1
fi

echo ""
echo "=========================================="
echo "All checks passed! ✓"
echo "=========================================="
echo ""
echo "To build the project:"
echo "  mkdir -p build && cd build"
echo "  qmake ../openterfaceQT.pro"
echo "  make -j$(nproc)"
echo ""
echo "Or with CMake:"
echo "  mkdir -p build && cd build"
echo "  cmake .."
echo "  make -j$(nproc)"
echo ""
echo "The WindowControlManager features:"
echo "  • Auto-hides toolbar 10 seconds after window is maximized"
echo "  • Shows toolbar when mouse moves to top edge (5px threshold)"
echo "  • Auto-hides again after inactivity"
echo "  • Prevents hiding when menus are open"
echo ""
