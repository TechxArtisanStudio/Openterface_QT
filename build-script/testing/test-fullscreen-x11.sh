#!/bin/bash
#
# Test script for toolbar auto-hide functionality in fullscreen mode (Alt+F11)
# This test verifies:
# 1. Fullscreen mode detection (Alt+F11)
# 2. Toolbar auto-hide after 5 seconds of inactivity in fullscreen
# 3. Toolbar show on mouse hover at top edge
# 4. Comprehensive logging of auto-hide events
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
TEST_DIR="/tmp/openterface_autohide_test"

# Try to find AppImage first (has bundled libraries), fallback to binary
if ls "$BUILD_DIR"/*.AppImage 1> /dev/null 2>&1; then
    APP_PATH=$(ls "$BUILD_DIR"/*.AppImage | head -1)
else
    APP_PATH="$BUILD_DIR/openterfaceQT"
fi

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  Toolbar Auto-Hide Test for Fullscreen Mode${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}[INFO]${NC} Test: Verify toolbar auto-hides after 10s in fullscreen mode (Alt+F11)"
echo -e "${GREEN}[INFO]${NC} Using Xvnc for complete X11 environment"
echo ""

# Check if Xvnc is available
if ! command -v Xvnc &> /dev/null; then
    echo -e "${YELLOW}[INFO]${NC} Xvnc not found, installing tigervnc-standalone-server..."
    sudo apt-get update -qq
    sudo apt-get install -y tigervnc-standalone-server
    
    if ! command -v Xvnc &> /dev/null; then
        echo -e "${RED}[ERROR]${NC} Failed to install Xvnc"
        exit 1
    fi
    echo -e "${GREEN}[INFO]${NC} Xvnc installed successfully"
fi

# Check for xdotool
if ! command -v xdotool &> /dev/null; then
    echo -e "${YELLOW}[INFO]${NC} xdotool not found, installing..."
    sudo apt-get update -qq
    sudo apt-get install -y xdotool
    
    if ! command -v xdotool &> /dev/null; then
        echo -e "${RED}[ERROR]${NC} Failed to install xdotool"
        exit 1
    fi
    echo -e "${GREEN}[INFO]${NC} xdotool installed successfully"
fi

# Check for wmctrl (optional, for better window management)
if ! command -v wmctrl &> /dev/null; then
    echo -e "${YELLOW}[INFO]${NC} wmctrl not found, installing (improves window maximize/restore)..."
    sudo apt-get update -qq
    sudo apt-get install -y wmctrl
    
    if command -v wmctrl &> /dev/null; then
        echo -e "${GREEN}[INFO]${NC} wmctrl installed successfully"
    else
        echo -e "${YELLOW}[WARNING]${NC} wmctrl installation failed, will use xdotool fallback"
    fi
fi

# Check for scrot (optional, for screenshots)
if ! command -v scrot &> /dev/null; then
    echo -e "${YELLOW}[INFO]${NC} scrot not found, installing (for screenshots)..."
    sudo apt-get update -qq
    sudo apt-get install -y scrot
    
    if command -v scrot &> /dev/null; then
        echo -e "${GREEN}[INFO]${NC} scrot installed successfully"
    else
        echo -e "${YELLOW}[WARNING]${NC} scrot installation failed, screenshots will be skipped"
    fi
fi

# Clean up
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"

# Check app exists
if [ ! -f "$APP_PATH" ]; then
    echo -e "${RED}[ERROR]${NC} App not found: $APP_PATH"
    exit 1
fi

# Start Xvnc server
echo -e "${GREEN}[INFO]${NC} Starting Xvnc server..."
# Xvnc provides a complete VNC/X11 server with proper input handling
Xvnc :99 -geometry 1920x1080 -depth 24 -rfbport 5999 -SecurityTypes None > "$TEST_DIR/xvnc.log" 2>&1 &
XVNC_PID=$!
export DISPLAY=:99

echo -e "${GREEN}[INFO]${NC} Xvnc PID: $XVNC_PID, DISPLAY=:99"
sleep 3

# Start a window manager (important for proper event routing)
if command -v openbox &> /dev/null; then
    openbox &
    WM_PID=$!
    echo -e "${GREEN}[INFO]${NC} Started openbox window manager (PID: $WM_PID)"
    sleep 1
elif command -v fluxbox &> /dev/null; then
    fluxbox &
    WM_PID=$!
    echo -e "${GREEN}[INFO]${NC} Started fluxbox window manager (PID: $WM_PID)"
    sleep 1
elif command -v icewm &> /dev/null; then
    icewm &
    WM_PID=$!
    echo -e "${GREEN}[INFO]${NC} Started icewm window manager (PID: $WM_PID)"
    sleep 1
else
    echo -e "${YELLOW}[WARNING]${NC} No window manager available"
    WM_PID=""
fi

# Start application with comprehensive logging
echo -e "${GREEN}[INFO]${NC} Starting application with auto-hide logging enabled..."
cd "$BUILD_DIR"
QT_LOGGING_RULES="opf.ui.windowcontrolmanager=true;opf.ui.mainwindow=true;opf.ui.*=true" \
    "$APP_PATH" > "$TEST_DIR/autohide_test.log" 2>&1 &
APP_PID=$!

echo -e "${GREEN}[INFO]${NC} Application PID: $APP_PID"
echo -e "${GREEN}[INFO]${NC} Waiting for application to initialize..."
sleep 5

# Function to enter fullscreen using Alt+F11
enter_fullscreen() {
    local window_id="$1"
    
    echo -e "${GREEN}[INFO]${NC} Entering fullscreen mode (Alt+F11) for window $window_id..."
    
    # Activate and focus the window first
    xdotool windowactivate --sync "$window_id" 2>/dev/null || xdotool windowactivate "$window_id" 2>/dev/null || true
    sleep 0.5
    
    xdotool windowfocus --sync "$window_id" 2>/dev/null || xdotool windowfocus "$window_id" 2>/dev/null || true
    sleep 0.5
    
    # Send Alt+F11 key combination to enter fullscreen
    echo -e "${GREEN}[INFO]${NC} Sending Alt+F11 key event..."
    xdotool key --clearmodifiers --window "$window_id" Alt+F11 2>/dev/null || \
        xdotool key --window "$window_id" Alt+F11 2>/dev/null || true
    
    sleep 2
}

# Function to exit fullscreen using Alt+F11
exit_fullscreen() {
    local window_id="$1"
    
    echo -e "${GREEN}[INFO]${NC} Exiting fullscreen mode (Alt+F11) for window $window_id..."
    
    # Send Alt+F11 again to exit fullscreen
    xdotool key --clearmodifiers --window "$window_id" Alt+F11 2>/dev/null || \
        xdotool key --window "$window_id" Alt+F11 2>/dev/null || true
    
    sleep 2
}

# Function to move mouse to top edge
move_mouse_to_top_edge() {
    echo -e "${GREEN}[INFO]${NC} Moving mouse to top edge of window..."
    # Move mouse to coordinates near top edge (x=960, y=5)
    xdotool mousemove 960 5
    sleep 0.5
}

# Function to move mouse away from top edge
move_mouse_away() {
    echo -e "${GREEN}[INFO]${NC} Moving mouse away from top edge..."
    # Move mouse to center of screen
    xdotool mousemove 960 540
    sleep 0.5
}

# Function to get window geometry using xdotool
get_window_geometry() {
    local window_id="$1"
    xdotool getwindowgeometry "$window_id" 2>/dev/null || echo "Geometry unavailable"
}

# Function to find the MAIN window ID (largest window) using xdotool
find_window_id() {
    # Find all Openterface windows and select the one with largest geometry
    local windows=$(xdotool search --name "Openterface" 2>/dev/null || echo "")
    
    if [ -z "$windows" ]; then
        windows=$(xdotool search --class "openterfaceQT" 2>/dev/null || echo "")
    fi
    
    if [ -z "$windows" ]; then
        echo ""
        return
    fi
    
    # Find the window with the largest area (width * height)
    local max_area=0
    local main_window=""
    
    for wid in $windows; do
        local geom=$(xdotool getwindowgeometry "$wid" 2>/dev/null | grep "Geometry:" | awk '{print $2}')
        if [ -n "$geom" ]; then
            local width=$(echo "$geom" | cut -d'x' -f1)
            local height=$(echo "$geom" | cut -d'x' -f2)
            local area=$((width * height))
            
            if [ $area -gt $max_area ]; then
                max_area=$area
                main_window=$wid
            fi
        fi
    done
    
    echo "$main_window"
}

# Find window - wait for it to be fully initialized
echo -e "${GREEN}[INFO]${NC} Looking for application window..."
sleep 3

for i in {1..10}; do
    WINDOW_ID=$(find_window_id)
    
    if [ -n "$WINDOW_ID" ]; then
        # Check if window has proper geometry (should be at least 800x600)
        GEOM=$(get_window_geometry "$WINDOW_ID" | grep "Geometry:" | awk '{print $2}' || echo "0x0")
        WIDTH=$(echo "$GEOM" | cut -d'x' -f1)
        HEIGHT=$(echo "$GEOM" | cut -d'x' -f2)
        
        if [ "$WIDTH" -gt 800 ] && [ "$HEIGHT" -gt 600 ]; then
            echo -e "${GREEN}[INFO]${NC} Window found and properly initialized: ${WIDTH}x${HEIGHT} (attempt $i)"
            break
        else
            echo -e "${YELLOW}[INFO]${NC} Window found but too small (${WIDTH}x${HEIGHT}), waiting... (attempt $i)"
            WINDOW_ID=""
            sleep 2
        fi
    else
        echo -e "${YELLOW}[INFO]${NC} Window not found yet, waiting... (attempt $i)"
        sleep 2
    fi
done

if [ -n "$WINDOW_ID" ]; then
    echo -e "${GREEN}[INFO]${NC} Found window ID: $WINDOW_ID"
else
    # Even if geometry check failed, try to find ANY window
    WINDOW_ID=$(find_window_id)
    
    if [ -n "$WINDOW_ID" ]; then
        echo -e "${YELLOW}[INFO]${NC} Found window ID (small geometry, testing anyway): $WINDOW_ID"
    fi
fi

if [ -n "$WINDOW_ID" ]; then
    echo -e "${GREEN}[SUCCESS]${NC} Window found: $WINDOW_ID"
    
    # Take initial screenshot
    scrot "$TEST_DIR/01_initial_window.png" 2>/dev/null || true
    
    # Get initial geometry
    GEOM_INITIAL=$(get_window_geometry "$WINDOW_ID")
    echo -e "${GREEN}[INFO]${NC} Initial geometry:\n$GEOM_INITIAL"
    
    # Test 1: Enter fullscreen mode with Alt+F11
    echo -e "\n${BLUE}=== TEST 1: Enter Fullscreen Mode (Alt+F11) ===${NC}"
    echo -e "${GREEN}[INFO]${NC} This should trigger auto-hide timer (5 seconds)"
    enter_fullscreen "$WINDOW_ID"
    sleep 2
    
    scrot "$TEST_DIR/02_after_fullscreen.png" 2>/dev/null || true
    GEOM_FULLSCREEN=$(get_window_geometry "$WINDOW_ID")
    echo -e "${GREEN}[INFO]${NC} Geometry after fullscreen:\n$GEOM_FULLSCREEN"
    
    # Move mouse away from top edge to ensure it doesn't interfere
    move_mouse_away
    
    # Test 2: Wait for auto-hide (5 seconds + 2 seconds buffer)
    echo -e "\n${BLUE}=== TEST 2: Wait for Auto-Hide (7 seconds) ===${NC}"
    echo -e "${GREEN}[INFO]${NC} Toolbar should auto-hide after 5 seconds of inactivity..."
    for i in {7..1}; do
        echo -e "${YELLOW}[COUNTDOWN]${NC} Waiting... $i seconds remaining"
        sleep 1
    done
    
    scrot "$TEST_DIR/03_after_autohide.png" 2>/dev/null || true
    echo -e "${GREEN}[INFO]${NC} Auto-hide period complete"
    
    # Test 3: Move mouse to top edge to trigger toolbar show
    echo -e "\n${BLUE}=== TEST 3: Mouse Hover at Top Edge ===${NC}"
    echo -e "${GREEN}[INFO]${NC} Moving mouse to top edge should show toolbar..."
    move_mouse_to_top_edge
    sleep 2
    
    scrot "$TEST_DIR/04_mouse_at_top.png" 2>/dev/null || true
    echo -e "${GREEN}[INFO]${NC} Mouse at top edge"
    
    # Test 4: Move mouse away and wait for auto-hide again
    echo -e "\n${BLUE}=== TEST 4: Second Auto-Hide Cycle ===${NC}"
    echo -e "${GREEN}[INFO]${NC} Moving mouse away and waiting for auto-hide again..."
    move_mouse_away
    
    echo -e "${GREEN}[INFO]${NC} Waiting 7 seconds for second auto-hide..."
    for i in {7..1}; do
        echo -e "${YELLOW}[COUNTDOWN]${NC} Waiting... $i seconds remaining"
        sleep 1
    done
    
    scrot "$TEST_DIR/05_second_autohide.png" 2>/dev/null || true
    echo -e "${GREEN}[INFO]${NC} Second auto-hide cycle complete"
    
    # Test 5: Exit fullscreen mode to restore window
    echo -e "\n${BLUE}=== TEST 5: Exit Fullscreen (Alt+F11) ===${NC}"
    echo -e "${GREEN}[INFO]${NC} Exiting fullscreen should show toolbar permanently..."
    exit_fullscreen "$WINDOW_ID"
    sleep 2
    
    scrot "$TEST_DIR/06_after_exit_fullscreen.png" 2>/dev/null || true
    GEOM_RESTORED=$(get_window_geometry "$WINDOW_ID")
    echo -e "${GREEN}[INFO]${NC} Geometry after exit fullscreen:\n$GEOM_RESTORED"
    
else
    echo -e "${RED}[ERROR]${NC} Could not find application window"
fi

# Wait for final events to be logged
sleep 2

# Cleanup
echo -e "\n${GREEN}[INFO]${NC} Cleaning up..."
kill $APP_PID 2>/dev/null || true
[ -n "$WM_PID" ] && kill $WM_PID 2>/dev/null || true
kill $XVNC_PID 2>/dev/null || true
sleep 1
# Force kill if still running
kill -9 $XVNC_PID 2>/dev/null || true

# Results
echo -e "\n${BLUE}========================================${NC}"
echo -e "${BLUE}  TEST RESULTS${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}[INFO]${NC} Log file: $TEST_DIR/autohide_test.log"
echo -e "${GREEN}[INFO]${NC} Screenshots: $TEST_DIR/*.png"
echo ""

# Check 1: Fullscreen mode detected
echo -e "${BLUE}[CHECK 1]${NC} Fullscreen Mode Detection"
if grep -q "WindowControlManager.*fullscreen\|Window entered fullscreen\|FULLSCREEN" "$TEST_DIR/autohide_test.log"; then
    echo -e "${GREEN}[✓ PASS]${NC} Fullscreen mode was detected"
    grep "WindowControlManager.*fullscreen\|Window entered fullscreen\|FULLSCREEN" "$TEST_DIR/autohide_test.log" | head -5
else
    echo -e "${RED}[✗ FAIL]${NC} Fullscreen mode was NOT detected"
fi
echo ""

# Check 2: Auto-hide timer started
echo -e "${BLUE}[CHECK 2]${NC} Auto-Hide Timer Initialization"
if grep -q "Starting auto-hide timer\|startAutoHideTimer" "$TEST_DIR/autohide_test.log"; then
    echo -e "${GREEN}[✓ PASS]${NC} Auto-hide timer was started"
    grep "Starting auto-hide timer\|startAutoHideTimer" "$TEST_DIR/autohide_test.log" | head -5
else
    echo -e "${RED}[✗ FAIL]${NC} Auto-hide timer was NOT started"
fi
echo ""

# Check 3: Auto-hide enabled status
echo -e "${BLUE}[CHECK 3]${NC} Auto-Hide Configuration"
if grep -q "AutoHide enabled.*true" "$TEST_DIR/autohide_test.log"; then
    echo -e "${GREEN}[✓ PASS]${NC} Auto-hide is enabled"
    grep "AutoHide enabled" "$TEST_DIR/autohide_test.log" | head -5
else
    echo -e "${YELLOW}[⚠ WARN]${NC} Could not verify auto-hide enabled status"
fi
echo ""

# Check 4: Toolbar hide event
echo -e "${BLUE}[CHECK 4]${NC} Toolbar Auto-Hide Event"
if grep -q "hideToolbar\|animateToolbarHide\|Toolbar hidden" "$TEST_DIR/autohide_test.log"; then
    echo -e "${GREEN}[✓ PASS]${NC} Toolbar hide event was triggered"
    grep "hideToolbar\|animateToolbarHide\|Toolbar hidden" "$TEST_DIR/autohide_test.log" | head -10
else
    echo -e "${RED}[✗ FAIL]${NC} Toolbar did NOT auto-hide"
fi
echo ""

# Check 5: Toolbar show on mouse hover
echo -e "${BLUE}[CHECK 5]${NC} Toolbar Show on Mouse Hover"
if grep -q "showToolbar\|animateToolbarShow\|Toolbar shown\|Edge hover detected" "$TEST_DIR/autohide_test.log"; then
    echo -e "${GREEN}[✓ PASS]${NC} Toolbar show event was triggered"
    grep "showToolbar\|animateToolbarShow\|Toolbar shown\|Edge hover" "$TEST_DIR/autohide_test.log" | head -10
else
    echo -e "${YELLOW}[⚠ WARN]${NC} Could not verify toolbar show on hover"
fi
echo ""

# Check 6: Exit fullscreen event
echo -e "${BLUE}[CHECK 6]${NC} Exit Fullscreen Detection"
if grep -q "Window restored to normal\|exit.*fullscreen\|WINDOW BEING RESTORED" "$TEST_DIR/autohide_test.log"; then
    echo -e "${GREEN}[✓ PASS]${NC} Exit fullscreen was detected"
    grep "Window restored to normal\|exit.*fullscreen\|WINDOW BEING RESTORED" "$TEST_DIR/autohide_test.log" | head -5
else
    echo -e "${YELLOW}[⚠ WARN]${NC} Could not verify exit fullscreen"
fi
echo ""

# Summary of all WindowControlManager events
echo -e "${BLUE}[SUMMARY]${NC} All WindowControlManager Events (chronological)"
echo -e "${YELLOW}==========================================${NC}"
grep "WindowControlManager\|windowcontrolmanager" "$TEST_DIR/autohide_test.log" | grep -v "^[[:space:]]*$" || echo "No WindowControlManager events found"
echo ""

echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}[INFO]${NC} Test complete!"
echo -e "${GREEN}[INFO]${NC} To review full logs:"
echo -e "  ${YELLOW}cat $TEST_DIR/autohide_test.log${NC}"
echo -e "${GREEN}[INFO]${NC} To view specific events:"
echo -e "  ${YELLOW}grep -i 'windowcontrolmanager' $TEST_DIR/autohide_test.log${NC}"
echo -e "  ${YELLOW}grep -i 'autohide\|toolbar.*hide\|toolbar.*show' $TEST_DIR/autohide_test.log${NC}"
echo -e "${BLUE}========================================${NC}"
