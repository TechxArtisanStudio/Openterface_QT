#!/bin/bash

# Simple Docker App Screenshot Script
# Starts Docker container, waits 10 seconds, takes screenshot

set -e

# Configuration - can be overridden by environment variables
DOCKER_IMAGE="${DOCKER_IMAGE:-openterface-test-shared}"
DOCKER_TAG="${DOCKER_TAG:-screenshot-test}"
DOCKERFILE_PATH="docker/testos/Dockerfile.ubuntu-test-shared"
SCREENSHOTS_DIR="${SCREENSHOTS_DIR:-app-screenshots}"
CONTAINER_NAME="openterface-screenshot-test"
GITHUB_TOKEN="${GITHUB_TOKEN:-}"
VOLUME_MOUNT="${VOLUME_MOUNT:-}"
INSTALL_TYPE="${INSTALL_TYPE:-deb}"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${BLUE}🚀 Starting Openterface Docker App Screenshot Test${NC}"
echo "================================================"

# Cleanup function
cleanup() {
    echo -e "${YELLOW}🧹 Cleaning up resources...${NC}"
    docker stop $CONTAINER_NAME 2>/dev/null || true
    docker rm $CONTAINER_NAME 2>/dev/null || true
    if [ ! -z "$XVFB_PID" ]; then
        kill $XVFB_PID 2>/dev/null || true
    fi
}
trap cleanup EXIT

echo -e "${BLUE}📦 Using existing Docker image${NC}"

# Install virtual display and ImageMagick dependencies if needed
if ! command -v Xvfb >/dev/null 2>&1 || ! command -v import >/dev/null 2>&1; then
    echo -e "${BLUE}📦 Installing virtual display and image processing dependencies...${NC}"
    sudo apt-get update -y >/dev/null
    sudo apt-get install -y xvfb imagemagick x11-utils >/dev/null
    echo -e "${GREEN}✅ Dependencies installation completed${NC}"
fi

# Setup virtual display
echo -e "${BLUE}🖥️  Setting up virtual display...${NC}"
export DISPLAY=:98
Xvfb :98 -screen 0 1920x1080x24 -ac +extension GLX +render -noreset >/dev/null 2>&1 &
XVFB_PID=$!
sleep 3

# Verify X server
if ! DISPLAY=:98 xdpyinfo >/dev/null 2>&1; then
    echo -e "${RED}❌ Virtual display startup failed${NC}"
    exit 1
fi
echo -e "${GREEN}✅ Virtual display started successfully ($DISPLAY)${NC}"

# Create screenshots directory
mkdir -p $SCREENSHOTS_DIR

# Start Docker container with the app
echo -e "${BLUE}🐳 Starting Docker container and app...${NC}"

# Build the docker run command with optional volume mount and environment variables
DOCKER_RUN_CMD="docker run -d \
    --name $CONTAINER_NAME \
    -e DISPLAY=$DISPLAY \
    -e GITHUB_TOKEN=$GITHUB_TOKEN \
    -e INSTALL_TYPE=$INSTALL_TYPE \
    -e QT_X11_NO_MITSHM=1 \
    -e QT_QPA_PLATFORM=xcb \
    -e QT_PLUGIN_PATH=/usr/lib/qt6/plugins \
    -e QML2_IMPORT_PATH=/usr/lib/qt6/qml \
    -e LC_ALL=C.UTF-8 \
    -e LANG=C.UTF-8 \
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
    --network host \
    --privileged \
    --device /dev/fuse"

# Add volume mount if provided
if [ -n "$VOLUME_MOUNT" ]; then
    DOCKER_RUN_CMD="$DOCKER_RUN_CMD $VOLUME_MOUNT"
fi

# Add the image and override entrypoint behavior
# We want to run installation but NOT start the app automatically
# Override the CMD to just sleep, and we'll start the app manually
DOCKER_RUN_CMD="$DOCKER_RUN_CMD $DOCKER_IMAGE:$DOCKER_TAG tail -f /dev/null"

# Execute the docker run command
CONTAINER_ID=$(eval $DOCKER_RUN_CMD)

echo -e "${GREEN}✅ Container started${NC}"
echo -e "${BLUE}📦 Container ID: ${CONTAINER_ID:0:12}${NC}"

# Wait a moment for entrypoint to complete installation
echo -e "${BLUE}⏳ Waiting for entrypoint to complete installation and start app...${NC}"
sleep 12

