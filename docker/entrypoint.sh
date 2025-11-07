#!/bin/bash
# Entrypoint script for Openterface QT Docker container
# Handles installation on first run when artifacts are available via volume mount

echo "🔧 Openterface QT Docker Entrypoint"
echo "===================================="

# Set display environment for X11 early
export DISPLAY="${DISPLAY:-:0}"
export QT_X11_NO_MITSHM=1
export QT_QPA_PLATFORM=xcb

# Set Qt plugin and QML paths (must point to bundled locations in deb package)
export QT_PLUGIN_PATH=/usr/lib/qt6/plugins:/usr/lib/x86_64-linux-gnu/qt6/plugins
export QML2_IMPORT_PATH=/usr/lib/qt6/qml:/usr/lib/x86_64-linux-gnu/qt6/qml

# Fix locale to UTF-8 (Qt6 requirement)
export LC_ALL=C.UTF-8
export LANG=C.UTF-8

# Run installation if not already installed
if [ ! -f /usr/local/bin/openterfaceQT ] && [ ! -f /usr/local/bin/openterfaceQT.AppImage ]; then
    echo "📦 Openterface not yet installed. Running installation..."
    
    # Pass INSTALL_TYPE to installation script if set, default to deb
    INSTALL_TYPE="${INSTALL_TYPE:-deb}"
    export INSTALL_TYPE
    
    # Pass GITHUB_TOKEN if available
    if [ -n "$GITHUB_TOKEN" ]; then
        export GITHUB_TOKEN
        echo "✅ GITHUB_TOKEN is available for artifact downloads"
    else
        echo "⚠️  GITHUB_TOKEN not set - will attempt local artifact search only"
    fi
    
    # Determine which installation script to use
    if [ "$INSTALL_TYPE" = "appimage" ]; then
        INSTALL_SCRIPT="/docker/install-openterface-appimage.sh"
        INSTALL_DESC="AppImage"
    else
        INSTALL_SCRIPT="/docker/install-openterface-deb.sh"
        INSTALL_DESC="DEB"
    fi
    
    echo "📥 Using $INSTALL_DESC installation script: $INSTALL_SCRIPT"
    
    # Run installation as root (required for dpkg, apt-get, and file permissions)
    if [ "$(id -u)" -ne 0 ]; then
        # Not root, use sudo with environment variables
        if sudo -n -E "$INSTALL_SCRIPT" 2>/dev/null; then
            echo "✅ $INSTALL_DESC installation completed successfully"
        else
            # Try with password prompt (though sudo -n should work in Docker)
            if sudo -E "$INSTALL_SCRIPT" 2>&1; then
                echo "✅ $INSTALL_DESC installation completed successfully"
            else
                echo "⚠️  Installation may have had issues but continuing..."
            fi
        fi
    else
        # Already root: run directly
        if "$INSTALL_SCRIPT"; then
            echo "✅ $INSTALL_DESC installation completed successfully"
        else
            echo "⚠️  Installation may have had issues but continuing..."
        fi
    fi
else
    echo "✅ Openterface already installed"
fi

echo "🚀 Launching application..."
echo ""

# Execute the command passed to docker run, or default to bash
if [ $# -eq 0 ]; then
    # No command provided, use bash as default
    exec /bin/bash
else
    # Execute provided command
    exec "$@"
fi

