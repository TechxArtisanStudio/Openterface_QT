#!/bin/bash
#
# UI Test with Window Manager
# Uses a minimal window manager to properly handle events
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
OUTPUT_DIR="${OUTPUT_DIR:-/tmp/openterface_ui_wm_test}"
DISPLAY_NUM=":99"

GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

print_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
print_step() { echo -e "${BLUE}[STEP]${NC} $1"; }
print_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

check_and_install_wm() {
    print_info "Checking for window manager..."
    
    if command -v openbox &> /dev/null; then
        print_info "Found: openbox"
        echo "openbox"
        return 0
    fi
    
    if command -v fluxbox &> /dev/null; then
        print_info "Found: fluxbox"
        echo "fluxbox"
        return 0
    fi
    
    if command -v icewm &> /dev/null; then
        print_info "Found: icewm"
        echo "icewm"
        return 0
    fi
    
    if command -v twm &> /dev/null; then
        print_info "Found: twm"
        echo "twm"
        return 0
    fi
    
    print_warn "No window manager found. Installing openbox..."
    if sudo apt-get install -y openbox 2>&1 | tail -5; then
        echo "openbox"
        return 0
    else
        print_error "Failed to install window manager"
        return 1
    fi
}

find_appimage() {
    local appimage=$(find "$BUILD_DIR" -maxdepth 1 -name "*.AppImage" -type f | head -n 1)
    [ -z "$appimage" ] && { print_error "No AppImage found"; exit 1; }
    chmod +x "$appimage" 2>/dev/null || true
    echo "$appimage"
}

run_test_with_wm() {
    local appimage="$1"
    local wm="$2"
    
    mkdir -p "$OUTPUT_DIR"
    
    print_info "Starting Xvfb on display $DISPLAY_NUM..."
    Xvfb $DISPLAY_NUM -screen 0 1920x1080x24 &
    local xvfb_pid=$!
    sleep 2
    export DISPLAY=$DISPLAY_NUM
    
    print_info "Starting window manager: $wm..."
    DISPLAY=$DISPLAY_NUM $wm >/dev/null 2>&1 &
    local wm_pid=$!
    sleep 2
    
    print_info "Launching application..."
    "$appimage" &> "$OUTPUT_DIR/app.log" &
    local app_pid=$!
    sleep 5
    
    print_step "Test sequence starting..."
    
    # 1. Baseline
    print_step "1️⃣  Baseline screenshot"
    DISPLAY=$DISPLAY_NUM scrot "$OUTPUT_DIR/01_baseline.png" 2>/dev/null
    sleep 1
    
    # 2. Try to focus window and open menu
    print_step "2️⃣  Focusing window and opening File menu"
    # Get window ID
    local win_id=$(DISPLAY=$DISPLAY_NUM xdotool search --class "" 2>/dev/null | head -n 1)
    if [ -n "$win_id" ]; then
        print_info "   Found window ID: $win_id"
        DISPLAY=$DISPLAY_NUM xdotool windowfocus --sync "$win_id" 2>/dev/null
        DISPLAY=$DISPLAY_NUM xdotool windowactivate --sync "$win_id" 2>/dev/null
        sleep 1
        
        # Try Alt+F
        print_info "   Sending Alt+F..."
        DISPLAY=$DISPLAY_NUM xdotool key --window "$win_id" alt+f 2>/dev/null
        sleep 1.5
        DISPLAY=$DISPLAY_NUM scrot "$OUTPUT_DIR/02_altf_with_wm.png" 2>/dev/null
        
        # Try clicking File menu
        print_info "   Clicking File menu..."
        DISPLAY=$DISPLAY_NUM xdotool mousemove --window "$win_id" 40 10 2>/dev/null
        sleep 0.5
        DISPLAY=$DISPLAY_NUM xdotool click --window "$win_id" 1 2>/dev/null
        sleep 1.5
        DISPLAY=$DISPLAY_NUM scrot "$OUTPUT_DIR/03_click_file.png" 2>/dev/null
        
        # Try to click Preferences (assuming menu is open)
        print_info "   Clicking Preferences..."
        DISPLAY=$DISPLAY_NUM xdotool mousemove --window "$win_id" 40 50 2>/dev/null
        sleep 0.5
        DISPLAY=$DISPLAY_NUM xdotool click 1 2>/dev/null
        sleep 2
        DISPLAY=$DISPLAY_NUM scrot "$OUTPUT_DIR/04_preferences.png" 2>/dev/null
        
        # Close
        print_info "   Closing dialogs..."
        DISPLAY=$DISPLAY_NUM xdotool key Escape Escape 2>/dev/null
        sleep 1
        DISPLAY=$DISPLAY_NUM scrot "$OUTPUT_DIR/05_final.png" 2>/dev/null
    else
        print_warn "Could not find application window"
    fi
    
    # Cleanup
    print_step "Cleaning up..."
    kill $app_pid 2>/dev/null || true
    wait $app_pid 2>/dev/null || true
    kill $wm_pid 2>/dev/null || true
    kill $xvfb_pid 2>/dev/null || true
    
    # Show results
    echo ""
    print_info "=== Results ==="
    if [ -d "$OUTPUT_DIR" ]; then
        ls -lh "$OUTPUT_DIR"/*.png 2>/dev/null | awk '{print "  " $9 " (" $5 ")"}'
        
        # Check for size differences
        echo ""
        print_info "Comparing sizes:"
        local baseline_size=$(stat -c%s "$OUTPUT_DIR/01_baseline.png" 2>/dev/null || echo 0)
        for img in "$OUTPUT_DIR"/*.png; do
            local size=$(stat -c%s "$img")
            local diff=$((size - baseline_size))
            if [ $diff -lt 0 ]; then diff=$((-diff)); fi
            
            local name=$(basename "$img")
            if [ $diff -gt 1000 ]; then
                echo -e "  ${GREEN}✓${NC} $name: Different from baseline (+/- ${diff} bytes)"
            else
                echo -e "  ${RED}✗${NC} $name: Same as baseline (~${diff} bytes diff)"
            fi
        done
    fi
    
    echo ""
    print_info "Screenshots: $OUTPUT_DIR"
    print_info "Log: $OUTPUT_DIR/app.log"
}

main() {
    echo ""
    print_info "=== UI Test with Window Manager ==="
    echo ""
    
    # Check dependencies
    for dep in Xvfb scrot xdotool; do
        if ! command -v $dep &> /dev/null; then
            print_error "Missing: $dep"
            print_info "Install with: sudo apt-get install -y xvfb scrot xdotool"
            exit 1
        fi
    done
    
    local wm=$(check_and_install_wm) || exit 1
    local appimage=$(find_appimage)
    
    print_info "Using window manager: $wm"
    print_info "Testing application: $(basename "$appimage")"
    echo ""
    
    run_test_with_wm "$appimage" "$wm"
}

main