# Check if container is still running
if ! docker ps | grep -q $CONTAINER_ID; then
    echo -e "${RED}⚠️  Container exited unexpectedly!${NC}"
    echo -e "${BLUE}📋 Container logs:${NC}"
    docker logs $CONTAINER_ID 2>&1 | sed 's/^/   /'
    exit 1
fi

echo -e "${GREEN}✅ Container is running${NC}"

# Check if app is running
echo -e "${BLUE}🔍 Checking if openterfaceQT is running...${NC}"
if docker exec $CONTAINER_NAME pgrep -x openterfaceQT >/dev/null 2>&1; then
    echo -e "${GREEN}✅ openterfaceQT process is running${NC}"
    docker exec $CONTAINER_NAME ps aux | grep openterfaceQT | grep -v grep | sed 's/^/   /'
else
    echo -e "${YELLOW}⚠️  openterfaceQT process not detected, checking logs...${NC}"
    docker exec $CONTAINER_NAME tail -20 /tmp/openterfaceqt.log 2>&1 | sed 's/^/   /'
fi

# Give the app extra time to fully initialize UI
echo -e "${BLUE}⏳ Waiting for app UI to initialize...${NC}"
sleep 5

echo -e "${BLUE}📱 App is initializing...${NC}"

# Wait for app to start with 2-minute timeout
echo -e "${YELLOW}⏱️  Waiting for app to start (timeout: 2 minutes)...${NC}"
MAX_WAIT=120
ELAPSED=0
APP_STARTED=false

while [ $ELAPSED -lt $MAX_WAIT ]; do
    # Display countdown first
    REMAINING=$((MAX_WAIT - ELAPSED))
    printf "\r${YELLOW}Waiting... %d seconds elapsed, %d seconds remaining${NC}" $ELAPSED $REMAINING
    
    # Check if container is still running
    if ! docker ps | grep -q $CONTAINER_ID; then
        echo ""
        echo -e "${RED}❌ Container has exited after $ELAPSED seconds${NC}"
        break
    fi
    
    # Check if the app process is running (match only the actual binary, not bash scripts)
    if docker exec $CONTAINER_NAME pgrep -x "openterfaceQT" >/dev/null 2>&1; then
        echo ""
        echo -e "${GREEN}✅ App process detected after $ELAPSED seconds!${NC}"
        APP_STARTED=true
        
        # Show process details (filter to only the actual app process, not entrypoint or bash)
        echo -e "${BLUE}📊 Process Details:${NC}"
        docker exec $CONTAINER_NAME ps aux | grep "openterfaceQT" | grep -v grep | grep -v bash | sed 's/^/   /'
        
        # Give it a few more seconds to fully initialize
        sleep 3
        break
    fi
    
    sleep 1
    ELAPSED=$((ELAPSED + 1))
done

echo ""

if [ "$APP_STARTED" = false ]; then
    echo -e "${YELLOW}⚠️  App process not detected within timeout, proceeding with screenshot anyway...${NC}"
echo -e "${GREEN}✅ App startup complete!${NC}"
fi

# Give the app extra time to fully initialize GUI
echo -e "${BLUE}⏳ Waiting for GUI to initialize...${NC}"
sleep 8

# Take the main screenshot
echo -e "${BLUE}📸 Taking screenshot...${NC}"
timestamp=$(date +"%Y%m%d_%H%M%S")
screenshot_jpg="$SCREENSHOTS_DIR/openterface_app_${timestamp}.jpg"

# Use ImageMagick import for reliable JPG screenshot capture
echo -e "${BLUE}📷 Taking screenshot with ImageMagick (PNG/JPG)...${NC}"
screenshot_success=false

# Debug: Check X11 display and window information before taking screenshot
echo -e "${BLUE}🔍 Pre-screenshot diagnostics:${NC}"
echo "   Display: $DISPLAY"
echo "   Xvfb PID: $XVFB_PID"

# Check if X server is responding
if ! DISPLAY=:98 xdpyinfo >/dev/null 2>&1; then
    echo -e "${RED}   ❌ X server not responding${NC}"
else
    echo -e "${GREEN}   ✅ X server responding${NC}"
fi

# Check for active windows
window_count=$(DISPLAY=:98 xwininfo -tree -root 2>/dev/null | grep -c "child" || echo "0")
echo "   Active windows: $window_count"

# List all windows
if [ "$window_count" -gt 0 ]; then
    echo -e "${BLUE}   Window list:${NC}"
    DISPLAY=:98 xwininfo -tree -root 2>/dev/null | grep "child" | head -5 | sed 's/^/     /'
