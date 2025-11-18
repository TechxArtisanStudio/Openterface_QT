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

# Wayland support - detect and enable if available
# Allow launcher script to detect and choose the best platform backend
# Default to Wayland, but can be overridden by OPENTERFACE_LAUNCHER_PLATFORM
#export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
#export OPENTERFACE_LAUNCHER_PLATFORM=${OPENTERFACE_LAUNCHER_PLATFORM:-wayland}

# Debug: Show Wayland environment variables
echo "🔍 DEBUG - Wayland Configuration:"
echo "   WAYLAND_DISPLAY: $WAYLAND_DISPLAY"
echo "   OPENTERFACE_LAUNCHER_PLATFORM: $OPENTERFACE_LAUNCHER_PLATFORM"
echo "   XDG_RUNTIME_DIR: ${XDG_RUNTIME_DIR:-not set}"

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
    
    # Let launcher script detect the platform backend (Wayland or X11)
    # The OPENTERFACE_LAUNCHER_PLATFORM environment variable signals the preferred platform
    echo "ℹ️  Platform detection: OPENTERFACE_LAUNCHER_PLATFORM=${OPENTERFACE_LAUNCHER_PLATFORM}"
    echo "ℹ️  Display backend: QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-wayland}"
    export QT_X11_NO_MITSHM=1
    export QT_DEBUG_PLUGINS=1  # Enable plugin debugging
    export QT_LOGGING_RULES="qt.qpa.plugin=true"  # Log plugin loading
    
    # CRITICAL: Add Docker system Qt plugin paths to QT_PLUGIN_PATH
    # AppImage has its own bundled plugins, but we add Docker system paths as fallback
    # AppImage's own paths will be searched first due to APPIMAGE_EXTRACT_AND_RUN or AppImage's internal setup
    export QT_PLUGIN_PATH="${QT_PLUGIN_PATH}:/usr/lib/openterfaceqt/qt6/plugins:/usr/lib/qt6/plugins:/usr/lib/x86_64-linux-gnu/qt6/plugins:/usr/lib/qt6/plugins/platforms"
    export QML2_IMPORT_PATH="${QML2_IMPORT_PATH}:/usr/lib/openterfaceqt/qt6/qml:/usr/lib/qt6/qml:/usr/lib/x86_64-linux-gnu/qt6/qml"
    
    # CRITICAL: Only set QT_QPA_PLATFORM_PLUGIN_PATH for non-AppImage installations
    # For AppImage, let it use its own plugin path from APPIMAGE environment
    # Only set system paths if APPIMAGE is not set (non-AppImage installations)
    if [ -z "$APPIMAGE" ]; then
        # Non-AppImage (system installed or deb package)
        # Explicitly set platform plugin path for system installations
        export QT_QPA_PLATFORM_PLUGIN_PATH="/usr/lib/openterfaceqt/qt6/plugins/platforms:/usr/lib/qt6/plugins/platforms:/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
        echo "ℹ️  Non-AppImage detected: Using system Qt plugin paths"
    else
        # AppImage installation
        # Let AppImage handle its own plugin paths, don't override
        echo "ℹ️  AppImage detected (APPIMAGE=$APPIMAGE): Using AppImage plugin paths"
    fi
    
    # CRITICAL: Ensure LD_LIBRARY_PATH includes both AppImage and Docker Qt libraries
    # This allows AppImage bundled libraries to be found, with Docker as fallback
    export LD_LIBRARY_PATH="/usr/lib/openterfaceqt/qt6:/usr/lib/openterfaceqt/ffmpeg:/usr/lib/openterfaceqt/gstreamer:/usr/lib/openterfaceqt:/usr/lib/qt6:/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH}"
    
    # Start the application and keep container running
    echo "📝 Starting Openterface QT..."
    echo "   DISPLAY=$DISPLAY"
    echo "   QT_QPA_PLATFORM=$QT_QPA_PLATFORM"
    echo "   APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN:-0}"
    echo "   LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
    echo "   QT_PLUGIN_PATH=$QT_PLUGIN_PATH"
    echo ""
    
    # Debug: Show library dependencies
    echo "🔍 Library Diagnostics:"
    echo "   LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
    
    if [ -f /usr/local/bin/openterfaceQT ]; then
        ls -trl /usr/local/bin/openterfaceQT 2>&1
        if [ -L /usr/local/bin/openterfaceQT ]; then
            echo "   Final target: $(readlink -f /usr/local/bin/openterfaceQT)"
        fi
    fi
    echo ""
    
    echo "🔧 Starting Openterface QT application..."
    OPENTERFACE_DEBUG=1 /usr/local/bin/openterfaceQT > /tmp/openterfaceqt.log 2>&1 &
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

