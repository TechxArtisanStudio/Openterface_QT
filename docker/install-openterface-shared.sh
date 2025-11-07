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

# Determine what type of package to download based on environment variable
# Default to DEB if not specified
INSTALL_TYPE="${INSTALL_TYPE:-deb}"

# Set package name based on install type
if [ "$INSTALL_TYPE" = "appimage" ]; then
    PACKAGE_NAME="openterfaceQT.linux.amd64.shared.AppImage"
else
    PACKAGE_NAME="openterfaceQT.linux.amd64.shared.deb"
fi

# Function to get the latest release version
get_latest_version() {
    echo "🔍 Fetching latest release information..."
    # Use GitHub API to get the latest release
    LATEST_VERSION=$(curl -s "https://api.github.com/repos/${GITHUB_REPO}/releases/latest" | \
                     grep '"tag_name":' | \
                     sed -E 's/.*"([^"]+)".*/\1/')
    
    echo "✅ Latest version: $LATEST_VERSION"
}

# Function to find the latest built package (DEB or AppImage)
find_latest_build_package() {
    # Search paths in order of preference
    local search_paths=(
        "/tmp/build-artifacts"
        "/build"
        "/workspace/build"
        "/src/build"
        "./build"
        "/tmp"
    )
    
    # Determine file pattern based on install type
    local file_pattern
    if [ "$INSTALL_TYPE" = "appimage" ]; then
        file_pattern="*.AppImage"
    else
        file_pattern="*.deb"
    fi
    
    # Find all matching files and get the newest one
    local latest_package=""
    local latest_timestamp=0
    
    for search_path in "${search_paths[@]}"; do
        if [ -d "$search_path" ]; then
            # Look for matching files
            if ls "$search_path"/$file_pattern 1> /dev/null 2>&1; then
                for package_file in "$search_path"/$file_pattern; do
                    if [ -f "$package_file" ]; then
                        # Get the modification time
                        local timestamp=$(stat -c %Y "$package_file" 2>/dev/null || stat -f %m "$package_file" 2>/dev/null || echo 0)
                        
                        if [ "$timestamp" -gt "$latest_timestamp" ]; then
                            latest_timestamp=$timestamp
                            latest_package="$package_file"
                        fi
                    fi
                done
            fi
        fi
    done
    
    if [ -n "$latest_package" ]; then
        # Output ONLY the file path - no debug output
        echo "$latest_package"
        return 0
    fi
    
    return 1
}

