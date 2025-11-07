#!/bin/bash
# =============================================================================
# Openterface QT DEB Installation Script
# =============================================================================
#
# This script downloads and installs the Openterface QT DEB package
# with all necessary runtime dependencies for testing purposes.
#
# OVERVIEW:
# - Downloads the latest DEB release package from GitHub
# - Installs the package using dpkg
# - Sets up proper device permissions and udev rules
# - Configures the application for immediate testing
#
# =============================================================================

set -e  # Exit on any error

echo "🚀 Openterface QT DEB Installation Script"
echo "=========================================="

# Configuration
GITHUB_REPO="TechxArtisanStudio/Openterface_QT"
PACKAGE_NAME="openterfaceQT.linux.amd64.shared.deb"

# Function to get the latest release version
get_latest_version() {
    echo "🔍 Fetching latest release information..."
    LATEST_VERSION=$(curl -s "https://api.github.com/repos/${GITHUB_REPO}/releases/latest" | \
                     grep '"tag_name":' | \
                     sed -E 's/.*"([^"]+)".*/\1/')
    
    echo "✅ Latest version: $LATEST_VERSION"
}

# Function to find the latest built DEB package
find_latest_deb_package() {
    local search_paths=(
        "/tmp/build-artifacts"
        "/build"
        "/workspace/build"
        "/src/build"
        "./build"
        "/tmp"
    )
    
    local latest_package=""
    local latest_timestamp=0
    
    for search_path in "${search_paths[@]}"; do
        if [ -d "$search_path" ]; then
            if ls "$search_path"/*.deb 1> /dev/null 2>&1; then
                for package_file in "$search_path"/*.deb; do
                    if [ -f "$package_file" ]; then
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
        echo "$latest_package"
        return 0
    fi
    
    return 1
}

# Function to download from latest linux-build workflow artifacts
download_from_latest_build() {
    echo "📥 Attempting to download DEB from latest linux-build workflow..."
    
    ARTIFACT_TOKEN="${GITHUB_TOKEN:-}"
    
    if [ -z "$ARTIFACT_TOKEN" ]; then
        echo "⚠️  GITHUB_TOKEN not provided - skipping workflow download"
        return 1
    fi
    
    echo "✅ GITHUB_TOKEN available, attempting download..."
    
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
    
    echo "🔍 Fetching artifacts from workflow run..."
    ARTIFACTS=$(curl -s -H "Authorization: token $ARTIFACT_TOKEN" \
                "https://api.github.com/repos/${GITHUB_REPO}/actions/runs/$RUN_ID/artifacts")
    
    ARTIFACT_ID=$(echo "$ARTIFACTS" | jq -r '.artifacts[] | select(.name | contains("shared.deb")) | .id' | head -1)
    
    if [ -z "$ARTIFACT_ID" ] || [ "$ARTIFACT_ID" = "null" ]; then
        echo "⚠️  No shared DEB artifact found in latest build"
        return 1
    fi
    
    echo "📦 Found DEB artifact: $ARTIFACT_ID"
    echo "⬇️ Downloading artifact..."
    
    TEMP_DIR=$(mktemp -d)
    trap "rm -rf $TEMP_DIR" EXIT
    
    if curl -L -H "Authorization: token $ARTIFACT_TOKEN" -o "$TEMP_DIR/artifact.zip" \
        "https://api.github.com/repos/${GITHUB_REPO}/actions/artifacts/$ARTIFACT_ID/zip" 2>/dev/null; then
        
        echo "✅ Artifact downloaded, extracting..."
        
        # Check if unzip is available
        if ! command -v unzip >/dev/null 2>&1; then
            echo "⚠️  unzip not found, trying to install..."
            apt-get update -qq && apt-get install -y unzip >/dev/null 2>&1 || pacman -Sy unzip --noconfirm >/dev/null 2>&1 || dnf install -y unzip >/dev/null 2>&1 || true
        fi
        
        # Extract with better error handling
        EXTRACT_OUTPUT=$(unzip -j "$TEMP_DIR/artifact.zip" -d "$TEMP_DIR/" 2>&1)
        EXTRACT_EXIT=$?
        
        if [ $EXTRACT_EXIT -eq 0 ]; then
            echo "✅ Archive extracted successfully"
        else
            echo "⚠️  unzip exit code: $EXTRACT_EXIT"
            echo "Extract output: $EXTRACT_OUTPUT"
            # Try alternative extraction method
            echo "Attempting alternative extraction..."
            if cd "$TEMP_DIR" && unzip -l "$TEMP_DIR/artifact.zip" | head -5; then
                echo "Archive contents detected, retrying extraction..."
                unzip -j -o "$TEMP_DIR/artifact.zip" -d "$TEMP_DIR/" 2>&1 | head -20
            fi
        fi
        
        ARTIFACT_FILE=$(find "$TEMP_DIR" -name "*.deb" -type f 2>/dev/null | head -1)
        if [ -z "$ARTIFACT_FILE" ]; then
            echo "❌ No .deb file found in extracted archive"
            echo "📁 Contents of extraction directory:"
            ls -la "$TEMP_DIR/" 2>/dev/null | head -20
            return 1
        fi
        
        if [ -n "$ARTIFACT_FILE" ] && [ -f "$ARTIFACT_FILE" ]; then
            echo "✅ Found DEB file: $ARTIFACT_FILE"
            
            if cp "$ARTIFACT_FILE" "/tmp/${PACKAGE_NAME}"; then
                echo "✅ DEB copied successfully to /tmp/${PACKAGE_NAME}"
                return 0
            else
                echo "❌ Failed to copy DEB file"
                return 1
            fi
        fi
    else
        echo "❌ Failed to download artifact"
        return 1
    fi
}

# Function to download the DEB package
download_package() {
    echo "📥 Looking for Openterface QT DEB package..."
    
    # Try to find locally first
    if built_package=$(find_latest_deb_package); then
        echo "🔍 Found local DEB artifact: $built_package"
        echo "   Verifying file..."
        if [ -f "$built_package" ] && [ -r "$built_package" ]; then
            echo "   ✅ File exists and is readable, copying..."
            if cp "$built_package" "/tmp/${PACKAGE_NAME}"; then
                echo "✅ Build artifact copied to /tmp/${PACKAGE_NAME}"
                # Verify the copy
                if [ -f "/tmp/${PACKAGE_NAME}" ]; then
                    echo "✅ Destination file verified"
                    ls -lh "/tmp/${PACKAGE_NAME}"
                    return 0
                fi
            else
                echo "⚠️  Failed to copy build artifact, trying other paths..."
            fi
        else
            echo "   ❌ Source file not accessible"
        fi
    else
        echo "ℹ️  No local DEB files found in standard paths"
    fi
    
    # Fallback: check standard paths
    LOCAL_DEB_PATHS=(
        "/workspace/build/*.deb"
        "/build/*.deb"
        "/tmp/${PACKAGE_NAME}"
        "./${PACKAGE_NAME}"
    )
    
    echo "🔍 Checking standard paths for DEB packages..."
    for path_pattern in "${LOCAL_DEB_PATHS[@]}"; do
        for potential_path in $path_pattern; do
            if [ -f "$potential_path" ]; then
                echo "✅ Found local DEB: $potential_path"
                if cp "$potential_path" "/tmp/${PACKAGE_NAME}"; then
                    echo "✅ DEB copied to /tmp/${PACKAGE_NAME}"
                    return 0
                fi
            fi
        done
    done
    
    echo "ℹ️  No local DEB found, trying workflow download..."
    
    # Try to download from latest linux-build workflow
    if download_from_latest_build; then
        return 0
    fi
    
    echo "❌ Failed to find or download DEB package"
    return 1
}

# Function to install the DEB package
install_package() {
    echo "📦 Installing Openterface QT DEB package..."
    
    PACKAGE_FILE="/tmp/${PACKAGE_NAME}"
    
    # Verify package file exists
    if [ ! -f "$PACKAGE_FILE" ]; then
        echo "❌ DEB package file not found: $PACKAGE_FILE"
        echo "🔍 Checking /tmp directory:"
        ls -la /tmp/ | grep -i "\.deb\|openterface" || echo "   No DEB packages found"
        return 1
    fi
    
    echo "📋 Package file found:"
    ls -lh "$PACKAGE_FILE"
    
    # Verify it's a valid DEB package
    if ! dpkg-deb --info "$PACKAGE_FILE" &>/dev/null; then
        echo "❌ File is not a valid DEB package"
        return 1
    fi
    
    echo "✅ Valid DEB package detected"
    
    # Install using dpkg
    if [ "$(id -u)" -ne 0 ]; then
        SUDO="sudo"
    else
        SUDO=""
    fi
    
    echo "🔄 Running dpkg install..."
    DPKG_OUTPUT=$($SUDO dpkg -i "$PACKAGE_FILE" 2>&1)
    DPKG_EXIT=$?
    
    if [ $DPKG_EXIT -eq 0 ]; then
        echo "✅ Package installed successfully"
    else
        echo "⚠️  Package installation had dependency issues (exit code: $DPKG_EXIT)"
        echo "📋 dpkg output:"
        echo "$DPKG_OUTPUT" | tail -10
        echo "🔧 Attempting to fix dependencies..."
        $SUDO apt-get update 2>&1 | grep -v "^Reading\|^Building\|^done\|^Hit:" || true
        $SUDO apt-get install -f -y 2>&1 | tail -5
        echo "✅ Dependencies resolved and package installed"
    fi
    
    # After DEB installation, ensure binary is accessible
    POSSIBLE_LOCATIONS=(
        "/usr/bin/openterfaceQT"
        "/usr/local/bin/openterfaceQT"
        "/opt/openterface/bin/openterfaceQT"
    )
    
    DEB_BINARY_FOUND=""
    for loc in "${POSSIBLE_LOCATIONS[@]}"; do
        if [ -f "$loc" ] && [ -x "$loc" ]; then
            DEB_BINARY_FOUND="$loc"
            break
        fi
    done
    
    if [ -n "$DEB_BINARY_FOUND" ]; then
        # Create symlink to /usr/local/bin if not already there
        if [ "$DEB_BINARY_FOUND" != "/usr/local/bin/openterfaceQT" ]; then
            echo "🔗 Creating symlink from $DEB_BINARY_FOUND to /usr/local/bin/openterfaceQT"
            $SUDO ln -sf "$DEB_BINARY_FOUND" /usr/local/bin/openterfaceQT
        fi
        echo "✅ DEB: Binary located and accessible from /usr/local/bin/openterfaceQT"
    else
        echo "⚠️  DEB binary not found in expected locations"
        echo "🔍 Searching for binary..."
        find /usr -name "openterfaceQT" -type f 2>/dev/null | head -5 || echo "No binary found in /usr"
        find /opt -name "openterfaceQT" -type f 2>/dev/null | head -5 || echo "No binary found in /opt"
    fi
    
    # Clean up
    rm -f "$PACKAGE_FILE"
}

# Function to set up device permissions
setup_device_permissions() {
    echo "🔐 Setting up device permissions..."
    
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
    
    # Check if in container
    IN_CONTAINER=false
    if [ -f /.dockerenv ] || [ "${container:-}" = "docker" ] || [ "${container:-}" = "podman" ]; then
        IN_CONTAINER=true
    fi
    
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
    echo "🔍 Verifying DEB installation..."
    
    BINARY_LOCATIONS=(
        "/usr/local/bin/openterfaceQT"
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
        
        if [ -x "$FOUND_BINARY" ]; then
            echo "✅ Binary is executable"
        else
            echo "⚠️  Binary found but not executable"
        fi
    else
        echo "❌ Openterface QT binary not found"
        return 1
    fi
    
    # Check for desktop file
    if [ -f "/usr/share/applications/openterfaceQT.desktop" ] || [ -f "/opt/openterface/share/applications/openterfaceQT.desktop" ]; then
        echo "✅ Desktop entry found"
    else
        echo "⚠️  Desktop entry not found"
    fi
}

# Function to create launcher script
create_launcher() {
    echo "🚀 Creating launcher script..."
    
    if [ "$(id -u)" -ne 0 ]; then
        SUDO="sudo"
    else
        SUDO=""
    fi
    
    $SUDO bash -c 'cat > /usr/local/bin/start-openterface.sh << '"'"'EOF'"'"'
#!/bin/bash

echo "🔧 Setting up device permissions..."

# Start udev if not running
if ! pgrep -x "systemd-udevd" > /dev/null && ! pgrep -x "udevd" > /dev/null; then
    echo "Starting udev..."
    sudo /lib/systemd/systemd-udevd --daemon 2>/dev/null || true
    sudo udevadm control --reload-rules 2>/dev/null || true
    sudo udevadm trigger 2>/dev/null || true
fi

sleep 1

# Set USB device permissions
echo "🔌 Setting USB device permissions..."
sudo chmod 666 /dev/ttyUSB* 2>/dev/null || true
sudo chmod 666 /dev/hidraw* 2>/dev/null || true
sudo chmod 666 /dev/bus/usb/*/* 2>/dev/null || true

# Set specific Openterface device permissions
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
    echo "lsusb not available"
fi

echo "🚀 Starting Openterface application..."

# Find and execute binary
BINARY_LOCATION=""
for loc in /usr/local/bin/openterfaceQT /usr/bin/openterfaceQT /opt/openterface/bin/openterfaceQT; do
    if [ -f "$loc" ] && [ -x "$loc" ]; then
        BINARY_LOCATION="$loc"
        break
    fi
done

if [ -z "$BINARY_LOCATION" ]; then
    echo "Error: openterfaceQT binary not found!"
    exit 1
fi

echo "📦 Running binary from $BINARY_LOCATION..."
exec "$BINARY_LOCATION" "$@"
EOF'
    
    $SUDO chmod +x /usr/local/bin/start-openterface.sh
    echo "✅ Launcher script created at /usr/local/bin/start-openterface.sh"
}

# Function to display summary
show_summary() {
    echo ""
    echo "🎉 DEB Installation Summary"
    echo "==========================="
    echo "✅ Openterface QT DEB version: $LATEST_VERSION"
    echo "✅ Installation type: Debian Package (.deb)"
    echo "✅ Device permissions: Configured"
    echo "✅ Launcher script: /usr/local/bin/start-openterface.sh"
    echo ""
    echo "🚀 Ready for testing!"
}

# Main execution
main() {
    echo "Starting DEB installation process..."
    
    echo "🔍 Checking for local build artifacts..."
    if download_package; then
        echo "✅ Package found"
    else
        echo "❌ Failed to find or download package"
        exit 1
    fi
    
    install_package
    setup_device_permissions
    verify_installation
    create_launcher
    show_summary
    
    echo "✅ DEB installation completed successfully!"
}

main "$@"
