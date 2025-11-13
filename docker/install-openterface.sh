#!/bin/bash
# =============================================================================
# Openterface QT Installation Script (Unified)
# =============================================================================
#
# This script unifies installation for both DEB and AppImage packages.
# It routes to the appropriate installation method based on INSTALL_TYPE.
#
# Usage:
#   ./install-openterface.sh deb       # Install DEB package
#   ./install-openterface.sh appimage  # Install AppImage package
#   ./install-openterface.sh rpm       # Install RPM package
#   INSTALL_TYPE=deb ./install-openterface.sh  # Via environment variable
#
# OVERVIEW:
# - Supports DEB package installation with dpkg
# - Supports AppImage installation with FUSE support
# - Sets up device permissions and udev rules
# - Configures launcher scripts
#
# =============================================================================

set -e  # Exit on any error

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
GITHUB_REPO="TechxArtisanStudio/Openterface_QT"
DEB_PACKAGE_NAME="openterfaceQT.linux.amd64.shared.deb"
APPIMAGE_PACKAGE_NAME="openterfaceQT.linux.amd64.shared.AppImage"
RPM_PACKAGE_NAME="openterfaceQT.linux.amd64.shared.rpm"

# Determine installation type from argument or environment variable
INSTALL_TYPE="${1:-${INSTALL_TYPE:-}}"

# =============================================================================
# Utility Functions
# =============================================================================

print_header() {
    echo ""
    echo -e "${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║${NC}  $1"
    echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"
    echo ""
}

print_section() {
    echo ""
    echo -e "${BLUE}▶ $1${NC}"
    echo ""
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ️  $1${NC}"
}

# =============================================================================
# Validation Functions
# =============================================================================

validate_install_type() {
    if [ -z "$INSTALL_TYPE" ]; then
        print_error "INSTALL_TYPE not specified"
        echo ""
        echo "Usage: $0 <install_type>"
        echo "  install_type: 'deb', 'rpm', or 'appimage'"
        echo ""
        echo "Examples:"
        echo "  $0 deb"
        echo "  $0 rpm"
        echo "  $0 appimage"
        echo "  INSTALL_TYPE=deb $0"
        echo ""
        return 1
    fi
    
    case "$INSTALL_TYPE" in
        deb|appimage|rpm)
            print_success "Installation type: $INSTALL_TYPE"
            return 0
            ;;
        *)
            print_error "Invalid INSTALL_TYPE: $INSTALL_TYPE"
            echo "Supported types: deb, appimage, rpm"
            return 1
            ;;
    esac
}

# =============================================================================
# Common Package Download Functions
# =============================================================================

