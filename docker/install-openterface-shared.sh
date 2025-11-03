#!/bin/bash
# =============================================================================
# Openterface QT Testing Installation Script
# =============================================================================
#
# This script downloads and installs the latest Openterface QT package
# with all necessary runtime dependencies for testing purposes.
#
# OVERVIEW:
# - Downloads the latest release package from GitHub
# - Installs the package using dpkg
# - Sets up proper device permissions and udev rules
# - Configures the application for immediate testing
#
# =============================================================================

set -e  # Exit on any error

echo "🚀 Openterface QT Testing Installation Script"
echo "=============================================="

# Configuration
GITHUB_REPO="TechxArtisanStudio/Openterface_QT"
PACKAGE_NAME="openterfaceQT.linux.amd64.shared.deb"

# Function to get the latest release version
get_latest_version() {
    echo "🔍 Fetching latest release information..."
    # Use GitHub API to get the latest release
    LATEST_VERSION=$(curl -s "https://api.github.com/repos/${GITHUB_REPO}/releases/latest" | \
                     grep '"tag_name":' | \
                     sed -E 's/.*"([^"]+)".*/\1/')
    
    echo "✅ Latest version: $LATEST_VERSION"
}

# Function to find the latest built deb package
find_latest_build_deb() {
    echo "🔍 Looking for latest Linux build artifacts (.deb files)..."
    
    # Search paths in order of preference
    local search_paths=(
        "/tmp/build-artifacts"
        "/build"
        "/workspace/build"
        "/src/build"
        "./build"
        "/tmp"
    )
    
    # Find all .deb files matching the pattern and get the newest one
    local latest_deb=""
    local latest_timestamp=0
    
    for search_path in "${search_paths[@]}"; do
        if [ -d "$search_path" ]; then
            echo "   Searching in: $search_path"
            # Look for deb files (openterfaceQT*.deb or similar)
            while IFS= read -r -d '' deb_file; do
                if [ -f "$deb_file" ]; then
                    # Get the modification time
                    local timestamp=$(stat -c %Y "$deb_file" 2>/dev/null || stat -f %m "$deb_file" 2>/dev/null || echo 0)
                    echo "   Found: $deb_file (timestamp: $timestamp)"
                    
                    if [ "$timestamp" -gt "$latest_timestamp" ]; then
                        latest_timestamp=$timestamp
                        latest_deb="$deb_file"
                    fi
                fi
            done < <(find "$search_path" -maxdepth 2 -name "*openterface*.deb" -o -name "openterfaceQT*.deb" -print0 2>/dev/null)
        fi
    done
    
    if [ -n "$latest_deb" ]; then
        echo "✅ Found latest build artifact: $latest_deb"
        echo "$latest_deb"
        return 0
    fi
    
    return 1
}

# Function to download the package
download_package() {
    echo "📥 Looking for Openterface QT package..."
    
    # First, try to find the latest Linux build artifact
    if built_package=$(find_latest_build_deb); then
        echo "✅ Using latest Linux build result: $built_package"
        if cp "$built_package" "/tmp/${PACKAGE_NAME}"; then
            echo "✅ Build artifact copied to /tmp/${PACKAGE_NAME}"
            return 0
        else
            echo "⚠️  Failed to copy build artifact, attempting local paths..."
        fi
    fi
    
    # Fallback: check for local built packages in standard locations
    LOCAL_PACKAGE_PATHS=(
        "/workspace/build/openterfaceQT_*.AppImage"
        "/workspace/build/openterfaceQT"
        "/workspace/build/*.deb"
        "/workspace/build/*.AppImage"
        "/build/*.deb"
        "/build/*.AppImage"
        "/tmp/${PACKAGE_NAME}"
        "./${PACKAGE_NAME}"
    )
    
    for path_pattern in "${LOCAL_PACKAGE_PATHS[@]}"; do
        # Expand glob pattern
        for potential_path in $path_pattern; do
            if [ -f "$potential_path" ]; then
                echo "✅ Found local package: $potential_path"
                cp "$potential_path" "/tmp/${PACKAGE_NAME}"
                return 0
            fi
        done
    done
    
    echo "ℹ️  No local build artifacts found. Downloading from GitHub releases as fallback..."
    DOWNLOAD_URL="https://github.com/${GITHUB_REPO}/releases/download/${LATEST_VERSION}/${PACKAGE_NAME}"
    
    echo "   URL: $DOWNLOAD_URL"
    
    # Download with retries
    for i in {1..3}; do
        if wget -O "/tmp/${PACKAGE_NAME}" "$DOWNLOAD_URL"; then
            echo "✅ Package downloaded successfully from GitHub"
            return 0
        else
            echo "⚠️  Download attempt $i failed, retrying..."
            sleep 2
        fi
    done
    
    echo "❌ Failed to find build artifacts or download package after 3 attempts"
    exit 1
}