fi

# Check if app window is visible
if docker exec $CONTAINER_NAME pgrep -x "openterfaceQT" >/dev/null 2>&1; then
    echo -e "${BLUE}   App process status: Running${NC}"
    
    # Check app logs for any GUI-related messages
    echo -e "${BLUE}   Recent app logs:${NC}"
    docker exec $CONTAINER_NAME tail -10 /tmp/openterfaceqt.log 2>&1 | sed 's/^/     /' || echo "     No logs available"
    
    # Check for Qt platform plugin messages
    echo -e "${BLUE}   Checking for Qt platform messages:${NC}"
    docker exec $CONTAINER_NAME grep -i "platform\|xcb\|plugin" /tmp/openterfaceqt.log 2>&1 | tail -3 | sed 's/^/     /' || echo "     No platform messages found"
else
    echo -e "${RED}   App process status: Not running${NC}"
fi

# ImageMagick import is the most reliable method for this virtual display setup
if command -v import >/dev/null 2>&1; then
    echo -e "${BLUE}   Attempting screenshot with ImageMagick...${NC}"
    # Show the command being run and capture both stdout and stderr
    screenshot_output=$(DISPLAY=:98 import -window root -quality 90 "$screenshot_jpg" 2>&1)
    screenshot_exit_code=$?
    
    if [ $screenshot_exit_code -eq 0 ] && [ -f "$screenshot_jpg" ]; then
        echo -e "${GREEN}✅ JPG screenshot saved: $screenshot_jpg${NC}"
        screenshot_success=true
    else
        echo -e "${RED}❌ ImageMagick screenshot failed (exit code: $screenshot_exit_code)${NC}"
        echo -e "${YELLOW}   Error output: $screenshot_output${NC}"
        
        # Try alternative screenshot method
        echo -e "${BLUE}   Trying alternative screenshot method...${NC}"
        if command -v xwd >/dev/null 2>&1 && command -v convert >/dev/null 2>&1; then
            xwd_file="/tmp/screenshot.xwd"
            DISPLAY=:98 xwd -root -out "$xwd_file" 2>/dev/null && \
            convert "$xwd_file" -quality 90 "$screenshot_jpg" 2>/dev/null && \
            rm -f "$xwd_file"
            
            if [ -f "$screenshot_jpg" ]; then
                echo -e "${GREEN}✅ Alternative screenshot method succeeded${NC}"
                screenshot_success=true
            else
                echo -e "${RED}❌ Alternative screenshot method also failed${NC}"
            fi
        fi
    fi
else
    echo -e "${RED}❌ ImageMagick import command not available${NC}"
    echo -e "${YELLOW}💡 Please install ImageMagick: sudo apt-get install imagemagick${NC}"
fi

if [ "$screenshot_success" = true ]; then
    echo -e "${GREEN}✅ Screenshot generated successfully${NC}"
    
    # Analyze screenshot
    if [ -f "$screenshot_jpg" ] && command -v identify >/dev/null 2>&1; then
        filesize=$(ls -lh "$screenshot_jpg" | awk '{print $5}')
        dimensions=$(identify "$screenshot_jpg" | awk '{print $3}')
        # Calculate mean color using convert and statistics
        mean_value=$(convert "$screenshot_jpg" -format "%[fx:mean*65535]" info: 2>/dev/null | awk '{print int($1)}' || echo "0")
        
        echo -e "${BLUE}📊 JPG screenshot analysis:${NC}"
        echo "   File size: $filesize"
        echo "   Image dimensions: $dimensions"
        echo "   Average color value: $mean_value"
        
        if [ "$mean_value" -gt 1000 ]; then
            echo -e "${GREEN}   Status: ✅ Rich app content detected${NC}"
        elif [ "$mean_value" -gt 100 ]; then
            echo -e "${YELLOW}   Status: ⚠️  Basic content detected${NC}"
        else
            echo -e "${RED}   Status: ❌ Screenshot may be blank${NC}"
        fi
    fi
else
    echo -e "${RED}❌ All screenshot methods failed${NC}"
fi