find_latest_package() {
    local search_paths=(
        "/tmp/build-artifacts"
        "/build"
        "/workspace/build"
        "/src/build"
        "./build"
        "/tmp"
    )
    
    local extension="$1"  # .deb or .AppImage
    local latest_package=""
    local latest_timestamp=0
    
    for search_path in "${search_paths[@]}"; do
        if [ -d "$search_path" ]; then
            if ls "$search_path"/*"$extension" 1> /dev/null 2>&1; then
                for package_file in "$search_path"/*"$extension"; do
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

download_from_latest_build() {
    local artifact_type="$1"  # "shared.deb" or "shared.AppImage"
    
    print_info "Attempting to download from latest linux-build workflow..."
    
    ARTIFACT_TOKEN="${GITHUB_TOKEN:-}"
    
    if [ -z "$ARTIFACT_TOKEN" ]; then
        print_warning "GITHUB_TOKEN not provided - skipping workflow download"
        return 1
    fi
    
    print_success "GITHUB_TOKEN available, attempting download..."
    
    print_info "Finding latest linux-build workflow run..."
    LATEST_RUN=$(curl -s -H "Authorization: token $ARTIFACT_TOKEN" \
                 "https://api.github.com/repos/${GITHUB_REPO}/actions/workflows/linux-build.yaml/runs?status=success&per_page=1" | \
                 jq -r '.workflow_runs[0]')
    
    if [ "$LATEST_RUN" = "null" ] || [ -z "$LATEST_RUN" ]; then
        print_warning "No successful linux-build runs found"
        return 1
    fi
    
    RUN_ID=$(echo "$LATEST_RUN" | jq -r '.id')
    print_success "Found latest linux-build run: $RUN_ID"
    
    print_info "Fetching artifacts from workflow run..."
    ARTIFACTS=$(curl -s -H "Authorization: token $ARTIFACT_TOKEN" \
                "https://api.github.com/repos/${GITHUB_REPO}/actions/runs/$RUN_ID/artifacts")
    
    ARTIFACT_ID=$(echo "$ARTIFACTS" | jq -r ".artifacts[] | select(.name | contains(\"$artifact_type\")) | .id" | head -1)
    
    if [ -z "$ARTIFACT_ID" ] || [ "$ARTIFACT_ID" = "null" ]; then
        print_warning "No $artifact_type artifact found in latest build"
        return 1
    fi
    
    print_success "Found artifact: $ARTIFACT_ID"
    print_info "Downloading artifact..."
    
    TEMP_DIR=$(mktemp -d)
    trap "rm -rf $TEMP_DIR" EXIT
    
    if curl -L -H "Authorization: token $ARTIFACT_TOKEN" -o "$TEMP_DIR/artifact.zip" \
        "https://api.github.com/repos/${GITHUB_REPO}/actions/artifacts/$ARTIFACT_ID/zip" 2>/dev/null; then
        
        print_success "Artifact downloaded, extracting..."
        
        # Check if unzip is available
        if ! command -v unzip >/dev/null 2>&1; then
            print_warning "unzip not found, trying to install..."
            apt-get update -qq && apt-get install -y unzip >/dev/null 2>&1 || \
            pacman -Sy unzip --noconfirm >/dev/null 2>&1 || \
            dnf install -y unzip >/dev/null 2>&1 || true
        fi
        
        # Extract with error handling
        EXTRACT_OUTPUT=$(unzip -j "$TEMP_DIR/artifact.zip" -d "$TEMP_DIR/" 2>&1)
        EXTRACT_EXIT=$?
        
        if [ $EXTRACT_EXIT -eq 0 ]; then
            print_success "Archive extracted successfully"
        else
            print_warning "unzip exit code: $EXTRACT_EXIT"
            return 1
        fi
        
        ARTIFACT_FILE=$(find "$TEMP_DIR" -type f \( -name "*.deb" -o -name "*.AppImage" \) 2>/dev/null | head -1)
        if [ -z "$ARTIFACT_FILE" ]; then
            print_error "No package file found in extracted archive"
            return 1
        fi
        
        if [ -n "$ARTIFACT_FILE" ] && [ -f "$ARTIFACT_FILE" ]; then
            print_success "Found package file: $ARTIFACT_FILE"
            
            # Determine output filename
            if [[ "$ARTIFACT_FILE" == *.deb ]]; then
                OUTPUT_FILE="/tmp/${DEB_PACKAGE_NAME}"
            else
                OUTPUT_FILE="/tmp/${APPIMAGE_PACKAGE_NAME}"
            fi
            
            if cp "$ARTIFACT_FILE" "$OUTPUT_FILE"; then
                print_success "Package copied to $OUTPUT_FILE"
                echo "$OUTPUT_FILE"
                return 0
            else
                print_error "Failed to copy package file"
                return 1
            fi
        fi
    else
        print_error "Failed to download artifact"
        return 1
    fi
}

# =============================================================================
# DEB Installation Functions
# =============================================================================

check_and_install_missing_deps() {
    local package_file="$1"
    local sudo_cmd="$2"
    
    print_section "Attempting to install package and checking for missing dependencies..."
    
    # Try dpkg install without -f flag first to see what's missing
    DPKG_OUTPUT=$($sudo_cmd dpkg -i "$package_file" 2>&1)
    DPKG_EXIT=$?
    
    if [ $DPKG_EXIT -eq 0 ]; then
        # No errors, dependencies are satisfied
        print_success "Package dependencies are satisfied"
        return 0
    fi
    
    # Package installation failed, extract missing dependencies from dpkg output
    print_warning "Package has unmet dependencies"
    print_info "dpkg output:"
    echo "$DPKG_OUTPUT"
    echo ""
    
    # Extract missing package names from dpkg/preinst error output
    # Look for "Missing packages: pkg1 pkg2 pkg3 ..." format (from preinst script)
    MISSING_PACKAGES=$(echo "$DPKG_OUTPUT" | grep "^Missing packages:" | sed 's/^Missing packages: //')
    
    if [ -z "$MISSING_PACKAGES" ]; then
        # Try alternative format: "Run the following command BEFORE installing..."
        # followed by packages list in apt-get install command
        MISSING_PACKAGES=$(echo "$DPKG_OUTPUT" | grep -A 1 "apt-get install -y" | tail -1 | sed 's/.*apt-get install -y //' | sed 's/^[ \t]*//')
    fi
    
    if [ -z "$MISSING_PACKAGES" ]; then
        # Try dpkg's standard "Depends: xxx, yyy, zzz" format
        MISSING_PACKAGES=$(echo "$DPKG_OUTPUT" | grep -oP "(?<=Depends: ).*|(?<=depends on ).*" | tr ',' '\n' | sed 's/^ *//;s/ *$//' | grep -v '|' | awk '{print $1}' | sort -u | tr '\n' ' ')
    fi
    
    if [ -z "$MISSING_PACKAGES" ]; then
        print_warning "Could not automatically detect missing packages from dpkg output"
        print_info "Attempting to fix with apt-get install -f..."
        
        # Let apt-get try to resolve dependencies
        print_section "Installing missing dependencies..."
        print_info "Running: apt-get update && apt-get install -f -y"
        echo ""
        
        if $sudo_cmd apt-get update -qq 2>&1 | grep -v "^Get:\|^Hit:\|^Reading" || true; then
            INSTALL_OUTPUT=$($sudo_cmd apt-get install -f -y 2>&1)
            INSTALL_EXIT=$?
            
            echo "$INSTALL_OUTPUT"
            echo ""
            
            if [ $INSTALL_EXIT -eq 0 ]; then
                print_success "Missing dependencies installed successfully"
                return 0
            else
                print_warning "apt-get install -f had issues, but continuing..."
                return 0
            fi
        else
            print_warning "apt-get update failed, but continuing..."
            return 0
        fi
    fi
    
    # Install detected missing packages
    print_info "Missing packages detected: $MISSING_PACKAGES"
    echo ""
    
    print_section "Installing missing dependencies..."
    print_info "Running: apt-get update && apt-get install -y $MISSING_PACKAGES"
    echo ""
    
    if $sudo_cmd apt-get update -qq 2>&1 | grep -v "^Get:\|^Hit:\|^Reading" || true; then
        print_info "Running apt-get install..."
        INSTALL_OUTPUT=$($sudo_cmd apt-get install -y $MISSING_PACKAGES 2>&1)
        INSTALL_EXIT=$?
        
        echo "$INSTALL_OUTPUT"
        echo ""
        
        if [ $INSTALL_EXIT -eq 0 ]; then
            print_success "Missing dependencies installed successfully"
            echo ""
            return 0
        else
            print_warning "Dependency installation had issues, but continuing..."
            echo ""
            return 0
        fi
    else
        print_warning "apt-get update failed, but continuing..."
        return 0
    fi
}

