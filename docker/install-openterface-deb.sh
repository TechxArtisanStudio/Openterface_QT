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

# Function to install runtime dependencies (especially GStreamer)
install_runtime_dependencies() {
    echo "📦 Installing runtime dependencies (especially GStreamer)..."
    
    if [ "$(id -u)" -ne 0 ]; then
        SUDO="sudo"
    else
        SUDO=""
    fi
    
    # Update package lists
    echo "   🔄 Updating package lists..."
    if ! $SUDO apt-get update -qq 2>&1 | tail -1; then
        echo "   ⚠️  apt-get update had issues, continuing..."
    fi
    
    # Install critical GStreamer runtime dependencies
    echo "   📚 Installing GStreamer and multimedia libraries..."
    GSTREAMER_PACKAGES=(
        "libgstreamer1.0-0"
        "libgstreamer-plugins-base1.0-0"
        "gstreamer1.0-plugins-base"
        "gstreamer1.0-plugins-good"
        "gstreamer1.0-plugins-bad"
        "gstreamer1.0-plugins-ugly"
        "gstreamer1.0-libav"
        "gstreamer1.0-alsa"
        "gstreamer1.0-pulseaudio"
    )
    
    local installed_count=0
    for pkg in "${GSTREAMER_PACKAGES[@]}"; do
        if ! dpkg -l 2>/dev/null | grep -q "^ii.*$pkg"; then
            echo "      Installing $pkg..."
            if $SUDO apt-get install -y -qq "$pkg" 2>&1 | grep -q "Unable to locate" 2>/dev/null; then
                echo "      ⚠️  $pkg not available in repositories"
            else
                echo "      ✅ $pkg installed"
                ((installed_count++))
            fi
        else
            echo "      ✅ $pkg already installed"
        fi
    done
    
    echo "✅ Runtime dependencies installation completed (${installed_count} packages installed)"
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
        
        # CRITICAL: Update system library cache after successful DEB installation
        # This allows ldconfig and the dynamic linker to find the bundled libraries in /usr/lib
        echo ""
        echo "🔄 Updating system library cache (ldconfig)..."
        echo "   This step is CRITICAL for bundled libraries to be discoverable"
        if $SUDO ldconfig 2>&1 | head -3; then
            echo "   ✅ Library cache updated successfully"
        else
            echo "   ⚠️  Note: ldconfig may not be available in this environment"
        fi
        echo ""
    else
        echo "⚠️  Package installation had dependency issues (exit code: $DPKG_EXIT)"
        echo "📋 Full dpkg output:"
        echo "$DPKG_OUTPUT"
        echo ""
        echo "🔧 Attempting to fix dependencies..."
        
        # Try to update apt cache first
        echo "   📦 Updating package lists..."
        if APT_UPDATE=$($SUDO apt-get update 2>&1); then
            echo "   ✅ Package lists updated"
        else
            echo "   ⚠️  apt-get update had issues, continuing..."
        fi
        
        # Try to install dependencies
        echo "   📦 Installing missing dependencies..."
        APT_FIX_OUTPUT=$($SUDO apt-get install -f -y 2>&1)
        APT_FIX_EXIT=$?
        
        echo "   📋 Installation output:"
        echo "$APT_FIX_OUTPUT" | tail -20 | sed 's/^/      /'
        
        if [ $APT_FIX_EXIT -ne 0 ]; then
            echo "   ❌ Failed to automatically fix dependencies (exit code: $APT_FIX_EXIT)"
            echo "   📋 Full apt-get install -f output:"
            echo "$APT_FIX_OUTPUT" | sed 's/^/      /'
            echo ""
            echo "   🔍 Analyzing missing packages..."
            
            # Extract package names from error messages
            echo "$APT_FIX_OUTPUT" | grep -i "unmet\|depends\|unable" | sed 's/^/      /'
            
            echo ""
            echo "   💡 Diagnostic Information:"
            echo "      Installed packages related to multimedia:"
            $SUDO apt-cache search libgstreamer | grep "^libgstreamer" | sed 's/^/      /'
            
            echo ""
            echo "   ⚠️  Please check the errors above and ensure system has multimedia libraries"
            echo "   💡 Try installing: $SUDO apt-get install -y gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good"
            return 1
        fi
        
        echo "   ✅ Dependencies resolved"
        echo ""
        echo "   📦 Retrying package installation..."
        
        # Retry dpkg install
        DPKG_RETRY=$($SUDO dpkg -i "$PACKAGE_FILE" 2>&1)
        DPKG_RETRY_EXIT=$?
        
        if [ $DPKG_RETRY_EXIT -ne 0 ]; then
            echo "   ❌ Package installation still failed after dependency fix (exit code: $DPKG_RETRY_EXIT)"
            echo "   📋 dpkg retry output:"
            echo "$DPKG_RETRY" | sed 's/^/      /'
            return 1
        fi
        
        echo "   ✅ Package installed successfully after fixing dependencies"
    fi
    
    echo "📋 DEB Installation Log: Verifying package contents..."
    
    # CRITICAL: Update system library cache after DEB installation
    # This allows ldconfig and the dynamic linker to find the bundled libraries in /usr/lib
    echo ""
    echo "🔄 Updating system library cache (ldconfig)..."
    echo "   This step is CRITICAL for bundled libraries to be discoverable"
    if $SUDO ldconfig 2>&1 | head -5; then
        echo "   ✅ Library cache updated successfully"
    else
        echo "   ⚠️  Note: ldconfig may not be available in this environment"
    fi
    echo ""
    
    echo "🔍 Installed files from openterfaceqt package:"
    $SUDO dpkg -L openterfaceqt 2>/dev/null | head -30
    
    echo ""
    echo "📋 Checking for bundled libraries in package:"
    
    # Check for JPEG libraries in package
    echo "   🔍 JPEG libraries:"
    if $SUDO dpkg -L openterfaceqt 2>/dev/null | grep -i "libjpeg\|libturbojpeg"; then
        echo "   ✅ JPEG libraries found in package"
    else
        echo "   ℹ️  JPEG libraries not bundled (will use system packages)"
    fi
    
    # Check for GStreamer libraries
    echo ""
    echo "   🔍 GStreamer libraries:"
    if $SUDO dpkg -L openterfaceqt 2>/dev/null | grep -i "libgstreamer"; then
        echo "   ✅ GStreamer libraries found in package"
    else
        echo "   ℹ️  GStreamer libraries not bundled (will use system packages)"
    fi
    
    # Check for VA-API libraries
    echo ""
    echo "   🔍 VA-API libraries:"
    if $SUDO dpkg -L openterfaceqt 2>/dev/null | grep -i "libva"; then
        echo "   ✅ VA-API libraries found in package"
    else
        echo "   ℹ️  VA-API libraries not bundled (will use system packages)"
    fi
    
    # List all .so files in package
    echo ""
    echo "   📊 Total .so files in package:"
    SO_COUNT=$($SUDO dpkg -L openterfaceqt 2>/dev/null | grep "\.so" | wc -l)
    echo "      $SO_COUNT .so files found"
    
    if [ "$SO_COUNT" -gt 0 ]; then
        echo "   📋 Library files present:"
        $SUDO dpkg -L openterfaceqt 2>/dev/null | grep "\.so" | head -20 | sed 's/^/      /'
    fi
    
    echo ""
    echo "📋 Checking library locations in installed package:"
    if [ -d /usr/lib/openterfaceqt ]; then
        echo "Found /usr/lib/openterfaceqt:"
        ls -lah /usr/lib/openterfaceqt/
    fi
    if [ -d /usr/local/lib/openterfaceqt ]; then
        echo "Found /usr/local/lib/openterfaceqt:"
        ls -lah /usr/local/lib/openterfaceqt/
    fi
    if [ -d /usr/lib/qt6/plugins ]; then
        echo "Found Qt6 plugins:"
        ls -lah /usr/lib/qt6/plugins/ | head -10
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
    return 0
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
    
    # Check library dependencies
    echo ""
    echo "📋 Verifying library dependencies..."
    echo "🔍 Checking ldd output for binary:"
    echo "   NOTE: Libraries are bundled in /usr/lib via the DEB package"
    echo "   The binary's RPATH is set to /usr/lib for library discovery"
    echo ""
    
    # Use the binary's embedded RPATH - don't override with LD_LIBRARY_PATH for now
    # This tests the actual RPATH configuration
    echo "   Running ldd to check dependencies (using binary's embedded RPATH)..."
    echo ""
    
    ldd "$FOUND_BINARY" 2>&1 | tee /tmp/ldd_output.txt
    
    echo ""
    echo "📋 Analyzing missing libraries:"
    if grep "not found" /tmp/ldd_output.txt; then
        echo "❌ Found missing libraries:"
        MISSING_COUNT=$(grep "not found" /tmp/ldd_output.txt | wc -l)
        echo "   Total missing: $MISSING_COUNT"
        echo ""
        
        # Extract individual missing libraries
        echo "   Missing library names:"
        grep "not found" /tmp/ldd_output.txt | awk '{print $1}' | sort | uniq | sed 's/^/      /'
        
        echo ""
        echo "🔍 Searching for missing libraries in installed DEB package:"
        
        # Check if libraries are actually in the DEB
        for missing_lib in $(grep "not found" /tmp/ldd_output.txt | awk '{print $1}' | sort | uniq); do
            echo ""
            echo "   Checking for $missing_lib:"
            
            # Search in package
            FOUND_IN_PKG=$($SUDO dpkg -L openterfaceqt 2>/dev/null | grep "$missing_lib" | head -3)
            if [ -n "$FOUND_IN_PKG" ]; then
                echo "      ✅ FOUND IN PACKAGE:"
                echo "$FOUND_IN_PKG" | sed 's/^/         /'
                
                # Verify the files actually exist on the filesystem
                while IFS= read -r pkg_file; do
                    if [ -f "$pkg_file" ]; then
                        echo "      ✅ File exists: $pkg_file"
                        ls -lh "$pkg_file" | sed 's/^/         /'
                    else
                        echo "      ❌ File does not exist: $pkg_file"
                    fi
                done <<< "$FOUND_IN_PKG"
            else
                echo "      ❌ NOT found in package - need to add to build script"
            fi
        done
        
        echo ""
        echo "💡 DIAGNOSTICS:"
        echo "   Binary RPATH:"
        if command -v patchelf >/dev/null 2>&1; then
            patchelf --print-rpath "$FOUND_BINARY" | sed 's/^/      /'
        else
            echo "      (patchelf not available)"
        fi
        
        echo ""
        echo "   Contents of /usr/lib (library files):"
        ls -lh /usr/lib/*.so* 2>/dev/null | wc -l | xargs echo "      Total .so files:"
        ls -lh /usr/lib/*.so* 2>/dev/null | grep -E "(libturbojpeg|libva|libgstreamer|libgstvideo|libgstapp)" | sed 's/^/      /'
        
    else
        echo "✅ All libraries found!"
    fi
    
    echo ""
    echo "📋 Library path configuration:"
    echo "   LD_LIBRARY_PATH: ${LD_LIBRARY_PATH:-not set}"
    echo ""
    echo "   Contents of /usr/lib (first 15 .so files):"
    ls -la /usr/lib/*.so* 2>/dev/null | head -15 || echo "      No .so files found"
    
    echo ""
    echo "📋 Package contents verification:"
    echo "   Total files in openterfaceqt package:"
    dpkg -L openterfaceqt 2>/dev/null | wc -l
    echo ""
    echo "   .so files in package:"
    dpkg -L openterfaceqt 2>/dev/null | grep "\.so" || echo "      No .so files found in package!"
    
    echo ""
    echo "✅ Installation verification completed"
    return 0
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
export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH
exec "$BINARY_LOCATION" "$@"
EOF'
    
    $SUDO chmod +x /usr/local/bin/start-openterface.sh
    echo "✅ Launcher script created at /usr/local/bin/start-openterface.sh"
    return 0
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
    if ! download_package; then
        echo "❌ Failed to find or download package"
        echo ""
        echo "⚠️  Installation failed - possible solutions:"
        echo "  1. Provide local build artifacts via volume mount:"
        echo "     docker run -v \$(pwd)/build:/tmp/build-artifacts:ro ..."
        echo "  2. Provide a valid GITHUB_TOKEN with 'actions:read' permission:"
        echo "     docker build --build-arg GITHUB_TOKEN=\$YOUR_TOKEN ..."
        echo "  3. Ensure the latest linux-build workflow has completed successfully"
        exit 1
    fi
    echo "✅ Package found"
    
    install_runtime_dependencies
    
    if ! install_package; then
        echo "❌ Package installation failed"
        exit 1
    fi
    
    if ! setup_device_permissions; then
        echo "❌ Device permissions setup failed"
        exit 1
    fi
    
    if ! verify_installation; then
        echo "❌ Installation verification failed"
        exit 1
    fi
    
    if ! create_launcher; then
        echo "❌ Launcher creation failed"
        exit 1
    fi
    
    show_summary
    
    echo "✅ DEB installation completed successfully!"
    exit 0
}

main "$@"