# Analyze all screenshots
if [ -d "$SCREENSHOTS_DIR" ] && [ "$(ls -A $SCREENSHOTS_DIR/*.jpg 2>/dev/null)" ]; then
    echo -e "${BLUE}📊 All screenshots analysis:${NC}"
    for img in $SCREENSHOTS_DIR/*.jpg; do
        if [ -f "$img" ]; then
            filename=$(basename "$img")
            if command -v identify >/dev/null 2>&1; then
                filesize=$(ls -lh "$img" | awk '{print $5}')
                dimensions=$(identify "$img" | awk '{print $3}' | head -1)
                # Calculate mean color using convert and statistics
                mean_value=$(convert "$img" -format "%[fx:mean*65535]" info: 2>/dev/null | awk '{print int($1)}' || echo "0")
                
                echo -e "${BLUE}   📸 $filename:${NC}"
                echo "      Size: $filesize | Dimensions: $dimensions | Average: $mean_value"
                
                if [ "$mean_value" -gt 1000 ]; then
                    echo -e "${GREEN}      Status: ✅ Rich content detected${NC}"
                elif [ "$mean_value" -gt 100 ]; then
                    echo -e "${YELLOW}      Status: ⚠️  Basic content detected${NC}"
                else
                    echo -e "${RED}      Status: ❌ Screenshot may be blank${NC}"
                fi
            fi
        fi
    done
fi

# Show container status and logs
echo -e "${BLUE}🔍 Container status check:${NC}"
if docker ps | grep -q $CONTAINER_ID; then
    echo -e "${GREEN}✅ Container is running${NC}"
    
    # Show recent logs
    echo -e "${BLUE}📋 Container logs (last 10 lines):${NC}"
    docker logs --tail 10 $CONTAINER_ID 2>&1 | sed 's/^/   /'
    
    # Check processes
    echo -e "${BLUE}🔍 App process check:${NC}"
    process_count=$(docker exec $CONTAINER_NAME ps aux | grep -E "openterface|Qt|qt" | grep -v grep | wc -l)
    if [ $process_count -gt 0 ]; then
        echo -e "${GREEN}   ✅ Found $process_count related processes${NC}"
        docker exec $CONTAINER_NAME ps aux | grep -E "openterface|Qt|qt" | grep -v grep | sed 's/^/   /'
    else
        echo -e "${YELLOW}   ⚠️  No obvious app processes found${NC}"
    fi
else
    echo -e "${RED}❌ Container has exited${NC}"
    echo -e "${BLUE}📋 Container exit logs:${NC}"
    docker logs $CONTAINER_ID 2>&1 | sed 's/^/   /'
fi

# Show window information
echo -e "${BLUE}🪟 X11 window information:${NC}"
window_info=$(DISPLAY=:98 xwininfo -tree -root 2>/dev/null | head -10 || echo "Unable to get window information")
if echo "$window_info" | grep -q "child"; then
    echo -e "${GREEN}   ✅ Active windows detected${NC}"
    echo "$window_info" | grep "child" | head -3 | sed 's/^/   /'
else
    echo -e "${YELLOW}   ⚠️  No active windows detected${NC}"
fi

# Summary
echo ""
echo -e "${BLUE}📋 Test Summary:${NC}"
echo "================================================"
echo "Container Image: $DOCKER_IMAGE:$DOCKER_TAG"
echo "Container Name: $CONTAINER_NAME"
echo "Display Environment: $DISPLAY"
echo "Screenshot Directory: $SCREENSHOTS_DIR/"
echo ""

# List all screenshots
if [ -d "$SCREENSHOTS_DIR" ] && [ "$(ls -A $SCREENSHOTS_DIR 2>/dev/null)" ]; then
    echo -e "${GREEN}📸 Generated screenshot files:${NC}"
    ls -lh $SCREENSHOTS_DIR/ | grep -v "^total" | sed 's/^/   /'
    echo ""
    
    # Show specific viewing commands for main screenshots
    main_jpg=$(ls $SCREENSHOTS_DIR/openterface_app_*[0-9].jpg 2>/dev/null | head -1)
    
    if [ -n "$main_jpg" ]; then
        echo -e "${BLUE}💡 View screenshot commands:${NC}"
        echo "   display $main_jpg"
        echo "   eog $main_jpg"
        echo "   firefox $main_jpg"
    fi
    
    # Count JPG files
    jpg_count=$(ls $SCREENSHOTS_DIR/*.jpg 2>/dev/null | wc -l)
    
    echo ""
    echo -e "${GREEN}📈 Screenshot Statistics:${NC}"
    echo "   JPG files: $jpg_count files"
else
    echo -e "${RED}❌ No screenshot files generated${NC}"
fi

echo ""
echo -e "${GREEN}🎉 Test completed!${NC}"