install_deb_package() {
    print_header "DEB Package Installation"
    
    PACKAGE_FILE="/tmp/${DEB_PACKAGE_NAME}"
    
    # Find or download package
    if ! download_deb_package; then
        print_error "Failed to find or download DEB package"
        return 1
    fi
    
    # Verify package file
    if [ ! -f "$PACKAGE_FILE" ]; then
        print_error "DEB package file not found: $PACKAGE_FILE"
        return 1
    fi
    
    print_info "Package file found:"
    ls -lh "$PACKAGE_FILE"
    
    # Verify it's a valid DEB package
    if ! dpkg-deb --info "$PACKAGE_FILE" &>/dev/null; then
        print_error "File is not a valid DEB package"
        return 1
    fi
    
    print_success "Valid DEB package detected"
    
    # Get sudo if needed
    if [ "$(id -u)" -ne 0 ]; then
        SUDO="sudo"
    else
        SUDO=""
    fi
    
    # Attempt installation and handle missing dependencies
    echo ""
    print_section "Attempting package installation..."
    
    if check_and_install_missing_deps "$PACKAGE_FILE" "$SUDO"; then
        # Dependencies were handled, now do the actual installation
        print_section "Installing package with dpkg..."
        DPKG_OUTPUT=$($SUDO dpkg -i "$PACKAGE_FILE" 2>&1)
        DPKG_EXIT=$?
        
        if [ $DPKG_EXIT -eq 0 ]; then
            print_success "Package installed successfully"
        else
            print_warning "Package installation had issues (exit code: $DPKG_EXIT)"
            echo "$DPKG_OUTPUT"
            print_info "Attempting to fix remaining dependencies (silent mode)..."
            if APT_FIX=$($SUDO apt-get install -f -y -qq 2>&1); then
                print_success "Dependencies resolved"
            else
                print_error "Failed to fix dependencies"
                print_info "Error output:"
                echo "$APT_FIX"
                return 1
            fi
        fi
    else
        print_warning "Dependency check had issues, but continuing..."
    fi
    
    # Update library cache
    print_section "Updating system library cache..."
    if $SUDO ldconfig 2>&1 | head -3; then
        print_success "Library cache updated successfully"
    else
        print_warning "ldconfig may not be available in this environment"
    fi
    
    # Clean up
    rm -f "$PACKAGE_FILE"
    
    print_success "DEB installation completed"
    return 0
}

