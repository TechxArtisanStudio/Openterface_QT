#!/bin/bash
# =============================================================================
# Openterface QT Static Testing Installation Script
# =============================================================================
#
# This script downloads and installs the latest Openterface QT static package
# with minimal runtime dependencies for testing purposes.
#
# OVERVIEW:
# - Downloads the latest static release package from GitHub
# - Installs the static package 
# - Sets up proper device permissions and udev rules
# - Configures the application for immediate testing
#
# Static builds have fewer runtime dependencies since libraries are embedded
# =============================================================================

set -e  # Exit on any error

echo "🚀 Openterface QT Static Testing Installation Script"
echo "===================================================="

# Configuration
GITHUB_REPO="TechxArtisanStudio/Openterface_QT"
STATIC_PACKAGE_NAME="openterfaceQT-portable"  # Assuming static portable app

# Function to get the specified version
get_latest_version() {
    echo "🔍 Fetching latest release information..."
    # Use GitHub API to get the latest release
    LATEST_VERSION=$(curl -s "https://api.github.com/repos/${GITHUB_REPO}/releases/latest" | \
                     grep '"tag_name":' | \
                     sed -E 's/.*"([^"]+)".*/\1/')
    
    echo "✅ Latest version: $LATEST_VERSION"
}

# Function to check if static package exists
check_static_package_exists() {
    local version=$1
    local static_url="https://github.com/${GITHUB_REPO}/releases/download/${version}/${STATIC_PACKAGE_NAME}"
    
    echo "🔍 Checking if static package exists..."
    if curl --output /dev/null --silent --head --fail "$static_url"; then
        echo "✅ Static package found: $STATIC_PACKAGE_NAME"
        PACKAGE_NAME="$STATIC_PACKAGE_NAME"
        return 0
    else
        echo "⚠️  Static package not found, falling back to regular package: $FALLBACK_PACKAGE_NAME"
        PACKAGE_NAME="$FALLBACK_PACKAGE_NAME"
        return 1
    fi
}

# Function to download the package
download_package() {
    echo "📥 Downloading Openterface QT package..."
    
    # Get specified version
    get_latest_version
    
    # Check for static package, fallback to regular if not found
    check_static_package_exists "$LATEST_VERSION"
    
    DOWNLOAD_URL="https://github.com/${GITHUB_REPO}/releases/download/${LATEST_VERSION}/${PACKAGE_NAME}"
    
    echo "   URL: $DOWNLOAD_URL"
    
    # Download with retries
    for i in {1..3}; do
        echo "   Attempt $i/3..."
        if wget -q --show-progress "$DOWNLOAD_URL" -O "/tmp/${PACKAGE_NAME}"; then
            echo "✅ Download completed successfully"
            return 0
        else
            echo "❌ Download attempt $i failed"
            if [ $i -eq 3 ]; then
                echo "❌ All download attempts failed"
                exit 1
            fi
            sleep 2
        fi
    done
}

# Function to install the package
install_package() {
    echo "📦 Installing Openterface QT package..."
    
    if [ ! -f "/tmp/${PACKAGE_NAME}" ]; then
        echo "❌ Package file not found: /tmp/${PACKAGE_NAME}"
        exit 1
    fi
    
    cp /tmp/${PACKAGE_NAME}/usr/bin
    
    # Clean up downloaded package
    rm "/tmp/${PACKAGE_NAME}"
}

# Function to set up device permissions and udev rules
setup_device_permissions() {
    echo "🔧 Setting up device permissions and udev rules..."
    
    # Create udev rules directory if it doesn't exist
    mkdir -p /etc/udev/rules.d/
    
    # Create udev rules for Openterface hardware
    cat > /etc/udev/rules.d/50-openterface.rules << 'EOF'
# Openterface Mini-KVM udev rules
# Main device (HID interface)
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess", MODE="0666"
SUBSYSTEM=="usb", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess", MODE="0666"

# Serial interface (CH340 chip)
SUBSYSTEM=="ttyUSB", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess", MODE="0666"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess", MODE="0666"

# Additional permissions for device access
KERNEL=="hidraw*", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", MODE="0666", GROUP="plugdev"
EOF
    
    echo "✅ Udev rules created at /etc/udev/rules.d/50-openterface.rules"
    
    # Reload udev rules
    if command -v udevadm >/dev/null 2>&1; then
        udevadm control --reload-rules
        udevadm trigger
        echo "✅ Udev rules reloaded"
    else
        echo "⚠️  udevadm not available, udev rules will be active after restart"
    fi
}

