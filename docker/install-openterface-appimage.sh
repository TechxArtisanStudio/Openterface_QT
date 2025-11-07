#!/bin/bash
# =============================================================================
# Openterface QT AppImage Installation Script
# =============================================================================
#
# This script downloads and installs the Openterface QT AppImage package
# with all necessary runtime dependencies for testing purposes.
#
# OVERVIEW:
# - Downloads the latest AppImage release from GitHub
# - Sets up FUSE support for AppImage mounting
# - Sets up proper device permissions and udev rules
# - Configures the application for immediate testing
#
# =============================================================================

set -e  # Exit on any error

echo "🚀 Openterface QT AppImage Installation Script"
echo "=============================================="

# Configuration
GITHUB_REPO="TechxArtisanStudio/Openterface_QT"
PACKAGE_NAME="openterfaceQT.linux.amd64.shared.AppImage"

# Function to get the latest release version
get_latest_version() {
    echo "🔍 Fetching latest release information..."
    LATEST_VERSION=$(curl -s "https://api.github.com/repos/${GITHUB_REPO}/releases/latest" | \
                     grep '"tag_name":' | \
                     sed -E 's/.*"([^"]+)".*/\1/')
    
    echo "✅ Latest version: $LATEST_VERSION"
}

# Function to find the latest built AppImage package
find_latest_appimage_package() {
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
            if ls "$search_path"/*.AppImage 1> /dev/null 2>&1; then
                for package_file in "$search_path"/*.AppImage; do
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
    echo "📥 Attempting to download AppImage from latest linux-build workflow..."
    
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
    
    ARTIFACT_ID=$(echo "$ARTIFACTS" | jq -r '.artifacts[] | select(.name | contains("shared.AppImage")) | .id' | head -1)
    
    if [ -z "$ARTIFACT_ID" ] || [ "$ARTIFACT_ID" = "null" ]; then
        echo "⚠️  No shared AppImage artifact found in latest build"
        return 1
    fi
    
    echo "📦 Found AppImage artifact: $ARTIFACT_ID"
    echo "⬇️ Downloading artifact..."
    
    TEMP_DIR=$(mktemp -d)
    trap "rm -rf $TEMP_DIR" EXIT
    
    if curl -L -H "Authorization: token $ARTIFACT_TOKEN" -o "$TEMP_DIR/artifact.zip" \
        "https://api.github.com/repos/${GITHUB_REPO}/actions/artifacts/$ARTIFACT_ID/zip" 2>/dev/null; then
        
        echo "✅ Artifact downloaded, extracting..."
        
        if unzip -j "$TEMP_DIR/artifact.zip" -d "$TEMP_DIR/" 2>/dev/null; then
            echo "✅ Archive extracted"
            
            ARTIFACT_FILE=$(find "$TEMP_DIR" -name "*.AppImage" -type f | head -1)
            if [ -z "$ARTIFACT_FILE" ]; then
                echo "❌ No .AppImage file found in extracted archive"
                return 1
            fi
            
            if [ -n "$ARTIFACT_FILE" ] && [ -f "$ARTIFACT_FILE" ]; then
                echo "✅ Found AppImage file: $ARTIFACT_FILE"
                
                if cp "$ARTIFACT_FILE" "/tmp/${PACKAGE_NAME}"; then
                    echo "✅ AppImage copied successfully to /tmp/${PACKAGE_NAME}"
                    return 0
                else
                    echo "❌ Failed to copy AppImage file"
                    return 1
                fi
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

# Function to download the AppImage package
download_package() {
    echo "📥 Looking for Openterface QT AppImage package..."
    
    # Try to find locally first
    if built_package=$(find_latest_appimage_package); then
        echo "🔍 Found local AppImage artifact: $built_package"
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
        echo "ℹ️  No local AppImage files found in standard paths"
    fi
    
    # Fallback: check standard paths
    LOCAL_APPIMAGE_PATHS=(
        "/workspace/build/*.AppImage"
        "/build/*.AppImage"
        "/tmp/${PACKAGE_NAME}"
        "./${PACKAGE_NAME}"
    )
    
    echo "🔍 Checking standard paths for AppImage packages..."
    for path_pattern in "${LOCAL_APPIMAGE_PATHS[@]}"; do
        for potential_path in $path_pattern; do
            if [ -f "$potential_path" ]; then
                echo "✅ Found local AppImage: $potential_path"
                if cp "$potential_path" "/tmp/${PACKAGE_NAME}"; then
                    echo "✅ AppImage copied to /tmp/${PACKAGE_NAME}"
                    return 0
                fi
            fi
        done
    done
    
    echo "ℹ️  No local AppImage found, trying workflow download..."
    
    # Try to download from latest linux-build workflow
    if download_from_latest_build; then
        return 0
    fi
    
    echo "❌ Failed to find or download AppImage package"
    return 1
}

# Function to check FUSE availability
check_fuse_support() {
    echo "🔍 Checking FUSE support..."
    
    if command -v fusermount >/dev/null 2>&1; then
        echo "✅ FUSE (fusermount) is available"
        return 0
    else
        echo "⚠️  FUSE (fusermount) not available"
        echo "ℹ️  AppImage will need to run with --appimage-extract-and-run flag"
        echo "ℹ️  Consider installing: sudo apt-get install libfuse2 fuse"
        return 1
    fi
}

# Function to install the AppImage package
install_package() {
    echo "📦 Installing Openterface QT AppImage package..."
    
    PACKAGE_FILE="/tmp/${PACKAGE_NAME}"
    
    # Verify package file exists
    if [ ! -f "$PACKAGE_FILE" ]; then
        echo "❌ AppImage package file not found: $PACKAGE_FILE"
        echo "🔍 Checking /tmp directory:"
        ls -la /tmp/ | grep -i "\.AppImage\|openterface" || echo "   No AppImage packages found"
        return 1
    fi
    
    echo "📋 Package file found:"
    ls -lh "$PACKAGE_FILE"
    
    # Verify file type
    echo "🔍 File type detection:"
    file "$PACKAGE_FILE" 2>/dev/null || echo "   (file command failed)"
    
    if ! file "$PACKAGE_FILE" 2>/dev/null | grep -q "AppImage"; then
        echo "⚠️  File may not be a valid AppImage"
    fi
    
    # Verify it's readable
    if [ ! -r "$PACKAGE_FILE" ]; then
        echo "❌ AppImage file not readable: $PACKAGE_FILE"
        return 1
    fi
    
    echo "✅ AppImage file verified"
    
    if [ "$(id -u)" -ne 0 ]; then
        SUDO="sudo"
    else
        SUDO=""
    fi
    
    # Copy AppImage to /usr/local/bin
    echo "🔄 Copying AppImage to /usr/local/bin/openterfaceQT..."
    if $SUDO cp "$PACKAGE_FILE" /usr/local/bin/openterfaceQT 2>&1; then
        echo "✅ AppImage copied successfully"
        
        # Verify the copy
        if [ -f /usr/local/bin/openterfaceQT ]; then
            echo "✅ AppImage file exists at destination"
            ls -lh /usr/local/bin/openterfaceQT
        else
            echo "❌ AppImage file not found at destination after copy"
            return 1
        fi
        
        # Make executable
        echo "🔄 Making AppImage executable..."
        if $SUDO chmod +x /usr/local/bin/openterfaceQT 2>&1; then
            echo "✅ AppImage made executable"
        else
            echo "⚠️  Failed to make AppImage executable"
            return 1
        fi
        
        # Final verification
        if [ -x /usr/local/bin/openterfaceQT ]; then
            echo "✅ AppImage is executable"
            echo "✅ AppImage installed to /usr/local/bin/openterfaceQT"
        else
            echo "❌ AppImage is not executable"
            return 1
        fi
    else
        echo "❌ Failed to copy AppImage to /usr/local/bin/openterfaceQT"
        echo "🔍 Checking /usr/local/bin permissions..."
        ls -ld /usr/local/bin 2>/dev/null || echo "   /usr/local/bin not found"
        return 1
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
    echo "🔍 Verifying AppImage installation..."
    
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
        echo "✅ Openterface QT AppImage found at: $FOUND_BINARY"
        
        if [ -x "$FOUND_BINARY" ]; then
            echo "✅ AppImage is executable"
        else
            echo "⚠️  AppImage found but not executable"
        fi
        
        # Verify it's an AppImage
        if file "$FOUND_BINARY" 2>/dev/null | grep -q "AppImage"; then
            echo "✅ Verified as AppImage file type"
        else
            echo "⚠️  File may not be a valid AppImage"
        fi
    else
        echo "❌ Openterface QT AppImage not found"
        return 1
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

# Check if FUSE is available
FUSE_AVAILABLE=false
if command -v fusermount >/dev/null 2>&1; then
    echo "✅ FUSE (fusermount) is available"
    FUSE_AVAILABLE=true
else
    echo "⚠️  FUSE (fusermount) not available - using extraction mode"
    echo "   For better performance, install: sudo apt-get install libfuse2 fuse"
    FUSE_AVAILABLE=false
fi

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

# Find and execute AppImage
APPIMAGE_LOCATION=""
for loc in /usr/local/bin/openterfaceQT /usr/bin/openterfaceQT /opt/openterface/bin/openterfaceQT; do
    if [ -f "$loc" ] && [ -x "$loc" ]; then
        APPIMAGE_LOCATION="$loc"
        break
    fi
done

if [ -z "$APPIMAGE_LOCATION" ]; then
    echo "Error: openterfaceQT AppImage not found!"
    exit 1
fi

# Execute with appropriate flags
if [ "$FUSE_AVAILABLE" = true ]; then
    echo "📦 Running AppImage with FUSE support..."
    exec "$APPIMAGE_LOCATION" "$@"
else
    echo "📦 Running AppImage with extraction mode..."
    exec "$APPIMAGE_LOCATION" --appimage-extract-and-run "$@"
fi
EOF'
    
    $SUDO chmod +x /usr/local/bin/start-openterface.sh
    echo "✅ Launcher script created at /usr/local/bin/start-openterface.sh"
}

# Function to display summary
show_summary() {
    echo ""
    echo "🎉 AppImage Installation Summary"
    echo "================================="
    echo "✅ Openterface QT AppImage version: $LATEST_VERSION"
    echo "✅ Installation type: AppImage (self-contained)"
    echo "✅ FUSE support: $(check_fuse_support && echo 'Enabled' || echo 'Fallback mode available')"
    echo "✅ Device permissions: Configured"
    echo "✅ Launcher script: /usr/local/bin/start-openterface.sh"
    echo ""
    echo "🚀 Ready for testing!"
}

# Main execution
main() {
    echo "Starting AppImage installation process..."
    
    echo "🔍 Checking for local build artifacts..."
    if download_package; then
        echo "✅ Package found"
    else
        echo "❌ Failed to find or download package"
        exit 1
    fi
    
    check_fuse_support || true  # Warn but don't fail
    install_package
    setup_device_permissions
    verify_installation
    create_launcher
    show_summary
    
    echo "✅ AppImage installation completed successfully!"
}

main "$@"
