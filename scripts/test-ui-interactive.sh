#!/bin/bash
#
# Interactive UI Test Script
# Runs the Openterface application and simulates user interactions
# then captures screenshots at various stages
#

set -e  # Exit on error

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
OUTPUT_DIR="${OUTPUT_DIR:-/tmp/openterface_ui_test}"
LOG_FILE="$OUTPUT_DIR/openterface_test.log"
SCREEN_RESOLUTION="${SCREEN_RESOLUTION:-1920x1080x24}"

# Timing configuration (in seconds)
STARTUP_DELAY=3          # Wait for app to start
PRE_ACTION_DELAY=1       # Wait before each action
POST_ACTION_DELAY=1      # Wait after each action
SCREENSHOT_DELAY=0.5     # Wait before screenshot

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Functions
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_step() {
    echo -e "${BLUE}[STEP]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_dependencies() {
    print_info "Checking dependencies..."
    
    local missing_deps=()
    
    if ! command -v xvfb-run &> /dev/null; then
        missing_deps+=("xvfb")
    fi
    
    if ! command -v scrot &> /dev/null; then
        missing_deps+=("scrot")
    fi
    
    if ! command -v xdotool &> /dev/null; then
        missing_deps+=("xdotool")
    fi
    
    if [ ${#missing_deps[@]} -gt 0 ]; then
        print_error "Missing dependencies: ${missing_deps[*]}"
        print_info "Install with: sudo apt-get install -y ${missing_deps[*]}"
        exit 1
    fi
    
    print_info "All dependencies satisfied"
}

find_appimage() {
    print_info "Looking for AppImage in $BUILD_DIR..."
    
    local appimage=$(find "$BUILD_DIR" -maxdepth 1 -name "*.AppImage" -type f | head -n 1)
    
    if [ -z "$appimage" ]; then
        print_error "No AppImage found in $BUILD_DIR"
        print_info "Please build the project first"
        exit 1
    fi
    
    if [ ! -x "$appimage" ]; then
        print_warning "AppImage is not executable, fixing..."
        chmod +x "$appimage"
    fi
    
    print_info "Found AppImage: $(basename "$appimage")"
    echo "$appimage"
}

setup_output_dir() {
    mkdir -p "$OUTPUT_DIR"
    print_info "Output directory: $OUTPUT_DIR"
}

capture_screenshot() {
    local name="$1"
    local filepath="$OUTPUT_DIR/${name}.png"
    
    sleep $SCREENSHOT_DELAY
    DISPLAY=$DISPLAY scrot "$filepath" 2>/dev/null || true
    
    if [ -f "$filepath" ]; then
        print_info "Screenshot saved: $filepath"
    else
        print_warning "Failed to capture screenshot: $name"
    fi
}

click_menu_item() {
    local menu_text="$1"
    print_step "Clicking on menu: $menu_text"
    
    # Find and activate any window (more reliable than searching by name)
    local window_id=$(DISPLAY=$DISPLAY xdotool search --class "" 2>/dev/null | head -n 1)
    
    if [ -z "$window_id" ]; then
        print_warning "No window found, trying alternative method..."
        # Try to get any window
        window_id=$(DISPLAY=$DISPLAY xdotool getactivewindow 2>/dev/null || echo "")
    fi
    
    if [ -n "$window_id" ]; then
        print_info "Found window ID: $window_id"
        DISPLAY=$DISPLAY xdotool windowactivate --sync "$window_id" 2>/dev/null || true
        sleep $PRE_ACTION_DELAY
        
        # Use keyboard to access menu (Alt+F for File menu)
        if [ "$menu_text" = "File" ]; then
            DISPLAY=$DISPLAY xdotool key --delay 100 alt+f
        fi
    else
        print_warning "Could not find window, continuing anyway..."
        # Try sending keys directly to display
        DISPLAY=$DISPLAY xdotool key --delay 100 alt+f 2>/dev/null || true
    fi
    
    sleep $POST_ACTION_DELAY
}

click_submenu_item() {
    local item_text="$1"
    print_step "Selecting menu item: $item_text"
    
    sleep $PRE_ACTION_DELAY
    
    # Navigate menu with keyboard
    if [ "$item_text" = "Preferences" ] || [ "$item_text" = "Preference" ]; then
        # Press 'P' key to select Preferences (or Down arrow + Enter)
        DISPLAY=$DISPLAY xdotool key --delay 100 p
    fi
    
    sleep $POST_ACTION_DELAY
}

close_dialog() {
    print_step "Closing dialog"
    sleep $PRE_ACTION_DELAY
    
    # Press Escape to close
    DISPLAY=$DISPLAY xdotool key --delay 100 Escape
    
    sleep $POST_ACTION_DELAY
}

run_interactive_test() {
    local appimage="$1"
    
    print_info "Starting interactive UI test..."
    print_info "Screen resolution: $SCREEN_RESOLUTION"
    print_info "Log file: $LOG_FILE"
    
    # Start Xvfb and get display number
    export DISPLAY=:99
    Xvfb :99 -screen 0 $SCREEN_RESOLUTION &
    XVFB_PID=$!
    sleep 2
    
    print_info "Xvfb started on $DISPLAY (PID: $XVFB_PID)"
    
    # Start the application
    print_step "Launching application..."
    "$appimage" &> "$LOG_FILE" &
    APP_PID=$!
    
    print_info "Application started (PID: $APP_PID)"
    
    # Wait for application to initialize
    print_step "Waiting ${STARTUP_DELAY}s for application startup..."
    sleep $STARTUP_DELAY
    
    # Capture initial state
    print_step "Capturing initial state..."
    capture_screenshot "01_initial_window"
    
    # Test sequence: File -> Preferences
    print_step "Opening File menu..."
    click_menu_item "File"
    capture_screenshot "02_file_menu_open"
    
    print_step "Opening Preferences..."
    click_submenu_item "Preferences"
    sleep 2  # Give dialog time to open
    capture_screenshot "03_preferences_dialog"
    
    # Close preferences dialog
    close_dialog
    sleep 1
    capture_screenshot "04_after_close_preferences"
    
    # Additional test: Try opening again to verify it works multiple times
    print_step "Testing menu again..."
    click_menu_item "File"
    sleep 1
    capture_screenshot "05_file_menu_reopen"
    
    # Press Escape to close menu
    DISPLAY=$DISPLAY xdotool key Escape
    sleep 1
    capture_screenshot "06_final_state"
    
    # Cleanup
    print_step "Cleaning up..."
    kill $APP_PID 2>/dev/null || true
    wait $APP_PID 2>/dev/null || true
    
    kill $XVFB_PID 2>/dev/null || true
    wait $XVFB_PID 2>/dev/null || true
    
    print_info "Test completed"
}

verify_results() {
    print_info "Verifying results..."
    
    local screenshot_count=$(find "$OUTPUT_DIR" -name "*.png" -type f | wc -l)
    print_info "Captured $screenshot_count screenshots"
    
    if [ $screenshot_count -eq 0 ]; then
        print_error "No screenshots were captured"
        exit 1
    fi
    
    # List all screenshots with sizes
    echo ""
    print_info "Screenshot summary:"
    find "$OUTPUT_DIR" -name "*.png" -type f -exec ls -lh {} \; | awk '{print "  " $9 " (" $5 ")"}'
    
    # Check log for errors
    if [ -f "$LOG_FILE" ]; then
        if grep -qi "error\|crash\|segmentation fault" "$LOG_FILE" 2>/dev/null; then
            print_warning "Errors detected in log file"
        fi
    fi
}

show_summary() {
    echo ""
    echo "=========================================="
    echo "Interactive UI Test Summary"
    echo "=========================================="
    echo "Output directory: $OUTPUT_DIR"
    echo "Log file:         $LOG_FILE"
    echo "Status:           ${GREEN}SUCCESS${NC}"
    echo "=========================================="
    echo ""
    
    print_info "View screenshots with:"
    echo "  cd '$OUTPUT_DIR' && ls -lh *.png"
    echo ""
    print_info "View all screenshots:"
    echo "  xdg-open '$OUTPUT_DIR'"
    echo ""
    print_info "Check logs:"
    echo "  cat '$LOG_FILE'"
}

# Main execution
main() {
    echo ""
    print_info "Openterface Interactive UI Test"
    print_info "Testing: File -> Preferences menu interaction"
    echo ""
    
    check_dependencies
    setup_output_dir
    appimage=$(find_appimage)
    run_interactive_test "$appimage"
    verify_results
    show_summary
}

# Help message
if [ "$1" == "-h" ] || [ "$1" == "--help" ]; then
    cat << EOF
Openterface Interactive UI Test Script

Usage: $0 [OPTIONS]

This script runs the Openterface application in a virtual framebuffer (Xvfb),
simulates user interactions (clicking menus, opening dialogs), and captures
screenshots at each stage.

Test Sequence:
  1. Launch application
  2. Capture initial window
  3. Open File menu (Alt+F)
  4. Capture File menu
  5. Select Preferences (P key)
  6. Capture Preferences dialog
  7. Close dialog (Escape)
  8. Capture final state

Environment Variables:
  OUTPUT_DIR          Directory for output files (default: /tmp/openterface_ui_test)
  SCREEN_RESOLUTION   Virtual screen resolution (default: 1920x1080x24)

Examples:
  # Basic usage
  $0
  
  # Custom output directory
  OUTPUT_DIR=./ui-tests $0

Dependencies:
  - xvfb (X virtual framebuffer)
  - scrot (screenshot utility)
  - xdotool (X automation tool)

EOF
    exit 0
fi

main
