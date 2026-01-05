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

# Debug environment information
echo "🔍 Environment Debug Information:"
echo "   Date: $(date)"
echo "   User: $(whoami)"
echo "   Working directory: $(pwd)"
echo "   Available disk space: $(df -h /tmp | tail -1 | awk '{print $4}')"
echo "   Network interfaces: $(ip addr show | grep -E '^[0-9]+:' | cut -d: -f2 | tr -d ' ' | paste -sd ',' -)"
echo "   DNS servers: $(cat /etc/resolv.conf | grep nameserver | awk '{print $2}' | paste -sd ',' - || echo 'none')"
echo ""

# Configuration
GITHUB_REPO="TechxArtisanStudio/Openterface_QT"
STATIC_PACKAGE_NAME="openterfaceQT-portable"  # Assuming static portable app

# Function to get the specified version
get_latest_version() {
    echo "🔍 Fetching latest release information..."
    
    # Test GitHub API connectivity first
    echo "   Testing GitHub API connectivity..."
    if ! curl --connect-timeout 10 --max-time 30 -s "https://api.github.com" > /dev/null; then
        echo "❌ Cannot reach GitHub API"
        echo "   Network diagnostics:"
        echo "     - DNS resolution for api.github.com:"
        nslookup api.github.com || echo "       DNS resolution failed"
        echo "     - Ping test to 8.8.8.8:"
        ping -c 3 8.8.8.8 || echo "       Ping failed"
        exit 1
    fi
    echo "   ✅ GitHub API is reachable"
    
    # Use GitHub API to get the latest release with better error handling
    echo "   Querying GitHub API for latest release..."
    API_RESPONSE=$(curl -s --connect-timeout 30 --max-time 60 "https://api.github.com/repos/${GITHUB_REPO}/releases/latest")
    
    if [ $? -ne 0 ] || [ -z "$API_RESPONSE" ]; then
        echo "❌ Failed to get API response from GitHub"
        echo "   API Response: $API_RESPONSE"
        
        # Fallback to a known version
        echo "   Using fallback version: v0.4.3"
        LATEST_VERSION="v0.4.3"
    else
        LATEST_VERSION=$(echo "$API_RESPONSE" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/')
        
        if [ -z "$LATEST_VERSION" ]; then
            echo "❌ Could not parse version from API response"
            echo "   API Response snippet: $(echo "$API_RESPONSE" | head -3)"
            echo "   Using fallback version: v0.4.3"
            LATEST_VERSION="v0.4.3"
        fi
    fi
    
    echo "✅ Using version: $LATEST_VERSION"
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
    echo "   Package size check..."
    
    # First, check if the URL is accessible and get file size
    PACKAGE_SIZE=$(curl -sI "$DOWNLOAD_URL" | grep -i content-length | awk '{print $2}' | tr -d '\r' || echo "unknown")
    echo "   Package size: $PACKAGE_SIZE bytes"
    
    # Test network connectivity first
    echo "   Testing network connectivity..."
    if ! curl --connect-timeout 10 --max-time 30 -s "https://api.github.com" > /dev/null; then
        echo "❌ Network connectivity test failed - GitHub is not reachable"
        exit 1
    fi
    echo "   ✅ Network connectivity OK"
    
    # Download with retries and better timeout settings
    for i in {1..3}; do
        echo "   Attempt $i/3..."
        echo "   Using wget with extended timeout settings..."
        if wget --timeout=300 --tries=3 --retry-connrefused --progress=bar:force \
               --connect-timeout=30 --read-timeout=300 \
               "$DOWNLOAD_URL" -O "/tmp/${PACKAGE_NAME}"; then
            echo "✅ Download completed successfully"
            
            # Verify downloaded file
            if [ -f "/tmp/${PACKAGE_NAME}" ]; then
                DOWNLOADED_SIZE=$(stat -c%s "/tmp/${PACKAGE_NAME}" 2>/dev/null || echo "0")
                echo "   Downloaded file size: $DOWNLOADED_SIZE bytes"
                if [ "$DOWNLOADED_SIZE" -gt 1000000 ]; then  # At least 1MB
                    echo "   ✅ File size looks reasonable"
                    return 0
                else
                    echo "   ❌ Downloaded file seems too small, may be incomplete"
                fi
            else
                echo "   ❌ Downloaded file not found"
            fi
        else
            echo "❌ Download attempt $i failed"
            # Show more details about the failure
            echo "   Wget exit code: $?"
            
            # Try with curl as fallback
            echo "   Trying with curl as fallback..."
            if curl --connect-timeout 30 --max-time 600 -L --retry 3 --retry-delay 5 \
                   --fail --progress-bar "$DOWNLOAD_URL" -o "/tmp/${PACKAGE_NAME}"; then
                echo "✅ Download completed with curl fallback"
                return 0
            else
                echo "❌ Curl fallback also failed"
            fi
        fi
        
        if [ $i -eq 3 ]; then
            echo "❌ All download attempts failed"
            echo "   This could be due to:"
            echo "   - Slow network connection"
            echo "   - GitHub rate limiting"
            echo "   - Package not available"
            echo "   - Firewall/proxy issues"
            exit 1
        fi
        echo "   Waiting 10 seconds before retry..."
        sleep 10
    done
}

# Function to install the package
install_package() {
    echo "📦 Installing Openterface QT package..."
    
    if [ ! -f "/tmp/${PACKAGE_NAME}" ]; then
        echo "❌ Package file not found: /tmp/${PACKAGE_NAME}"
        echo "   Checking if package was provided locally..."
        
        # Check if package exists in the same directory as the script
        SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
        if [ -f "${SCRIPT_DIR}/${PACKAGE_NAME}" ]; then
            echo "   ✅ Found local package: ${SCRIPT_DIR}/${PACKAGE_NAME}"
            cp "${SCRIPT_DIR}/${PACKAGE_NAME}" "/tmp/${PACKAGE_NAME}"
        else
            echo "   ❌ No local package found either"
            echo "   Available files in script directory:"
            ls -la "${SCRIPT_DIR}/" | head -10
            exit 1
        fi
    fi
    
    echo "   Package file size: $(stat -c%s "/tmp/${PACKAGE_NAME}") bytes"
    echo "   Package file type: $(file "/tmp/${PACKAGE_NAME}")"
    
    # Determine installation method based on file type
    FILE_TYPE=$(file "/tmp/${PACKAGE_NAME}")
    
    if [[ "$FILE_TYPE" == *"executable"* ]]; then
        echo "   Installing as executable binary..."
        cp "/tmp/${PACKAGE_NAME}" /usr/bin/openterfaceQT
        chmod +x /usr/bin/openterfaceQT
        echo "   ✅ Binary installed to /usr/bin/openterfaceQT"
    elif [[ "$FILE_TYPE" == *"gzip"* ]] || [[ "$FILE_TYPE" == *"tar"* ]]; then
        echo "   Installing as archive..."
        cd /tmp
        tar -xzf "${PACKAGE_NAME}" || tar -xf "${PACKAGE_NAME}"
        
        # Find the extracted binary
        EXTRACTED_BINARY=$(find /tmp -name "openterfaceQT" -type f -executable 2>/dev/null | head -1)
        if [ -n "$EXTRACTED_BINARY" ]; then
            cp "$EXTRACTED_BINARY" /usr/bin/openterfaceQT
            chmod +x /usr/bin/openterfaceQT
            echo "   ✅ Binary extracted and installed to /usr/bin/openterfaceQT"
        else
            echo "   ❌ Could not find executable in extracted archive"
            echo "   Extracted contents:"
            find /tmp -name "*openterface*" -o -name "*QT*" | head -10
            exit 1
        fi
    else
        echo "   ❌ Unknown file type: $FILE_TYPE"
        echo "   Attempting to install as executable anyway..."
        cp "/tmp/${PACKAGE_NAME}" /usr/bin/openterfaceQT
        chmod +x /usr/bin/openterfaceQT
    fi
    
    # Clean up downloaded package
    rm -f "/tmp/${PACKAGE_NAME}"
    
    echo "   ✅ Package installation completed"
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
    
    # Reload udev rules - handle Docker container environment
    if command -v udevadm >/dev/null 2>&1; then
        # Check if we're in a Docker container or if udev service is available
        if [ -f /.dockerenv ] || [ "${DOCKER_BUILD:-}" = "1" ] || [ "${container:-}" = "docker" ]; then
            echo "🐳 Running in Docker container - udev rules created but not reloaded"
            echo "   (Rules will be active when container is run with proper device access)"
        else
            # Try to reload rules, but don't fail the installation if it doesn't work
            echo "🔄 Attempting to reload udev rules..."
            if udevadm control --reload-rules 2>/dev/null && udevadm trigger 2>/dev/null; then
                echo "✅ Udev rules reloaded successfully"
            else
                echo "⚠️  Could not reload udev rules (may require privileged mode or system restart)"
                echo "   Rules are installed and will be active after reboot or udev service restart"
            fi
        fi
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