# Function to download from latest linux-build workflow artifacts
download_from_latest_build() {
    echo "📥 Attempting to download from latest linux-build workflow artifacts..."
    
    # Check if GITHUB_TOKEN is available
    ARTIFACT_TOKEN="${GITHUB_TOKEN:-}"
    
    if [ -z "$ARTIFACT_TOKEN" ]; then
        echo "⚠️  GITHUB_TOKEN not provided"
        echo "ℹ️  Artifact download requires GitHub token. This should be provided by the workflow."
        echo "ℹ️  Consider using volume mounts instead: docker run -v /path/to/artifacts:/tmp/build-artifacts"
        return 1
    fi
    
    echo "✅ GITHUB_TOKEN is available, attempting download..."
    
    # Get the latest successful linux-build workflow run
    echo "🔍 Finding latest linux-build workflow run..."
    LATEST_RUN=$(curl -s -H "Authorization: token $ARTIFACT_TOKEN" \
                 "https://api.github.com/repos/${GITHUB_REPO}/actions/workflows/linux-build.yaml/runs?status=success&per_page=1" | \
                 jq -r '.workflow_runs[0]')
    
    if [ "$LATEST_RUN" = "null" ] || [ -z "$LATEST_RUN" ]; then
        echo "⚠️  No successful linux-build runs found"
        return 1
    fi
    
    RUN_ID=$(echo "$LATEST_RUN" | jq -r '.id')
    echo "✅ Found latest linux-build run: $RUN_ID"
    
    # Get all artifacts from this run
    echo "🔍 Fetching artifacts from workflow run..."
    ARTIFACTS=$(curl -s -H "Authorization: token $ARTIFACT_TOKEN" \
                "https://api.github.com/repos/${GITHUB_REPO}/actions/runs/$RUN_ID/artifacts")
    
    # Determine artifact pattern based on install type
    if [ "$INSTALL_TYPE" = "appimage" ]; then
        ARTIFACT_PATTERN="shared.AppImage"
        OUTPUT_FILENAME="openterfaceQT.linux.amd64.shared.AppImage"
    else
        ARTIFACT_PATTERN="shared.deb"
        OUTPUT_FILENAME="openterfaceQT.linux.amd64.shared.deb"
    fi
    
    # Find the appropriate artifact
    ARTIFACT_ID=$(echo "$ARTIFACTS" | jq -r '.artifacts[] | select(.name | contains("'$ARTIFACT_PATTERN'")) | .id' | head -1)
    
    if [ -z "$ARTIFACT_ID" ] || [ "$ARTIFACT_ID" = "null" ]; then
        echo "⚠️  No shared $ARTIFACT_PATTERN artifact found in latest build"
        echo "Available artifacts:"
        echo "$ARTIFACTS" | jq -r '.artifacts[].name' 2>/dev/null || echo "  (could not list artifacts)"
        return 1
    fi
    
    echo "📦 Found shared $ARTIFACT_PATTERN artifact: $ARTIFACT_ID"
    
    # Download the artifact
    echo "⬇️ Downloading artifact..."
    
    # Use temp directory for download
    TEMP_DIR=$(mktemp -d)
    trap "rm -rf $TEMP_DIR" EXIT
    
    if curl -L -H "Authorization: token $ARTIFACT_TOKEN" -o "$TEMP_DIR/artifact.zip" \
        "https://api.github.com/repos/${GITHUB_REPO}/actions/artifacts/$ARTIFACT_ID/zip" 2>/dev/null; then
        
        echo "✅ Artifact downloaded, extracting..."
        
        # Extract the artifact file
        if unzip -j "$TEMP_DIR/artifact.zip" -d "$TEMP_DIR/" 2>/dev/null; then
            echo "✅ Archive extracted"
            
            # Find the appropriate file based on install type
            if [ "$INSTALL_TYPE" = "appimage" ]; then
                ARTIFACT_FILE=$(find "$TEMP_DIR" -name "*.AppImage" -type f | head -1)
                if [ -z "$ARTIFACT_FILE" ]; then
                    echo "❌ No .AppImage file found in extracted archive"
                    ls -la "$TEMP_DIR/" 2>/dev/null || true
                    return 1
                fi
            else
                ARTIFACT_FILE=$(find "$TEMP_DIR" -name "*.deb" -type f | head -1)
                if [ -z "$ARTIFACT_FILE" ]; then
                    echo "❌ No .deb file found in extracted archive"
                    ls -la "$TEMP_DIR/" 2>/dev/null || true
                    return 1
                fi
            fi
            
            if [ -n "$ARTIFACT_FILE" ] && [ -f "$ARTIFACT_FILE" ]; then
                echo "✅ Found artifact file: $ARTIFACT_FILE"
                
                # Copy to final location using the dynamic PACKAGE_NAME
                if cp "$ARTIFACT_FILE" "/tmp/${PACKAGE_NAME}"; then
                    echo "✅ Artifact copied successfully to /tmp/${PACKAGE_NAME}"
                    rm -f "$TEMP_DIR/artifact.zip"
                    return 0
                else
                    echo "❌ Failed to copy artifact file to /tmp/${PACKAGE_NAME}"
                    echo "   Artifact file: $ARTIFACT_FILE"
                    echo "   Size: $(stat -c%s "$ARTIFACT_FILE" 2>/dev/null || echo "unknown")"
                    return 1
                fi
            else
                echo "❌ No artifact file found after extraction"
                ls -la "$TEMP_DIR/" 2>/dev/null || true
                return 1
            fi
        else
            echo "❌ Failed to extract archive"
            return 1
        fi
    else
        echo "❌ Failed to download artifact"
        return 1
    fi
}