download_deb_package() {
    print_section "Looking for DEB package..."
    
    # Try to find locally first
    if built_package=$(find_latest_package ".deb"); then
        print_info "Found local DEB artifact: $built_package"
        
        if [ -f "$built_package" ] && [ -r "$built_package" ]; then
            if cp "$built_package" "/tmp/${DEB_PACKAGE_NAME}"; then
                print_success "Build artifact copied to /tmp/${DEB_PACKAGE_NAME}"
                return 0
            fi
        fi
    fi
    
    print_info "No local DEB found, trying workflow download..."
    
    if download_from_latest_build "shared.deb"; then
        return 0
    fi
    
    print_error "Failed to find or download DEB package"
    return 1
}


# =============================================================================
# AppImage Installation Functions
# =============================================================================

install_appimage_package() {
    print_header "AppImage Package Installation"
    
    PACKAGE_FILE="/tmp/${APPIMAGE_PACKAGE_NAME}"
    
    # Find or download package
    if ! download_appimage_package; then
        print_error "Failed to find or download AppImage package"
        return 1
    fi
    
    # Verify package file
    if [ ! -f "$PACKAGE_FILE" ]; then
        print_error "AppImage package file not found: $PACKAGE_FILE"
        return 1
    fi
    
    print_info "Package file found:"
    ls -lh "$PACKAGE_FILE"
    
    # Check FUSE support
    check_fuse_support || true
    
    # Get sudo if needed
    if [ "$(id -u)" -ne 0 ]; then
        SUDO="sudo"
    else
        SUDO=""
    fi
    
    # Copy to /usr/local/bin
    print_section "Copying AppImage to /usr/local/bin..."
    if $SUDO cp "$PACKAGE_FILE" /usr/local/bin/openterfaceQT 2>&1; then
        print_success "AppImage copied successfully"
        
        # Make executable
        if $SUDO chmod +x /usr/local/bin/openterfaceQT 2>&1; then
            print_success "AppImage made executable"
        else
            print_error "Failed to make AppImage executable"
            return 1
        fi
    else
        print_error "Failed to copy AppImage to /usr/local/bin"
        return 1
    fi
    
    # Clean up
    rm -f "$PACKAGE_FILE"
    
    print_success "AppImage installation completed"
    return 0
}

