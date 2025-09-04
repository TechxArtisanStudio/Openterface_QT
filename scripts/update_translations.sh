#!/bin/bash

# update_translations.sh - Script to update and compile Qt translations
# This script should be run before building the application

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_ROOT"

echo "=== Qt Translation Update Script ==="
echo "Working directory: $PROJECT_ROOT"

# Function to find Qt6 tools
find_qt_tool() {
    local tool_name=$1
    local qt_tool_path=""
    
    # Common Qt6 installation paths
    local qt_paths=(
        "/opt/Qt6/bin"
        "/usr/bin"
        "/usr/local/bin"
        "$(which qmake 2>/dev/null | xargs dirname 2>/dev/null)"
        "/usr/lib/qt6/bin"
        "/usr/local/lib/qt6/bin"
    )
    
    for path in "${qt_paths[@]}"; do
        if [ -f "$path/$tool_name" ]; then
            qt_tool_path="$path/$tool_name"
            break
        fi
    done
    
    echo "$qt_tool_path"
}

# Find lupdate and lrelease
LUPDATE=$(find_qt_tool "lupdate")
LRELEASE=$(find_qt_tool "lrelease")

# Check if tools are available
if [ -z "$LUPDATE" ]; then
    echo "Error: lupdate not found. Please install Qt6 development tools."
    echo "Searched paths: /opt/Qt6/bin, /usr/bin, /usr/local/bin, /usr/lib/qt6/bin"
    exit 1
fi

if [ -z "$LRELEASE" ]; then
    echo "Error: lrelease not found. Please install Qt6 development tools."
    echo "Searched paths: /opt/Qt6/bin, /usr/bin, /usr/local/bin, /usr/lib/qt6/bin"
    exit 1
fi

echo "Found lupdate: $LUPDATE"
echo "Found lrelease: $LRELEASE"
echo ""

# Check if .pro file exists
if [ ! -f "openterfaceQT.pro" ]; then
    echo "Error: openterfaceQT.pro not found in current directory"
    echo "Please run this script from the project root directory"
    exit 1
fi

# Update translation files (.ts) from source code
echo "=== Updating translation files with lupdate ==="
echo "Extracting translatable strings from source code..."
"$LUPDATE" openterfaceQT.pro -no-obsolete
if [ $? -eq 0 ]; then
    echo "✓ Translation files updated successfully"
else
    echo "⚠ lupdate completed with warnings/errors"
fi
echo ""

# Compile translation files (.ts -> .qm)
echo "=== Compiling translation files with lrelease ==="
echo "Converting .ts files to .qm files..."
"$LRELEASE" openterfaceQT.pro
if [ $? -eq 0 ]; then
    echo "✓ Translation files compiled successfully"
else
    echo "⚠ lrelease completed with warnings/errors"
fi
echo ""

# Verify compiled translation files
echo "=== Verifying compiled translation files ==="
QM_FILES=config/languages/*.qm
qm_count=0
for qm_file in $QM_FILES; do
    if [ -f "$qm_file" ]; then
        echo "✓ Found: $qm_file"
        qm_count=$((qm_count + 1))
    fi
done

if [ $qm_count -eq 0 ]; then
    echo "⚠ No .qm files found in config/languages/"
else
    echo "✓ Found $qm_count compiled translation files"
fi
echo ""

# List available translation files
echo "=== Available Translations ==="
TS_FILES=config/languages/*.ts
for ts_file in $TS_FILES; do
    if [ -f "$ts_file" ]; then
        basename_file=$(basename "$ts_file" .ts)
        lang_code=${basename_file#openterface_}
        qm_file="config/languages/openterface_${lang_code}.qm"
        
        if [ -f "$qm_file" ]; then
            echo "✓ $lang_code: $ts_file -> $qm_file"
        else
            echo "✗ $lang_code: $ts_file (compilation failed)"
        fi
    fi
done
echo ""

echo "=== Translation update completed ==="
echo "You can now proceed with building the application using CMake or qmake"
echo ""
echo "For CMake build:"
echo "  mkdir -p build && cd build"
echo "  cmake .."
echo "  make"
echo ""
echo "For qmake build:"
echo "  qmake"
echo "  make"