# Function to install the package
install_package() {
    echo "📦 Installing Openterface QT package..."
    
    PACKAGE_FILE="/tmp/${PACKAGE_NAME}"
    
    # Determine package type based on actual file content or extension
    if [[ "$PACKAGE_FILE" == *.deb ]] && dpkg-deb --info "$PACKAGE_FILE" &>/dev/null; then
        echo "   Installing as Debian package..."
        if dpkg -i "$PACKAGE_FILE"; then
            echo "✅ Package installed successfully"
        else
            echo "⚠️  Package installation had dependency issues, fixing..."
            apt-get update
            apt-get install -f -y
            echo "✅ Dependencies resolved and package installed"
        fi
    elif [[ "$PACKAGE_FILE" == *.AppImage ]] || file "$PACKAGE_FILE" 2>/dev/null | grep -q "AppImage"; then
        echo "   Installing as AppImage (extracting contents)..."
        mkdir -p /opt/openterface
        cd /opt/openterface
        # Extract AppImage contents
        "$PACKAGE_FILE" --appimage-extract >/dev/null 2>&1 || {
            echo "   AppImage extraction failed, trying alternative method..."
            # Fallback: copy as binary if extraction fails
            cp "$PACKAGE_FILE" /usr/local/bin/openterfaceQT.AppImage
            chmod +x /usr/local/bin/openterfaceQT.AppImage
            echo "✅ AppImage installed as fallback to /usr/local/bin/openterfaceQT.AppImage"
            return 0
        }
        # Find the extracted binary
        EXTRACTED_BINARY=$(find squashfs-root -name "openterfaceQT" -type f -executable 2>/dev/null | head -1)
        if [ -n "$EXTRACTED_BINARY" ]; then
            cp "$EXTRACTED_BINARY" /usr/local/bin/openterfaceQT
            chmod +x /usr/local/bin/openterfaceQT
            echo "✅ AppImage extracted and binary installed to /usr/local/bin/openterfaceQT"
        else
            echo "   ❌ Could not find executable in extracted AppImage"
            # Fallback to copying the AppImage
            cp "$PACKAGE_FILE" /usr/local/bin/openterfaceQT.AppImage
            chmod +x /usr/local/bin/openterfaceQT.AppImage
            echo "✅ AppImage installed as fallback to /usr/local/bin/openterfaceQT.AppImage"
        fi
    else
        echo "   Installing as executable binary..."
        cp "$PACKAGE_FILE" /usr/local/bin/openterfaceQT
        chmod +x /usr/local/bin/openterfaceQT
        echo "✅ Binary installed to /usr/local/bin/openterfaceQT"
    fi
    
    # Clean up downloaded/copied package
    rm -f "$PACKAGE_FILE"
}

