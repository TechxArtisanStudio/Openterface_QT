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
    --privileged"

# Add volume mount if provided
if [ -n "$VOLUME_MOUNT" ]; then
    DOCKER_RUN_CMD="$DOCKER_RUN_CMD $VOLUME_MOUNT"
fi

# Add the image and command to run openterfaceQT directly
# Use --entrypoint to override the default entrypoint behavior
DOCKER_RUN_CMD="$DOCKER_RUN_CMD --entrypoint /bin/bash $DOCKER_IMAGE:$DOCKER_TAG -c \"
export DISPLAY=$DISPLAY
export QT_X11_NO_MITSHM=1
export QT_QPA_PLATFORM=xcb
export QT_PLUGIN_PATH=/usr/lib/qt6/plugins
export QML2_IMPORT_PATH=/usr/lib/qt6/qml
export LC_ALL=C.UTF-8
export LANG=C.UTF-8

# Check if app exists
if [ -f /usr/local/bin/openterfaceQT ]; then
    echo 'Starting openterfaceQT...'
    /usr/local/bin/openterfaceQT &
    APP_PID=\\\$!
    echo 'App started with PID:' \\\$APP_PID
    # Keep container alive
    wait \\\$APP_PID || sleep infinity
else
    echo 'openterfaceQT not found, checking installation...'
    ls -la /usr/local/bin/openterface* 2>&1 || echo 'No openterface binaries found'
    echo 'Binary check failed'
    sleep infinity
fi
\""

# Execute the docker run command
CONTAINER_ID=$(eval $DOCKER_RUN_CMD)

echo -e "${GREEN}✅ Container started${NC}"
echo -e "${BLUE}📦 Container ID: ${CONTAINER_ID:0:12}${NC}"
echo -e "${BLUE}📱 App is initializing...${NC}"

# Quick initial check to see if container is still running
sleep 2
if ! docker ps | grep -q $CONTAINER_ID; then
    echo -e "${RED}⚠️  Container exited immediately after start!${NC}"
    echo -e "${BLUE}📋 Early exit logs:${NC}"
    docker logs $CONTAINER_ID 2>&1 | sed 's/^/   /'
    echo ""
    echo -e "${YELLOW}💡 Possible issues:${NC}"
    echo "   - Check if the Docker image exists and is properly built"
    echo "   - Verify /usr/local/bin/openterfaceQT exists in the image"
    echo "   - Check X11 display connection"
    echo "   - Verify Qt dependencies are installed"
    echo ""
fi

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
else
    echo -e "${GREEN}✅ App startup complete!${NC}"
fi

# Take the main screenshot
echo -e "${BLUE}📸 Taking screenshot...${NC}"
timestamp=$(date +"%Y%m%d_%H%M%S")
screenshot_jpg="$SCREENSHOTS_DIR/openterface_app_${timestamp}.jpg"

# Use ImageMagick import for reliable JPG screenshot capture
echo -e "${BLUE}📷 Taking screenshot with ImageMagick (PNG/JPG)...${NC}"
screenshot_success=false

# ImageMagick import is the most reliable method for this virtual display setup
if command -v import >/dev/null 2>&1; then
    if DISPLAY=:98 import -window root -quality 90 "$screenshot_jpg" 2>/dev/null; then
        echo -e "${GREEN}✅ JPG screenshot saved: $screenshot_jpg${NC}"
        screenshot_success=true
    else
        echo -e "${RED}❌ ImageMagick screenshot failed${NC}"
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