# Function to create startup script
create_startup_script() {
    echo "📝 Creating startup script..."
    
    cat > /usr/local/bin/start-openterface.sh << 'EOF'
#!/bin/bash
# Openterface QT Startup Script for Docker Testing Environment

echo "🚀 Starting Openterface QT Application..."
echo "=========================================="

# Check if the application binary exists
if [ -f "/usr/local/bin/openterfaceQT" ]; then
    BINARY_PATH="/usr/local/bin/openterfaceQT"
elif [ -f "/usr/bin/openterfaceQT" ]; then
    BINARY_PATH="/usr/bin/openterfaceQT"
elif [ -f "/opt/openterfaceQT/bin/openterfaceQT" ]; then
    BINARY_PATH="/opt/openterfaceQT/bin/openterfaceQT"
else
    echo "❌ Openterface QT binary not found in expected locations"
    echo "   Searched: /usr/local/bin, /usr/bin, /opt/openterfaceQT/bin"
    exit 1
fi

echo "✅ Found binary at: $BINARY_PATH"

# Check display environment
if [ -z "$DISPLAY" ]; then
    echo "⚠️  DISPLAY environment variable not set"
    echo "   Setting DISPLAY=:0"
    export DISPLAY=:0
fi

echo "🖥️  Display: $DISPLAY"

# Set QT environment for better compatibility
export QT_X11_NO_MITSHM=1
export QT_QPA_PLATFORM=xcb

# Check for device access
echo "🔍 Checking device access..."
if ls /dev/hidraw* >/dev/null 2>&1; then
    echo "✅ HID devices found: $(ls /dev/hidraw*)"
else
    echo "⚠️  No HID devices found - hardware may not be connected"
fi

if ls /dev/ttyUSB* >/dev/null 2>&1; then
    echo "✅ USB serial devices found: $(ls /dev/ttyUSB*)"
else
    echo "⚠️  No USB serial devices found"
fi

# Start the application
echo "🎬 Launching Openterface QT..."
echo "   Binary: $BINARY_PATH"
echo "   Arguments: $@"
echo ""

exec "$BINARY_PATH" "$@"
EOF

    chmod +x /usr/local/bin/start-openterface.sh
    echo "✅ Startup script created at /usr/local/bin/start-openterface.sh"
}

# Function to verify installation
verify_installation() {
    echo "🔍 Verifying installation..."
    
    # Check if binary exists and is executable
    BINARY_PATHS=("/usr/local/bin/openterfaceQT" "/usr/bin/openterfaceQT" "/opt/openterfaceQT/bin/openterfaceQT")
    BINARY_FOUND=false
    
    for path in "${BINARY_PATHS[@]}"; do
        if [ -f "$path" ] && [ -x "$path" ]; then
            echo "✅ Binary found and executable: $path"
            BINARY_FOUND=true
            
            # For static builds, check if it's actually static
            if command -v ldd >/dev/null 2>&1; then
                echo "📊 Checking binary dependencies:"
                ldd_output=$(ldd "$path" 2>&1 || true)
                if echo "$ldd_output" | grep -q "not a dynamic executable"; then
                    echo "✅ Binary is statically linked (no dynamic dependencies)"
                elif echo "$ldd_output" | grep -q "linux-vdso\|ld-linux"; then
                    echo "📋 Binary dependencies (dynamic/partial static):"
                    echo "$ldd_output" | head -10
                fi
            fi
            break
        fi
    done
    
    if [ "$BINARY_FOUND" = false ]; then
        echo "❌ No executable binary found in standard locations"
        echo "   This may indicate an installation problem"
        return 1
    fi
    
    # Check startup script
    if [ -x "/usr/local/bin/start-openterface.sh" ]; then
        echo "✅ Startup script is executable"
    else
        echo "❌ Startup script not found or not executable"
        return 1
    fi
    
    # Check udev rules
    if [ -f "/etc/udev/rules.d/50-openterface.rules" ]; then
        echo "✅ Udev rules installed"
    else
        echo "❌ Udev rules not found"
        return 1
    fi
    
    echo "✅ Installation verification completed successfully"
    return 0
}

# Main installation process
main() {
    echo "🏁 Starting Openterface QT Static Installation..."
    echo ""
    
    # Step 1: Download package
    download_package
    
    # Step 2: Install package
    install_package
    
    # Step 3: Set up device permissions
    setup_device_permissions
    
    # Step 4: Create startup script
    create_startup_script
    
    # Step 5: Verify installation
    if verify_installation; then
        echo ""
        echo "🎉 Openterface QT Static Installation Completed Successfully!"
        echo "============================================================"
        echo ""
        echo "📋 Installation Summary:"
        echo "   • Package type: Static build (minimal dependencies)"
        echo "   • Binary location: Auto-detected"
        echo "   • Startup script: /usr/local/bin/start-openterface.sh"
        echo "   • Udev rules: /etc/udev/rules.d/50-openterface.rules"
        echo ""
        echo "🚀 To run the application:"
        echo "   /usr/local/bin/start-openterface.sh"
        echo ""
        echo "🔧 For debugging:"
        echo "   • Check logs in the application"
        echo "   • Verify hardware connections"
        echo "   • Ensure proper display forwarding"
        echo ""
    else
        echo ""
        echo "❌ Installation verification failed!"
        echo "   Please check the installation manually"
        exit 1
    fi
}

# Run main installation
main "$@"
