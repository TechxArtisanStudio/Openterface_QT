#!/bin/bash
#
# Enhanced UI Test with Image Comparison
# Tests File -> Preferences menu with visual verification
#

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
OUTPUT_DIR="${OUTPUT_DIR:-/tmp/openterface_ui_test_v2}"
LOG_FILE="$OUTPUT_DIR/test.log"
DISPLAY_NUM=":99"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
print_step() { echo -e "${BLUE}[STEP]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }
print_success() { echo -e "${GREEN}[✓]${NC} $1"; }
print_fail() { echo -e "${RED}[✗]${NC} $1"; }

find_appimage() {
    local appimage=$(find "$BUILD_DIR" -maxdepth 1 -name "*.AppImage" -type f | head -n 1)
    [ -z "$appimage" ] && { print_error "No AppImage found"; exit 1; }
    chmod +x "$appimage" 2>/dev/null || true
    echo "$appimage"
}

capture_screenshot() {
    local name="$1"
    local filepath="$OUTPUT_DIR/${name}.png"
    DISPLAY=$DISPLAY_NUM scrot "$filepath" 2>/dev/null || true
    echo "$filepath"
}

compare_images() {
    local img1="$1"
    local img2="$2"
    
    if ! command -v compare &> /dev/null; then
        # Fallback to file size comparison
        local size1=$(stat -c%s "$img1" 2>/dev/null || echo 0)
        local size2=$(stat -c%s "$img2" 2>/dev/null || echo 0)
        local diff=$((size1 - size2))
        [ $diff -lt 0 ] && diff=$((-diff))
        
        # If difference is more than 1KB, consider them different
        if [ $diff -gt 1024 ]; then
            return 0  # Different
        else
            return 1  # Same
        fi
    fi
    
    # Use ImageMagick compare if available
    local diff_score=$(compare -metric AE "$img1" "$img2" /dev/null 2>&1 || echo "999999")
    if [ "$diff_score" -gt 100 ]; then
        return 0  # Different enough
    else
        return 1  # Too similar
    fi
}

test_menu_interaction() {
    local appimage="$1"
    
    mkdir -p "$OUTPUT_DIR"
    
    print_info "Starting test with better keyboard simulation..."
    
    # Start Xvfb
    Xvfb $DISPLAY_NUM -screen 0 1920x1080x24 2>/dev/null &
    local xvfb_pid=$!
    sleep 2
    export DISPLAY=$DISPLAY_NUM
    
    # Start app
    "$appimage" &> "$LOG_FILE" &
    local app_pid=$!
    sleep 5  # Longer wait for full initialization
    
    print_step "1️⃣  Capturing baseline (initial state)"
    local img1=$(capture_screenshot "01_baseline")
    sleep 1
    
    print_step "2️⃣  Attempting to open File menu..."
    # Try multiple methods
    
    # Method 1: Alt+F
    print_info "   Method 1: Alt+F"
    DISPLAY=$DISPLAY_NUM xdotool key alt+f 2>/dev/null
    sleep 1
    local img2=$(capture_screenshot "02_method1_altf")
    
    # Close any open menu
    DISPLAY=$DISPLAY_NUM xdotool key Escape 2>/dev/null
    sleep 0.5
    
    # Method 2: F10 (activates menu bar) then Right
    print_info "   Method 2: F10 then Right"
    DISPLAY=$DISPLAY_NUM xdotool key F10 2>/dev/null
    sleep 0.5
    DISPLAY=$DISPLAY_NUM xdotool key Right 2>/dev/null  
    sleep 1
    local img3=$(capture_screenshot "03_method2_f10")
    
    DISPLAY=$DISPLAY_NUM xdotool key Escape 2>/dev/null
    sleep 0.5
    
    # Method 3: Click at menu position
    print_info "   Method 3: Mouse click at File menu position"
    # Focus window first
    DISPLAY=$DISPLAY_NUM xdotool search --class "" windowactivate --sync 2>/dev/null || true
    sleep 0.5
    # Click where File menu should be (top-left area)
    DISPLAY=$DISPLAY_NUM xdotool mousemove 40 10 2>/dev/null
    sleep 0.3
    DISPLAY=$DISPLAY_NUM xdotool click 1 2>/dev/null
    sleep 1
    local img4=$(capture_screenshot "04_method3_click")
    
    # Try to select Preferences
    print_step "3️⃣  Attempting to open Preferences..."
    DISPLAY=$DISPLAY_NUM xdotool key p 2>/dev/null
    sleep 2
    local img5=$(capture_screenshot "05_preferences_attempt")
    
    # Close
    DISPLAY=$DISPLAY_NUM xdotool key Escape Escape 2>/dev/null
    sleep 1
    local img6=$(capture_screenshot "06_final")
    
    # Cleanup
    kill $app_pid 2>/dev/null || true
    wait $app_pid 2>/dev/null || true
    kill $xvfb_pid 2>/dev/null || true
    
    # Analyze results
    echo ""
    print_info "=== Analysis ==="
    
    if compare_images "$img1" "$img2"; then
        print_success "Method 1 (Alt+F): Menu opened! Images are different."
    else
        print_fail "Method 1 (Alt+F): No change detected"
    fi
    
    if compare_images "$img1" "$img3"; then
        print_success "Method 2 (F10): Menu opened! Images are different."
    else
        print_fail "Method 2 (F10): No change detected"
    fi
    
    if compare_images "$img1" "$img4"; then
        print_success "Method 3 (Click): Menu opened! Images are different."
    else
        print_fail "Method 3 (Click): No change detected"
    fi
    
    if compare_images "$img4" "$img5"; then
        print_success "Preferences dialog: Opened! Images are different."
    else
        print_fail "Preferences dialog: No change detected"
    fi
    
    echo ""
    print_info "All screenshots saved to: $OUTPUT_DIR"
    print_info "File sizes:"
    ls -lh "$OUTPUT_DIR"/*.png | awk '{printf "  %-30s %s\n", $9, $5}'
}

main() {
    echo ""
    print_info "=== Enhanced UI Menu Test ==="
    echo ""
    
    for dep in Xvfb scrot xdotool; do
        if ! command -v $dep &> /dev/null; then
            print_error "Missing: $dep"
            exit 1
        fi
    done
    
    local appimage=$(find_appimage)
    print_info "Testing: $(basename "$appimage")"
    
    test_menu_interaction "$appimage"
    
    echo ""
    print_info "View results: cd '$OUTPUT_DIR' && ls -lh"
}

main
