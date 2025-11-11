#!/bin/bash
# Entrypoint script for Openterface QT Docker container
# Handles installation on first run when artifacts are available via volume mount

echo "🔧 Openterface QT Docker Entrypoint"
echo "===================================="

# Set display environment for X11 early
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
    
    # Run installation as root (required for dpkg and apt-get)
    INSTALL_EXIT_CODE=0
    if [ "$(id -u)" -ne 0 ]; then
        # Not root, use sudo
        if ! sudo -n "$INSTALL_SCRIPT" "$INSTALL_TYPE_ARG"; then
            INSTALL_EXIT_CODE=$?
            echo ""
            echo "❌ Installation failed with exit code: $INSTALL_EXIT_CODE"
            echo ""
            echo "Please ensure one of the following:"
            echo "  1. Volume mount with DEB package: -v /path/to/build:/tmp/build-artifacts"
            echo "  2. Set GITHUB_TOKEN for artifact download: -e GITHUB_TOKEN=..."
            echo "  3. Check Docker permissions and network access"
            echo ""
            echo "Run 'docker logs <container>' for detailed error messages"
            echo ""
        else
            echo "✅ Installation completed successfully"
        fi
    else
        # Already root: run directly
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
        fi
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

# Try to launch the openterfaceQT application
if [ -f /usr/local/bin/openterfaceQT ]; then
    echo "✅ Found openterfaceQT application, starting it..."
    
    # Verify the binary is executable
    if [ ! -x /usr/local/bin/openterfaceQT ]; then
        echo "⚠️  Binary is not executable, fixing permissions..."
        chmod +x /usr/local/bin/openterfaceQT
    fi
    
    # Check if it's an AppImage
    if file /usr/local/bin/openterfaceQT | grep -q "AppImage"; then
        echo "ℹ️  Detected AppImage format - using extraction for reliability"
        export APPIMAGE_EXTRACT_AND_RUN=1  # Use extraction for consistent behavior
    fi
    
    # Set up display environment for GUI
    # Note: DISPLAY should be set externally (e.g., :98 for Xvfb)
    # Only set QT_QPA_PLATFORM=offscreen if no DISPLAY is available
    echo "ℹ️  DISPLAY environment: $DISPLAY"
    if [ -z "$DISPLAY" ] || [ "$DISPLAY" = ":99" ]; then
        export DISPLAY=":99"
        export QT_QPA_PLATFORM=offscreen
        echo "ℹ️  No X11 display available, using offscreen rendering"
    else
        export QT_QPA_PLATFORM=xcb
        echo "ℹ️  Using X11 display: $DISPLAY"
    fi
    export QT_X11_NO_MITSHM=1
    export QT_DEBUG_PLUGINS=1  # Enable plugin debugging
    export QT_LOGGING_RULES="qt.qpa.plugin=true"  # Log plugin loading
    
    # Additional environment variables (only for non-AppImage or when not extracting)
    if ! file /usr/local/bin/openterfaceQT 2>/dev/null | grep -q "AppImage"; then
        export APPIMAGE_EXTRACT_AND_RUN=1
    else
        # For AppImage, try to use system Qt plugins if bundled ones don't work
        export QT_PLUGIN_PATH="/usr/lib/qt6/plugins:/usr/lib/x86_64-linux-gnu/qt6/plugins"
        export LD_LIBRARY_PATH="/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"
        echo "ℹ️  Using system Qt plugins and libraries for AppImage"
    fi
    export APPDIR="/tmp/.mount_openterfaceQT"  # AppImage mount point
    
    # Start the application and keep container running
    echo "📝 Starting Openterface QT..."
    echo "   DISPLAY=$DISPLAY"
    echo "   QT_QPA_PLATFORM=$QT_QPA_PLATFORM"
    # Force DISPLAY to the correct value if it's not set properly
    export DISPLAY="${DISPLAY:-:98}"
    nohup env DISPLAY=$DISPLAY QT_QPA_PLATFORM=$QT_QPA_PLATFORM /usr/local/bin/openterfaceQT > /tmp/openterfaceqt.log 2>&1 &
    APP_PID=$!
    
    echo "✅ Openterface QT started with PID: $APP_PID"
    echo "📋 Log file: /tmp/openterfaceqt.log"
    echo ""
    
    # Wait longer for app to initialize (increase from 2 to 10 seconds)
    sleep 10
    
    # Check if app is still running and log more details
    if ps -p $APP_PID > /dev/null 2>&1; then
        echo "✅ Openterface QT is running!"
        # Optionally, add a command to verify rendering, e.g., check for window creation
    else
        echo "❌ Openterface QT process exited"
        echo "Full log:"
        cat /tmp/openterfaceqt.log 2>&1 || true
        exit 1  # Fail early if app doesn't start
    fi
    
    echo ""
    echo "Openterface QT is ready for testing!"
    echo ""
    
    # Keep container running
    if [ $# -eq 0 ]; then
        # No command provided, keep container alive with sleep
        echo "ℹ️  No command provided, keeping container alive..."
        exec tail -f /dev/null
    else
        # Execute provided command (keeps container alive)
        echo "ℹ️  Executing command: $@"
        exec "$@"
    fi
else
    echo "⚠️ openterfaceQT application not found"
    echo "Available applications:"
    ls -la /usr/local/bin/openterface* 2>/dev/null || echo "None found"
    echo ""
    
    # Fall back to bash if application is not installed
    if [ $# -eq 0 ]; then
        exec /bin/bash
    else
        exec "$@"
    fi
fi

