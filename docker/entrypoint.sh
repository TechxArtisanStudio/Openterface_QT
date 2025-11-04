#!/bin/bash
# Entrypoint script for Openterface QT Docker container
# Handles installation on first run when artifacts are available via volume mount

echo "🔧 Openterface QT Docker Entrypoint"
echo "===================================="

# Run installation if not already installed
if [ ! -f /usr/local/bin/openterfaceQT ]; then
    echo "📦 Openterface not yet installed. Running installation..."
    
    # Run the installation script with proper permissions
    # If running as non-root but with sudo access, use sudo
    # Otherwise try to run directly
    if [ "$(id -u)" -ne 0 ]; then
        # Non-root user: use sudo
        if sudo -n /tmp/install-openterface-shared.sh 2>/dev/null; then
            echo "✅ Installation completed successfully"
        else
            # Try with password prompt if sudo without password fails
            if sudo /tmp/install-openterface-shared.sh; then
                echo "✅ Installation completed successfully"
            else
                echo "⚠️  Installation failed but continuing..."
            fi
        fi
    else
        # Already root: run directly
        if /tmp/install-openterface-shared.sh; then
            echo "✅ Installation completed successfully"
        else
            echo "⚠️  Installation failed but continuing..."
        fi
    fi
else
    echo "✅ Openterface already installed"
fi

echo "🚀 Launching application..."
echo ""

# Set display environment for X11
export DISPLAY="${DISPLAY:-:0}"
export QT_X11_NO_MITSHM=1
export QT_QPA_PLATFORM=xcb

# Execute the command passed to docker run, or default to bash
if [ $# -eq 0 ]; then
    # No command provided, use bash as default
    exec /bin/bash
else
    # Execute provided command
    exec "$@"
fi
