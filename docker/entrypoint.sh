#!/bin/bash
# Entrypoint script for Openterface QT Docker container
# Handles installation on first run when artifacts are available via volume mount

echo "🔧 Openterface QT Docker Entrypoint"
echo "===================================="

# Set up error handler to catch unexpected exits (but don't exit, just log)
trap 'echo "⚠️ WARNING: Command at line $LINENO returned exit code $?"; true' ERR

# Set display environment for X11 early
# Force DISPLAY to :98 for screenshot testing (override any defaults)
export DISPLAY=:98
export QT_X11_NO_MITSHM=1
export QT_QPA_PLATFORM=xcb

# Set Qt plugin and QML paths (must point to bundled locations in deb package)
export QT_PLUGIN_PATH=/usr/lib/qt6/plugins:/usr/lib/x86_64-linux-gnu/qt6/plugins
export QML2_IMPORT_PATH=/usr/lib/qt6/qml:/usr/lib/x86_64-linux-gnu/qt6/qml

# Set library path for bundled libraries (especially for JPEG support)
export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH

# Fix locale to UTF-8 (Qt6 requirement)
export LC_ALL=C.UTF-8
export LANG=C.UTF-8

# Get installation type from environment variable (set by Dockerfile build arg)
# INSTALL_TYPE is passed via environment, not as command argument
INSTALL_TYPE_ARG="${INSTALL_TYPE:-}"

# Run installation if not already installed and INSTALL_TYPE is specified
if [ ! -f /usr/local/bin/openterfaceQT ] && [ -n "$INSTALL_TYPE_ARG" ]; then
    echo "📦 Openterface not yet installed. Running installation for: $INSTALL_TYPE_ARG"
    
    # Use unified installation script with INSTALL_TYPE
    INSTALL_SCRIPT="/docker/install-openterface.sh"
    
    # Run installation (container entrypoint runs as root)
    INSTALL_EXIT_CODE=0
    if [ "$(id -u)" -eq 0 ]; then
        # Running as root: run directly
        if "$INSTALL_SCRIPT" "$INSTALL_TYPE_ARG"; then
            INSTALL_EXIT_CODE=$?
            echo "✅ Installation completed successfully"
        else
            INSTALL_EXIT_CODE=$?
            echo ""
            echo "❌ Installation failed with exit code: $INSTALL_EXIT_CODE"
            echo ""
            echo "Please ensure one of the following:"
            echo "  1. Volume mount with DEB package: -v /path/to/build:/tmp/build-artifacts"
            echo "  2. Set GITHUB_TOKEN for artifact download: -e GITHUB_TOKEN=..."
            echo "  3. Check Docker permissions and network access"
            echo ""
            # Don't exit here - continue to app launch for debugging
            # This allows docker exec to work for troubleshooting
        fi
    else
        # Not root, shouldn't happen in container but handle gracefully
        echo "❌ Installation requires root privileges"
        echo "Please run container without -u flag to keep root for entrypoint"
    fi
else
    if [ -f /usr/local/bin/openterfaceQT ]; then
        echo "✅ Openterface already installed"
    else
        echo "ℹ️  No installation type specified. Use INSTALL_TYPE environment variable (deb|appimage)"
        echo ""
        echo "Examples:"
        echo "  docker run -e INSTALL_TYPE=deb -v /path/to/artifacts:/tmp/build-artifacts <image>"
        echo "  docker run -e INSTALL_TYPE=appimage <image>"
    fi
fi

echo "🚀 Launching Openterface application..."
echo ""

# Start Xvfb inside the container (may need root privileges)
echo "🖥️  Starting Xvfb virtual display..."
echo "   Current user: $(id)"

# Xvfb typically needs root or special privileges to access /tmp/.X11-unix
# Try multiple approaches in order of preference
XVFB_PID=""

# Approach 1: Direct execution (if running as root or user has permissions)
if Xvfb $DISPLAY -screen 0 1920x1080x24 -ac +extension GLX +render -noreset >/dev/null 2>&1 &
then
    XVFB_PID=$!
    echo "✅ Xvfb started directly (PID: $XVFB_PID)"
fi

# Approach 2: Try with sudo if direct failed
if [ -z "$XVFB_PID" ] || ! ps -p $XVFB_PID > /dev/null 2>&1; then
    if sudo -n Xvfb $DISPLAY -screen 0 1920x1080x24 -ac +extension GLX +render -noreset >/dev/null 2>&1 &
    then
        XVFB_PID=$!
        echo "✅ Xvfb started with sudo (PID: $XVFB_PID)"
    fi
