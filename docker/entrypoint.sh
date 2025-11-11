#!/bin/bash
# Entrypoint script for Openterface QT Docker container
# Handles installation on first run when artifacts are available via volume mount

echo "🔧 Openterface QT Docker Entrypoint"
echo "===================================="

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

# Start Xvfb inside the container
echo "🖥️  Starting Xvfb virtual display..."
Xvfb $DISPLAY -screen 0 1920x1080x24 -ac +extension GLX +render -noreset >/dev/null 2>&1 &
XVFB_PID=$!
sleep 3

# Verify Xvfb started
if ps -p $XVFB_PID > /dev/null 2>&1; then
    echo "✅ Xvfb started successfully (PID: $XVFB_PID, Display: $DISPLAY)"
else
    echo "❌ Xvfb failed to start"
    exit 1
fi

# Try to launch the openterfaceQT application
if [ -f /usr/local/bin/openterfaceQT ]; then
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
        
        # Find the AppImage extraction directory and prioritize its libraries
        # This ensures the AppImage uses its bundled libusb instead of the system one
        APPIMAGE_EXTRACTED=$(find /tmp -maxdepth 1 -type d -name "appimage_extracted_*" 2>/dev/null | head -1)
        if [ -n "$APPIMAGE_EXTRACTED" ] && [ -d "$APPIMAGE_EXTRACTED/usr/lib" ]; then
            export LD_LIBRARY_PATH="$APPIMAGE_EXTRACTED/usr/lib:$LD_LIBRARY_PATH"
            echo "ℹ️  Prioritizing AppImage bundled libraries: $APPIMAGE_EXTRACTED/usr/lib"
        else
            echo "ℹ️  AppImage extraction path not found yet (will be set at runtime)"
        fi
    elif [ "$INSTALL_TYPE_ARG" = "deb" ]; then
        echo "ℹ️  Detected DEB package (from INSTALL_TYPE)"
        # For DEB packages, ensure Qt and system libraries are available
        export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/usr/lib:/lib/x86_64-linux-gnu:/lib:$LD_LIBRARY_PATH
        export QT_PLUGIN_PATH=/usr/lib/qt6/plugins:/usr/lib/x86_64-linux-gnu/qt6/plugins
        export QML2_IMPORT_PATH=/usr/lib/qt6/qml:/usr/lib/x86_64-linux-gnu/qt6/qml
        echo "ℹ️  Library paths configured for system Qt6"
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
    echo ""
    echo "   Available libusb-1.0.so.0 locations:"
    find /lib /usr/lib /tmp -name "libusb-1.0.so.0*" 2>/dev/null | while read lib; do
        echo "     - $lib"
        if [ -f "$lib" ]; then
            echo "       $(ldd "$lib" 2>&1 | grep -i glibc || echo 'No GLIBC info')"
        fi
    done
    echo ""
    
    if [ -f /usr/local/bin/openterfaceQT ]; then
        echo "   Checking openterfaceQT dependencies:"
        ldd /usr/local/bin/openterfaceQT 2>&1 | grep -E "libusb|libc\.so" || echo "     (ldd check skipped for AppImage)"
    fi
    echo ""
    
    echo "🔧 Executing command:"
    echo "   env DISPLAY=$DISPLAY QT_QPA_PLATFORM=$QT_QPA_PLATFORM APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN:-0} /usr/local/bin/openterfaceQT"
    echo ""
    
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

exec /usr/local/bin/openterfaceQT "$@"
EOF
        chmod +x /tmp/run-appimage.sh
        nohup env DISPLAY=$DISPLAY QT_QPA_PLATFORM=$QT_QPA_PLATFORM APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN:-0} /tmp/run-appimage.sh > /tmp/openterfaceqt.log 2>&1 &
    else
        nohup env DISPLAY=$DISPLAY QT_QPA_PLATFORM=$QT_QPA_PLATFORM APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN:-0} /usr/local/bin/openterfaceQT > /tmp/openterfaceqt.log 2>&1 &
    fi
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

