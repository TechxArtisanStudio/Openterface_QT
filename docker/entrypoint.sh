#!/bin/bash
# Entrypoint script for Openterface QT Docker container
# Handles installation on first run when artifacts are available via volume mount

echo "🔧 Openterface QT Docker Entrypoint"
echo "===================================="

# Run installation if not already installed
if [ ! -f /usr/local/bin/openterfaceQT ]; then
    echo "📦 Openterface not yet installed. Running installation..."
    
    # Run the installation script
    if /tmp/install-openterface-shared.sh; then
        echo "✅ Installation completed successfully"
    else
        echo "⚠️  Installation failed but continuing..."
        # Don't exit on installation failure to allow debugging
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