# Function to set up device permissions
setup_device_permissions() {
    echo "🔐 Setting up device permissions..."
    
    # Create udev rules for Openterface hardware
    cat > /etc/udev/rules.d/51-openterface.rules << 'EOF'
# Openterface HID device
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess", MODE="0666"
SUBSYSTEM=="usb", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess", MODE="0666"

# Serial interface chip
SUBSYSTEM=="ttyUSB", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess", MODE="0666"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess", MODE="0666"
EOF
    
    # Check if we're in a container/build environment
    IN_CONTAINER=false
    if [ -f /.dockerenv ] || [ "${container:-}" = "docker" ] || [ "${container:-}" = "podman" ]; then
        IN_CONTAINER=true
    fi
    
    # Reload udev rules (skip if udev is not running, e.g., during Docker build)
    if $IN_CONTAINER; then
        echo "ℹ️  Container environment detected - udev rules created, will be applied at runtime"
    elif systemctl is-active --quiet systemd-udevd 2>/dev/null || pgrep -x "systemd-udevd\|udevd" >/dev/null 2>&1; then
        echo "🔄 Reloading udev rules..."
        udevadm control --reload-rules 2>/dev/null || echo "⚠️  Could not reload udev rules"
        udevadm trigger 2>/dev/null || echo "⚠️  Could not trigger udev"
    else
        echo "ℹ️  udev not running - rules will be applied when udev starts"
    fi
    
    echo "✅ Device permissions configured"
}

# Function to verify installation
verify_installation() {
    echo "🔍 Verifying installation..."
    
    # Check if binary exists and is executable
    BINARY_LOCATIONS=(
        "/usr/local/bin/openterfaceQT"
        "/usr/local/bin/openterfaceQT.AppImage"
        "/usr/bin/openterfaceQT"
        "/opt/openterface/bin/openterfaceQT"
    )
    
    FOUND_BINARY=""
    for location in "${BINARY_LOCATIONS[@]}"; do
        if [ -f "$location" ] && [ -x "$location" ]; then
            FOUND_BINARY="$location"
            break
        fi
    done
    
    if [ -n "$FOUND_BINARY" ]; then
        echo "✅ Openterface QT binary found at: $FOUND_BINARY"
        
        # Check if we can get version info (non-GUI test)
        if timeout 5s "$FOUND_BINARY" --version 2>/dev/null || timeout 5s "$FOUND_BINARY" --help 2>/dev/null; then
            echo "✅ Binary is responsive"
        else
            echo "⚠️  Binary found but may require GUI environment to run"
        fi
    else
        echo "❌ Openterface QT binary not found in expected locations"
        return 1
    fi
    
    # Check for desktop file
    if [ -f "/usr/share/applications/openterfaceQT.desktop" ] || [ -f "/opt/openterface/share/applications/openterfaceQT.desktop" ]; then
        echo "✅ Desktop entry found"
    else
        echo "⚠️  Desktop entry not found (may affect GUI launching)"
    fi
}