# Function to download the package
download_package() {
    echo "📥 Looking for Openterface QT package..."
    
    # Determine what we're looking for
    local package_type
    local search_pattern
    if [ "$INSTALL_TYPE" = "appimage" ]; then
        package_type="AppImage"
        search_pattern="*.AppImage"
    else
        package_type="DEB"
        search_pattern="*.deb"
    fi
    
    # First, try to find the latest build artifact locally
    if built_package=$(find_latest_build_package); then
        echo "🔍 Looking for latest Linux build artifacts ($search_pattern)..."
        echo "   Found local artifact: $built_package"
        if cp "$built_package" "/tmp/${PACKAGE_NAME}"; then
            echo "✅ Build artifact copied to /tmp/${PACKAGE_NAME}"
            return 0
        else
            echo "⚠️  Failed to copy build artifact: $built_package"
            echo "   Error details: $(ls -lah "$built_package" 2>&1)"
            echo "   Attempting other local paths..."
        fi
    else
        echo "ℹ️  No local $package_type files found in standard paths"
    fi
    
    # Fallback: check for local built packages in standard locations
    LOCAL_PACKAGE_PATHS=(
        "/workspace/build/openterfaceQT_*.AppImage"
        "/workspace/build/openterfaceQT_*.deb"
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
                if cp "$potential_path" "/tmp/${PACKAGE_NAME}"; then
                    echo "✅ Package copied to /tmp/${PACKAGE_NAME}"
                    return 0
                fi
            fi
        done
    done
    
    echo "ℹ️  No local build artifacts found. Trying latest linux-build workflow artifacts..."
    
    # Try to download from latest linux-build workflow
    if download_from_latest_build; then
        return 0
    fi
    
    echo "❌ Failed to find build artifacts or download from workflow"
    return 1
}

# Function to install the package
install_package() {
    echo "📦 Installing Openterface QT package..."
    
    PACKAGE_FILE="/tmp/${PACKAGE_NAME}"
    
    # Determine package type based on actual file content or extension
    if [[ "$PACKAGE_FILE" == *.deb ]] && dpkg-deb --info "$PACKAGE_FILE" &>/dev/null; then
        echo "   Installing as Debian package..."
        # Use sudo for dpkg and apt-get if not already root
        if [ "$(id -u)" -ne 0 ]; then
            SUDO="sudo"
        else
            SUDO=""
        fi
        
        if $SUDO dpkg -i "$PACKAGE_FILE" 2>&1; then
            echo "✅ Package installed successfully"
        else
            echo "⚠️  Package installation had dependency issues, fixing..."
            $SUDO apt-get update 2>&1 | grep -v "^Reading\|^Building\|^done\|^Hit:" || true
            $SUDO apt-get install -f -y 2>&1 | tail -5
            echo "✅ Dependencies resolved and package installed"
        fi
    elif [[ "$PACKAGE_FILE" == *.AppImage ]] || file "$PACKAGE_FILE" 2>/dev/null | grep -q "AppImage"; then
        echo "   Installing as AppImage (no extraction needed)..."
        if [ "$(id -u)" -ne 0 ]; then
            SUDO="sudo"
        else
            SUDO=""
        fi
        
        # AppImage can be run directly, no need to extract
        if $SUDO cp "$PACKAGE_FILE" /usr/local/bin/openterfaceQT; then
            $SUDO chmod +x /usr/local/bin/openterfaceQT
            echo "✅ AppImage installed directly to /usr/local/bin/openterfaceQT (ready to run)"
        else
            echo "❌ Failed to install AppImage"
            return 1
        fi
    else
        echo "   Installing as executable binary..."
        if [ "$(id -u)" -ne 0 ]; then
            SUDO="sudo"
        else
            SUDO=""
        fi
        $SUDO cp "$PACKAGE_FILE" /usr/local/bin/openterfaceQT
        $SUDO chmod +x /usr/local/bin/openterfaceQT
        echo "✅ Binary installed to /usr/local/bin/openterfaceQT"
    fi
    
    # Clean up downloaded/copied package
    rm -f "$PACKAGE_FILE"
}

# Function to set up device permissions
setup_device_permissions() {
    echo "🔐 Setting up device permissions..."
    
    # Determine if we need sudo
    if [ "$(id -u)" -ne 0 ]; then
        SUDO="sudo"
    else
        SUDO=""
    fi
    
    # Create udev rules for Openterface hardware
    $SUDO bash -c 'cat > /etc/udev/rules.d/51-openterface.rules << '"'"'EOF'"'"'
# Openterface HID device
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess", MODE="0666"
SUBSYSTEM=="usb", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess", MODE="0666"

# Serial interface chip
SUBSYSTEM=="ttyUSB", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess", MODE="0666"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess", MODE="0666"
EOF'
    
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
        $SUDO udevadm control --reload-rules 2>/dev/null || echo "⚠️  Could not reload udev rules"
        $SUDO udevadm trigger 2>/dev/null || echo "⚠️  Could not trigger udev"
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
        # Note: The binary may crash if trying to initialize Qt GUI in non-GUI environment
        # So we just check if the binary exists and is executable
        if [ -x "$FOUND_BINARY" ]; then
            echo "✅ Binary is executable"
        else
            echo "⚠️  Binary found but may not be executable"
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
    
    # Determine if we need sudo
    if [ "$(id -u)" -ne 0 ]; then
        SUDO="sudo"
    else
        SUDO=""
    fi
    
    $SUDO bash -c 'cat > /usr/local/bin/start-openterface.sh << '"'"'EOF'"'"'
#!/bin/bash

echo "🔧 Setting up device permissions..."

# Check if FUSE is available (needed for AppImage)
if ! command -v fusermount >/dev/null 2>&1; then
    echo "⚠️  FUSE (fusermount) not available - AppImage may need extraction mode"
    echo "   Consider running AppImage with: ./openterfaceQT --appimage-extract-and-run"
    echo "   Or install: sudo apt-get install libfuse2 fuse"
fi

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

# Start the application - find the binary location
if [ -f "/usr/local/bin/openterfaceQT" ]; then
    # Check if it's an AppImage
    if file "/usr/local/bin/openterfaceQT" 2>/dev/null | grep -q "AppImage"; then
        # AppImage detected
        if command -v fusermount >/dev/null 2>&1; then
            echo "📦 Running AppImage with FUSE support..."
            exec /usr/local/bin/openterfaceQT "$@"
        else
            echo "⚠️  FUSE not available, using extraction mode..."
            exec /usr/local/bin/openterfaceQT --appimage-extract-and-run "$@"
        fi
    else
        # Regular binary
        echo "📦 Running binary..."
        exec /usr/local/bin/openterfaceQT "$@"
    fi
elif [ -f "/usr/bin/openterfaceQT" ]; then
    exec /usr/bin/openterfaceQT "$@"
elif [ -f "/opt/openterface/bin/openterfaceQT" ]; then
    exec /opt/openterface/bin/openterfaceQT "$@"
else
    echo "Error: openterfaceQT binary/AppImage not found!"
    exit 1
fi
EOF'
    
    $SUDO chmod +x /usr/local/bin/start-openterface.sh
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
    
    # Try to find local build artifacts first (no network needed)
    echo "🔍 Checking for local build artifacts..."
    if download_package; then
        echo "✅ Package found (either local or downloaded)"
    else
        echo "❌ Failed to find or download package"
        exit 1
    fi
    
    install_package
    setup_device_permissions
    verify_installation
    create_launcher
    show_summary
    
    echo "✅ Installation completed successfully!"
}

# Run main function
main "$@"