download_appimage_package() {
    print_section "Looking for AppImage package..."
    
    # Try to find locally first
    if built_package=$(find_latest_package ".AppImage"); then
        print_info "Found local AppImage artifact: $built_package"
        
        if [ -f "$built_package" ] && [ -r "$built_package" ]; then
            if cp "$built_package" "/tmp/${APPIMAGE_PACKAGE_NAME}"; then
                print_success "Build artifact copied to /tmp/${APPIMAGE_PACKAGE_NAME}"
                return 0
            fi
        fi
    fi
    
    print_info "No local AppImage found, trying workflow download..."
    
    if download_from_latest_build "shared.AppImage"; then
        return 0
    fi
    
    print_error "Failed to find or download AppImage package"
    return 1
}

check_fuse_support() {
    print_info "Checking FUSE support..."
    
    if command -v fusermount >/dev/null 2>&1; then
        print_success "FUSE (fusermount) is available"
        # Store FUSE availability for launcher script
        echo "FUSE_AVAILABLE=true" > /tmp/openterface-config.sh
        return 0
    else
        print_warning "FUSE (fusermount) not available"
        print_info "AppImage will run in extraction mode"
        print_info "For optimal performance, install: sudo apt-get install libfuse2 fuse"
        # Store FUSE unavailability for launcher script
        echo "FUSE_AVAILABLE=false" > /tmp/openterface-config.sh
        return 1
    fi
}

# =============================================================================
# RPM Installation Functions
# =============================================================================

install_rpm_package() {
    print_header "RPM Package Installation"
    
    PACKAGE_FILE="/tmp/${RPM_PACKAGE_NAME}"
    
    # Find or download package
    if ! download_rpm_package; then
        print_error "Failed to find or download RPM package"
        return 1
    fi
    
    # Verify package file
    if [ ! -f "$PACKAGE_FILE" ]; then
        print_error "RPM package file not found: $PACKAGE_FILE"
        return 1
    fi
    
    print_info "Package file found:"
    ls -lh "$PACKAGE_FILE"
    
    # Get sudo if needed
    if [ "$(id -u)" -ne 0 ]; then
        SUDO="sudo"
    else
        SUDO=""
    fi
    
    # Attempt installation
    echo ""
    print_section "Installing RPM package..."
    
    if $SUDO dnf install -y "$PACKAGE_FILE" 2>&1 | tail -20; then
        print_success "RPM package installed successfully"
    else
        # Try again with --skip-broken to handle missing dependencies
        print_warning "First attempt failed, trying with --skip-broken..."
        if $SUDO dnf install -y --skip-broken "$PACKAGE_FILE" 2>&1 | tail -20; then
            print_success "RPM package installed with --skip-broken"
        else
            print_error "Failed to install RPM package even with --skip-broken"
            return 1
        fi
    fi
    
    # Update library cache
    print_section "Updating system library cache..."
    if $SUDO ldconfig 2>&1 | head -3; then
        print_success "Library cache updated successfully"
    else
        print_warning "ldconfig may not be available in this environment"
    fi
    
    # Clean up
    rm -f "$PACKAGE_FILE"
    
    print_success "RPM installation completed"
    return 0
}

download_rpm_package() {
    print_section "Looking for RPM package..."
    
    # Try to find locally first
    if built_package=$(find_latest_package ".rpm"); then
        print_info "Found local RPM artifact: $built_package"
        
        if [ -f "$built_package" ] && [ -r "$built_package" ]; then
            if cp "$built_package" "/tmp/${RPM_PACKAGE_NAME}"; then
                print_success "Build artifact copied to /tmp/${RPM_PACKAGE_NAME}"
                return 0
            fi
        fi
    fi
    
    print_info "No local RPM found, trying workflow download..."
    
    if download_from_latest_build "shared.rpm"; then
        return 0
    fi
    
    print_error "Failed to find or download RPM package"
    return 1
}

# =============================================================================
# Common Setup Functions
# =============================================================================

setup_device_permissions() {
    print_section "Setting up device permissions..."
    
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
        print_info "Container environment detected - udev rules created, will be applied at runtime"
    elif systemctl is-active --quiet systemd-udevd 2>/dev/null || pgrep -x "systemd-udevd\|udevd" >/dev/null 2>&1; then
        print_section "Reloading udev rules..."
        $SUDO udevadm control --reload-rules 2>/dev/null || print_warning "Could not reload udev rules"
        $SUDO udevadm trigger 2>/dev/null || print_warning "Could not trigger udev"
    else
        print_info "udev not running - rules will be applied when udev starts"
    fi
    
    print_success "Device permissions configured"
    return 0
}