# Function to create a launcher script
create_launcher() {
    echo "🚀 Creating launcher script..."
    
    cat > /usr/local/bin/start-openterface.sh << 'EOF'
#!/bin/bash

echo "🔧 Setting up device permissions..."

# Start udev if not running
if ! pgrep -x "systemd-udevd" > /dev/null && ! pgrep -x "udevd" > /dev/null; then
    echo "Starting udev..."
    sudo /lib/systemd/systemd-udevd --daemon 2>/dev/null || true
    sudo udevadm control --reload-rules 2>/dev/null || true
    sudo udevadm trigger 2>/dev/null || true
fi

# Wait a moment for udev to settle
sleep 1

# Ensure proper permissions for USB devices
echo "🔌 Setting USB device permissions..."
sudo chmod 666 /dev/ttyUSB* 2>/dev/null || true
sudo chmod 666 /dev/hidraw* 2>/dev/null || true
sudo chmod 666 /dev/bus/usb/*/* 2>/dev/null || true

# Set specific permissions for Openterface devices
if ls /dev/hidraw* 1> /dev/null 2>&1; then
    for device in /dev/hidraw*; do
        vendor=$(sudo udevadm info --name=$device --query=property | grep ID_VENDOR_ID | cut -d= -f2)
        product=$(sudo udevadm info --name=$device --query=property | grep ID_MODEL_ID | cut -d= -f2)
        if [ "$vendor" = "534d" ] && [ "$product" = "2109" ]; then
            echo "✅ Found Openterface device: $device"
            sudo chmod 666 $device
            sudo chown root:root $device
        fi
    done
fi

# Check for serial devices
if ls /dev/ttyUSB* 1> /dev/null 2>&1; then
    for device in /dev/ttyUSB*; do
        vendor=$(sudo udevadm info --name=$device --query=property | grep ID_VENDOR_ID | cut -d= -f2)
        product=$(sudo udevadm info --name=$device --query=property | grep ID_MODEL_ID | cut -d= -f2)
        if [ "$vendor" = "1a86" ] && [ "$product" = "7523" ]; then
            echo "✅ Found serial device: $device"
            sudo chmod 666 $device
            sudo chown root:dialout $device
        fi
    done
fi

echo "📱 Available USB devices:"
if command -v lsusb >/dev/null 2>&1; then
    lsusb | grep -E "(534d|1a86)" || echo "No Openterface devices detected"
else
    echo "lsusb not available - checking /sys/bus/usb/devices..."
    found_devices=false
    for device_dir in /sys/bus/usb/devices/*/; do
        if [ -f "${device_dir}idVendor" ] && [ -f "${device_dir}idProduct" ]; then
            vendor=$(cat "${device_dir}idVendor" 2>/dev/null || echo "")
            product=$(cat "${device_dir}idProduct" 2>/dev/null || echo "")
            if [ "$vendor" = "534d" ] || [ "$vendor" = "1a86" ]; then
                echo "Found device: VID=$vendor PID=$product"
                found_devices=true
            fi
        fi
    done
    if [ "$found_devices" = false ]; then
        echo "No Openterface devices detected"
    fi
fi

echo "🚀 Starting Openterface application..."

# Start the application - find the actual binary location
if [ -f "/usr/local/bin/openterfaceQT.AppImage" ]; then
    exec /usr/local/bin/openterfaceQT.AppImage "$@"
elif [ -f "/usr/local/bin/openterfaceQT" ]; then
    exec /usr/local/bin/openterfaceQT "$@"
elif [ -f "/usr/bin/openterfaceQT" ]; then
    exec /usr/bin/openterfaceQT "$@"
elif [ -f "/opt/openterface/bin/openterfaceQT" ]; then
    exec /opt/openterface/bin/openterfaceQT "$@"
else
    echo "Error: openterfaceQT binary not found!"
    exit 1
fi
EOF

    chmod +x /usr/local/bin/start-openterface.sh
    echo "✅ Launcher script created at /usr/local/bin/start-openterface.sh"
}

# Function to display summary
show_summary() {
    echo ""
    echo "🎉 Installation Summary"
    echo "======================"
    echo "✅ Openterface QT version: $LATEST_VERSION"
    echo "✅ Runtime dependencies: Installed"
    echo "✅ Device permissions: Configured"
    echo "✅ Launcher script: /usr/local/bin/start-openterface.sh"
    echo ""
    echo "🔧 Usage:"
    echo "   - Direct launch: Use the launcher script"
    echo "   - In Docker: CMD [\"/usr/local/bin/start-openterface.sh\"]"
    echo ""
    echo "📱 Hardware Requirements:"
    echo "   - Openterface device (VID: 534d, PID: 2109)"
    echo "   - Serial interface (VID: 1a86, PID: 7523)"
    echo ""
    echo "🚀 Ready for testing!"
}

# Main execution
main() {
    echo "Starting installation process..."
    
    get_latest_version
    download_package
    install_package
    setup_device_permissions
    verify_installation
    create_launcher
    show_summary
    
    echo "✅ Installation completed successfully!"
}

# Run main function
main "$@"
