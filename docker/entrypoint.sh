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
    if [ "$(id -u)" -ne 0 ]; then
        # Not root, use sudo
        if sudo -n "$INSTALL_SCRIPT" "$INSTALL_TYPE_ARG" 2>/dev/null; then
            echo "✅ Installation completed successfully"
        else
            # Try with password prompt (though sudo -n should work in Docker)
            if sudo "$INSTALL_SCRIPT" "$INSTALL_TYPE_ARG" 2>&1; then
                echo "✅ Installation completed successfully"
            else
                echo "⚠️  Installation may have had issues but continuing..."
            fi
        fi
    else
        # Already root: run directly
        if "$INSTALL_SCRIPT" "$INSTALL_TYPE_ARG"; then
            echo "✅ Installation completed successfully"
        else
            echo "⚠️  Installation may have had issues but continuing..."
        fi
    fi
else
    if [ -f /usr/local/bin/openterfaceQT ]; then
        echo "✅ Openterface already installed"
    else
        echo "ℹ️  No installation type specified. Use INSTALL_TYPE environment variable (deb|appimage)"
    fi
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