verify_installation() {
    print_section "Verifying installation..."
    
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
        print_success "Openterface QT found at: $FOUND_BINARY"
        
        if [ -x "$FOUND_BINARY" ]; then
            print_success "Binary is executable"
        else
            print_warning "Binary found but not executable"
        fi
        
        return 0
    else
        print_error "Openterface QT binary not found"
        return 1
    fi
}

create_launcher() {
    print_section "Creating launcher script..."
    
    if [ "$(id -u)" -ne 0 ]; then
        SUDO="sudo"
    else
        SUDO=""
    fi
    
    $SUDO bash -c 'cat > /usr/local/bin/start-openterface.sh << '"'"'LAUNCHER_EOF'"'"'
#!/bin/bash

echo "🔧 Starting Openterface..."

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

# Find and execute binary or AppImage
BINARY_LOCATION=""
for loc in /usr/local/bin/openterfaceQT /usr/bin/openterfaceQT /opt/openterface/bin/openterfaceQT; do
    if [ -f "$loc" ] && [ -x "$loc" ]; then
        BINARY_LOCATION="$loc"
        break
    fi
done

if [ -z "$BINARY_LOCATION" ]; then
    echo "Error: openterfaceQT not found!"
    exit 1
fi

echo "🚀 Running from $BINARY_LOCATION..."

# Check if it is an AppImage and handle accordingly
if file "$BINARY_LOCATION" 2>/dev/null | grep -q "AppImage"; then
    echo "📦 Detected AppImage format"
    
    # Check FUSE support
    if command -v fusermount >/dev/null 2>&1; then
        echo "✅ FUSE is available - running AppImage directly"
        export QT_QPA_PLATFORM=xcb
        export QT_X11_NO_MITSHM=1
        exec "$BINARY_LOCATION" "$@"
    else
        echo "⚠️  FUSE not available - using extraction mode"
        echo "   (Install libfuse2 and fuse for better performance)"
        export QT_QPA_PLATFORM=xcb
        export QT_X11_NO_MITSHM=1
        exec "$BINARY_LOCATION" --appimage-extract-and-run "$@"
    fi
else
    # Regular binary (e.g., from DEB package)
    export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH
    export QT_QPA_PLATFORM=xcb
    export QT_X11_NO_MITSHM=1
    exec "$BINARY_LOCATION" "$@"
fi
LAUNCHER_EOF'
    
    $SUDO chmod +x /usr/local/bin/start-openterface.sh
    print_success "Launcher script created at /usr/local/bin/start-openterface.sh"
    return 0
}

# =============================================================================
# Main Installation Logic
# =============================================================================

main() {
    print_header "Openterface QT Installation"
    
    # Validate installation type
    if ! validate_install_type; then
        return 1
    fi
    
    # Run installation based on type
    case "$INSTALL_TYPE" in
        deb)
            if ! install_deb_package; then
                print_error "DEB installation failed"
                return 1
            fi
            ;;
        rpm)
            if ! install_rpm_package; then
                print_error "RPM installation failed"
                return 1
            fi
            ;;
        appimage)
            if ! install_appimage_package; then
                print_error "AppImage installation failed"
                return 1
            fi
            ;;
    esac
    
    # Common setup steps
    if ! setup_device_permissions; then
        print_warning "Device permissions setup had issues"
    fi
    
    if ! verify_installation; then
        print_error "Installation verification failed"
        return 1
    fi
    
    if ! create_launcher; then
        print_warning "Launcher creation failed"
    fi
    
    # Show summary
    print_header "Installation Complete ✅"
    echo "Package Type: $(echo $INSTALL_TYPE | tr '[:lower:]' '[:upper:]')"
    echo "Binary Location: /usr/local/bin/openterfaceQT"
    echo "Launcher Script: /usr/local/bin/start-openterface.sh"
    echo ""
    print_success "Openterface QT is ready for testing!"
    
    return 0
}

# Run main
main "$@"
exit $?