fi

# Verify Xvfb started
if [ -n "$XVFB_PID" ] && ps -p $XVFB_PID > /dev/null 2>&1; then
    echo "✅ Xvfb started successfully (PID: $XVFB_PID, Display: $DISPLAY)"
elif [ -S /tmp/.X11-unix/98 ]; then
    # Socket exists but we couldn't verify PID - this is ok, X11 is running
    echo "✅ X11 display socket found at /tmp/.X11-unix/98"
else
    echo "⚠️  Xvfb startup status uncertain"
    echo "   Running as: $(id)"
    echo "   Display: $DISPLAY"
    echo "   Checking for X11 socket..."
    ls -la /tmp/.X11-unix/ 2>/dev/null || echo "   No X11 sockets found"
    echo "   Continuing anyway for persistent testing container..."
    # Don't exit here - for persistent container testing, we need to keep running
fi

echo "✅ X11 display setup complete"
echo ""

# Try to launch the openterfaceQT application
if [ -f /usr/local/bin/openterfaceQT ]; then
    echo "📌 DEBUG ENTERED IF BLOCK for openterfaceQT"
    echo "✅ Found openterfaceQT application, starting it..."
    
    # Verify the binary is executable
    if [ ! -x /usr/local/bin/openterfaceQT ]; then
        echo "⚠️  Binary is not executable, fixing permissions..."
        chmod +x /usr/local/bin/openterfaceQT
    fi
    
    # Determine package type from INSTALL_TYPE_ARG
    if [ "$INSTALL_TYPE_ARG" = "appimage" ]; then
        echo "ℹ️  Detected AppImage format (from INSTALL_TYPE)"
        # Don't extract AppImage - run it directly with FUSE
        export APPIMAGE_EXTRACT_AND_RUN=0
        echo "ℹ️  AppImage will run directly (FUSE is available)"
    elif [ "$INSTALL_TYPE_ARG" = "deb" ]; then
        echo "ℹ️  Detected DEB package (from INSTALL_TYPE)"
        # For DEB packages, ensure Qt and system libraries are available
        export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/usr/lib:/lib/x86_64-linux-gnu:/lib:$LD_LIBRARY_PATH
        export QT_PLUGIN_PATH=/usr/lib/qt6/plugins:/usr/lib/x86_64-linux-gnu/qt6/plugins
        export QML2_IMPORT_PATH=/usr/lib/qt6/qml:/usr/lib/x86_64-linux-gnu/qt6/qml
        # Set bundled Qt platform plugins path for installed application
        export QT_QPA_PLATFORM_PLUGIN_PATH=/usr/lib/openterfaceqt/qt6/plugins/platforms
        echo "ℹ️  Library paths configured for system Qt6"
        echo "ℹ️  QT_QPA_PLATFORM_PLUGIN_PATH set to: $QT_QPA_PLATFORM_PLUGIN_PATH"
    else
        echo "ℹ️  Package type unknown, using default configuration"
    fi
    
    # Set up display environment for GUI
    echo "ℹ️  DISPLAY environment: $DISPLAY"
    
    # Verify X11 connection before starting app
    echo "🔍 X11 Connection Diagnostics:"
    echo "   DISPLAY=$DISPLAY"
    
    # Check if xdpyinfo is available for connection test
    if command -v xdpyinfo >/dev/null 2>&1; then
        if xdpyinfo -display "$DISPLAY" >/dev/null 2>&1; then
            echo "   ✅ X11 display connection verified"
        else
            echo "   ⚠️  Cannot verify X11 connection (xdpyinfo failed)"
            echo "   Will attempt to start app anyway..."
        fi
    else
        echo "   ℹ️  xdpyinfo not available, skipping connection test"
    fi
    
    export QT_QPA_PLATFORM=xcb
    echo "ℹ️  Using X11 display: $DISPLAY"
    export QT_X11_NO_MITSHM=1
    export QT_DEBUG_PLUGINS=1  # Enable plugin debugging
    export QT_LOGGING_RULES="qt.qpa.plugin=true"  # Log plugin loading
    
    # Start the application and keep container running
    echo "📝 Starting Openterface QT..."
    echo "   DISPLAY=$DISPLAY"
    echo "   QT_QPA_PLATFORM=$QT_QPA_PLATFORM"
    echo "   APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN:-0}"
    echo ""
    
    # Debug: Show library dependencies
    echo "🔍 Library Diagnostics:"
    echo "   LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
    
    # Check for libusb in AppImage
    if [ "$INSTALL_TYPE_ARG" = "appimage" ]; then
        echo "   AppImage libusb check:"
        APPIMAGE_EXTRACTED=$(find /tmp -maxdepth 1 -type d -name "appimage_extracted_*" -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -1 | cut -d' ' -f2-)
        
        if [ -n "$APPIMAGE_EXTRACTED" ] && [ -d "$APPIMAGE_EXTRACTED" ]; then
            echo "     ℹ️  Extracted AppImage found at: $APPIMAGE_EXTRACTED"
            if find "$APPIMAGE_EXTRACTED" -name "libusb-1.0.so*" 2>/dev/null | grep -q .; then
                echo "     ✅ libusb IS bundled in AppImage"
                find "$APPIMAGE_EXTRACTED" -name "libusb-1.0.so*" 2>/dev/null | while read lib; do
                    echo "       - $lib"
                done
            else
                echo "     ❌ libusb NOT found in AppImage"
            fi
        else
            echo "     ⏳ AppImage not yet extracted (will extract on first run)"
        fi
        echo ""
    fi
    
    if [ -f /usr/local/bin/openterfaceQT ]; then
        echo "   Checking openterfaceQT dependencies:"
        ldd /usr/local/bin/openterfaceQT 2>&1 || echo "     (ldd check skipped for AppImage)"
    fi
    echo ""
    
    echo "🔧 Starting Openterface QT application..."
    
    # For AppImage, create a wrapper that sets LD_LIBRARY_PATH after extraction
    if [ "$INSTALL_TYPE_ARG" = "appimage" ]; then
        # Run AppImage with a wrapper script that detects and uses bundled libraries
        cat > /tmp/run-appimage.sh << 'EOF'
