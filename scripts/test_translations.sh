#!/bin/bash

# test_translations.sh - Test script to verify translation setup
# This script tests the translation integration without building the entire application

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.."
cd "$PROJECT_ROOT"

echo "=== Translation Setup Test ==="
echo "Project root: $PROJECT_ROOT"
echo ""

# Test 1: Check if required files exist
echo "Test 1: Checking required files..."
required_files=(
    "openterfaceQT.pro"
    "cmake/Internationalization.cmake" 
    "scripts/update_translations.sh"
    "config/languages/language.qrc"
)

for file in "${required_files[@]}"; do
    if [ -f "$file" ]; then
        echo "✓ $file exists"
    else
        echo "✗ $file missing"
    fi
done
echo ""

# Test 2: Check translation source files (.ts)
echo "Test 2: Checking translation source files..."
ts_files=(
    "config/languages/openterface_da.ts"
    "config/languages/openterface_de.ts"
    "config/languages/openterface_en.ts"
    "config/languages/openterface_fr.ts"
    "config/languages/openterface_ja.ts"
    "config/languages/openterface_se.ts"
    "config/languages/openterface_zh.ts"
)

ts_count=0
for ts_file in "${ts_files[@]}"; do
    if [ -f "$ts_file" ]; then
        echo "✓ $ts_file exists"
        ts_count=$((ts_count + 1))
    else
        echo "✗ $ts_file missing"
    fi
done
echo "Found $ts_count translation source files"
echo ""

# Test 3: Check if Qt6 tools are available
echo "Test 3: Checking Qt6 tools availability..."

# Function to find Qt6 tools (same as in update_translations.sh)
find_qt_tool() {
    local tool_name=$1
    local qt_tool_path=""
    
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

LUPDATE=$(find_qt_tool "lupdate")
LRELEASE=$(find_qt_tool "lrelease")

if [ -n "$LUPDATE" ]; then
    echo "✓ lupdate found: $LUPDATE"
    lupdate_version=$("$LUPDATE" -version 2>&1 | head -n1)
    echo "  Version: $lupdate_version"
else
    echo "✗ lupdate not found"
fi

if [ -n "$LRELEASE" ]; then
    echo "✓ lrelease found: $LRELEASE"
    lrelease_version=$("$LRELEASE" -version 2>&1 | head -n1)
    echo "  Version: $lrelease_version"
else
    echo "✗ lrelease not found"
fi
echo ""

# Test 4: Check .pro file TRANSLATIONS section
echo "Test 4: Checking .pro file TRANSLATIONS section..."
if [ -f "openterfaceQT.pro" ]; then
    if grep -q "TRANSLATIONS" openterfaceQT.pro; then
        echo "✓ TRANSLATIONS section found in .pro file"
        echo "Listed translation files:"
        grep -A 10 "TRANSLATIONS" openterfaceQT.pro | grep "config/languages" | sed 's/^/  /'
    else
        echo "✗ TRANSLATIONS section not found in .pro file"
    fi
else
    echo "✗ openterfaceQT.pro file not found"
fi
echo ""

# Test 5: Check CMake integration
echo "Test 5: Checking CMake translation integration..."
if [ -f "cmake/Internationalization.cmake" ]; then
    echo "✓ CMake internationalization file exists"
    
    if grep -q "qt6_add_lupdate\|qt6_add_lrelease" cmake/Internationalization.cmake; then
        echo "✓ Qt6 LinguistTools integration found"
    else
        echo "⚠ Qt6 LinguistTools integration not found (fallback mode available)"
    fi
    
    if grep -q "update_translations.sh" cmake/Internationalization.cmake; then
        echo "✓ Script fallback integration found"
    else
        echo "✗ Script fallback integration not found"
    fi
else
    echo "✗ CMake internationalization file not found"
fi
echo ""

# Test 6: Test script permissions and basic functionality
echo "Test 6: Testing update script..."
if [ -f "scripts/update_translations.sh" ]; then
    if [ -x "scripts/update_translations.sh" ]; then
        echo "✓ Translation update script is executable"
        
        # Try to run script in dry-run mode (check tools only)
        if [ -n "$LUPDATE" ] && [ -n "$LRELEASE" ]; then
            echo "✓ Required tools available - script should work"
            echo "  Run './scripts/update_translations.sh' to test full functionality"
        else
            echo "⚠ Some Qt6 tools missing - script may fail"
            echo "  Install Qt6 development tools and try again"
        fi
    else
        echo "✗ Translation update script is not executable"
        echo "  Run: chmod +x scripts/update_translations.sh"
    fi
else
    echo "✗ Translation update script not found"
fi
echo ""

# Test 7: Check compiled translation files (.qm)
echo "Test 7: Checking compiled translation files..."
qm_files=(
    "config/languages/openterface_da.qm"
    "config/languages/openterface_de.qm"
    "config/languages/openterface_en.qm"
    "config/languages/openterface_fr.qm"
    "config/languages/openterface_ja.qm"
    "config/languages/openterface_se.qm"
    "config/languages/openterface_zh.qm"
)

qm_count=0
for qm_file in "${qm_files[@]}"; do
    if [ -f "$qm_file" ]; then
        echo "✓ $qm_file exists"
        qm_count=$((qm_count + 1))
    else
        echo "⚠ $qm_file missing (will be generated by lupdate/lrelease)"
    fi
done
echo "Found $qm_count compiled translation files"
echo ""

# Summary
echo "=== Test Summary ==="
echo "Translation source files: $ts_count/$((${#ts_files[@]}))"
echo "Compiled translation files: $qm_count/$((${#qm_files[@]}))"

if [ -n "$LUPDATE" ] && [ -n "$LRELEASE" ]; then
    echo "Qt6 tools: ✓ Available"
    echo ""
    echo "✅ Translation setup appears to be working!"
    echo ""
    echo "Next steps:"
    echo "1. Run './scripts/update_translations.sh' to update/compile translations"
    echo "2. Build project with CMake (translations will be processed automatically)"
    echo "3. Or build with qmake after running the translation script"
else
    echo "Qt6 tools: ✗ Missing"
    echo ""
    echo "⚠️ Translation setup incomplete!"
    echo ""
    echo "Required actions:"
    echo "1. Install Qt6 development tools (qt6-tools-dev)"
    echo "2. Run './scripts/update_translations.sh' to test"
    echo "3. Proceed with normal build process"
fi