#!/bin/bash
# Find the most recent AppImage extraction directory
APPIMAGE_EXTRACTED=$(find /tmp -maxdepth 1 -type d -name "appimage_extracted_*" -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -1 | cut -d' ' -f2-)

if [ -n "$APPIMAGE_EXTRACTED" ] && [ -d "$APPIMAGE_EXTRACTED/usr/lib" ]; then
    export LD_LIBRARY_PATH="$APPIMAGE_EXTRACTED/usr/lib:$LD_LIBRARY_PATH"
    echo "ℹ️  Using AppImage bundled libraries from: $APPIMAGE_EXTRACTED/usr/lib" >&2
fi

# Enable more debugging
export QT_DEBUG_PLUGINS=1
export QT_LOGGING_RULES="*=true"

# Print environment for debugging
echo "=== Application Runtime Environment ===" >&2
echo "DISPLAY=$DISPLAY" >&2
echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH" >&2
echo "QT_QPA_PLATFORM=$QT_QPA_PLATFORM" >&2
echo "HOME=$HOME" >&2
echo "======================================" >&2

exec /usr/local/bin/openterfaceQT "$@"
EOF
        chmod +x /tmp/run-appimage.sh
        # Use stdbuf to unbuffer output for immediate logging (if available and compatible)
        if command -v stdbuf >/dev/null 2>&1; then
            stdbuf -oL -eL env DISPLAY=$DISPLAY QT_QPA_PLATFORM=$QT_QPA_PLATFORM APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN:-0} /tmp/run-appimage.sh > /tmp/openterfaceqt.log 2>&1 &
        else
            # Fallback without stdbuf if not available or incompatible
            env DISPLAY=$DISPLAY QT_QPA_PLATFORM=$QT_QPA_PLATFORM APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN:-0} /tmp/run-appimage.sh > /tmp/openterfaceqt.log 2>&1 &
        fi
    else
        # Use stdbuf to unbuffer output for immediate logging (if available and compatible)
        if command -v stdbuf >/dev/null 2>&1; then
            stdbuf -oL -eL env DISPLAY=$DISPLAY QT_QPA_PLATFORM=$QT_QPA_PLATFORM APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN:-0} /usr/local/bin/openterfaceQT > /tmp/openterfaceqt.log 2>&1 &
        else
            # Fallback without stdbuf if not available or incompatible
            env DISPLAY=$DISPLAY QT_QPA_PLATFORM=$QT_QPA_PLATFORM APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN:-0} /usr/local/bin/openterfaceQT > /tmp/openterfaceqt.log 2>&1 &
        fi
    fi
    APP_PID=$!
    
    echo "✅ Openterface QT started with PID: $APP_PID"
    echo "📋 Log file: /tmp/openterfaceqt.log"
    echo ""
    
    # Wait longer for app to initialize (increase from 2 to 10 seconds)
    # Use read with timeout instead of sleep for reliability
    read -t 10 _ < /dev/null || true
    
    # Check if app is still running and log more details
    if ps -p $APP_PID > /dev/null 2>&1; then
        echo "✅ Openterface QT is running!"
        # Optionally, add a command to verify rendering, e.g., check for window creation
    else
        echo "❌ Openterface QT process exited immediately"
        echo "Full log:"
        if [ -f /tmp/openterfaceqt.log ]; then
            cat /tmp/openterfaceqt.log 2>&1
        else
            echo "⚠️  Log file not created"
        fi
        
        # Additional diagnostics
        echo ""
        echo "🔍 Additional Diagnostics:"
        echo "   Process status: $(ps -p $APP_PID 2>&1 || echo 'Process not found')"
        echo "   System time: $(date)"
        echo "   Free memory: $(free -h 2>/dev/null || echo 'N/A')"
        echo "   Loaded Qt plugins:"
        find /usr/lib -name "*xcb*" -o -name "*platformtheme*" 2>/dev/null | head -10
        echo ""
        echo "⚠️  Note: Container is PERSISTENT and will stay running for debugging"
        echo "   You can use: docker exec <container> bash"
        echo "   To investigate issues and restart the app manually"
        # Don't exit - keep container running for persistent testing
    fi
    
    echo ""
    echo "📌 DEBUG: Reached 'ready for testing' message point"
    echo "Openterface QT is ready for testing!"
    echo ""
    
    # Keep container alive - app is running in background
    # This allows external commands to be run via docker exec
    echo "ℹ️  Openterface QT running in background (PID: $APP_PID)"
    echo ""
    echo "To interact with this container:"
    echo "  docker exec <container-id> bash"
    echo "  docker exec <container-id> import -window root /tmp/screenshot.png"
    echo ""
    
    # For a persistent container, we ignore SIGTERM and SIGINT
    # This prevents the container from exiting when Docker sends stop signals
    trap '' SIGTERM SIGINT
    
    echo "✅ Entering monitoring loop (PID $$)"
    
    # Monitor the app process - keep container alive for testing
    LOOP_COUNT=0
    while true; do
        LOOP_COUNT=$((LOOP_COUNT + 1))
        
        if ps -p $APP_PID > /dev/null 2>&1; then
            # App is still running
            # Just wait 2 seconds before checking again
            read -t 2 _ < /dev/null 2>/dev/null || true
            
            # Log status every 30 iterations (approximately every 60 seconds)
            if [ $((LOOP_COUNT % 30)) -eq 0 ]; then
                echo "ℹ️  Still monitoring app (PID: $APP_PID, loop iteration: $LOOP_COUNT)"
            fi
        else
            # App has exited - but for a persistent container, we keep running
            echo "⚠️  Openterface QT process exited"
            echo "ℹ️  Container stays alive for debugging"
            echo "   To restart the app, use: docker exec <container> /usr/local/bin/openterfaceQT &"
            echo "   Or enter the container: docker exec -it <container> bash"
            # Keep the container alive - don't exit
            # Just wait indefinitely
            read -t 60 _ < /dev/null 2>/dev/null || true
        fi
        if [ $LOOP_COUNT -ge 30 ]; then
            break
        fi
    done
    
    # This should never be reached, but if it is, keep the container running
    echo "⚠️ WARNING: Monitoring loop exited unexpectedly!"
    while true; do
        sleep 60
    done
else
    echo "📌 DEBUG: openterfaceQT file not found at /usr/local/bin/openterfaceQT"
    echo "⚠️ openterfaceQT application not found"
    echo "Available applications:"
    ls -la /usr/local/bin/openterface* 2>/dev/null || echo "None found"
    echo ""
    echo "🛠️  Installation likely failed. Checking details..."
    echo ""
    
    # Show installation attempts
    if [ -f /tmp/install.log ]; then
        echo "📋 Installation log tail:"
        tail -30 /tmp/install.log 2>/dev/null | sed 's/^/  /'
        echo ""
    fi
    
    echo "📌 DEBUG: Starting bash shell for debugging..."
    echo "You can use 'docker exec <container> bash' to investigate the installation issue"
    echo ""
    
    # Start bash in interactive mode if connected to a terminal, otherwise sleep
    if [ -t 0 ]; then
        exec /bin/bash -i
    else
        # Not connected to terminal - keep container running indefinitely
        echo "ℹ️  Container will stay alive for debugging (docker exec)"
        echo "🚀 Ready for testing!"
        trap '' SIGTERM SIGINT  # Ignore signals to keep container alive
        while true; do
            sleep 3600
        done
    fi
fi

