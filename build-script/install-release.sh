#!/bin/bash
# =============================================================================
# Openterface QT Release Installation Script
# =============================================================================
#
# One-liner installation from pre-built GitHub releases.
# Downloads and installs the latest (or specified) release version.
#
# QUICK INSTALLATION:
#   # Install latest release
#   curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-release.sh | bash
#
#   # Install specific version
#   VERSION="v0.5.17" bash <(curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-release.sh)
#
# REQUIREMENTS:
# - Linux (x86_64 or ARM64)
# - curl or wget
# - sudo privileges
#
# SUPPORTED DISTRIBUTIONS:
# - Ubuntu/Debian
# - Fedora/RHEL
# - openSUSE
# - Arch Linux
#
# AUTHOR: TechxArtisan Studio
# LICENSE: See LICENSE file in the project repository
# =============================================================================

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
VERSION="${VERSION:-}"
INSTALL_DIR="/usr/local"
BIN_DIR="${INSTALL_DIR}/bin"
APP_DIR="${INSTALL_DIR}/share/openterfaceQT"
DESKTOP_DIR="/usr/share/applications"
ICON_DIR="/usr/share/pixmaps"
REPO="TechxArtisanStudio/Openterface_QT"

# Helper functions
log_info() {
    echo -e "${BLUE}ℹ️  $1${NC}"
}

log_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

log_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

log_error() {
    echo -e "${RED}❌ $1${NC}"
}

# Detect system information
detect_system() {
    log_info "Detecting system information..."
    
    # Detect architecture
    ARCH=$(uname -m)
    case "$ARCH" in
        x86_64)
            ARCH_NAME="x86_64"
            ;;
        aarch64|arm64)
            ARCH_NAME="arm64"
            ;;
        *)
            log_error "Unsupported architecture: $ARCH"
            echo "Supported architectures: x86_64, arm64"
            exit 1
            ;;
    esac
    echo "  Architecture: $ARCH_NAME"
    
    # Detect OS
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        OS_ID="$ID"
        OS_VERSION="$VERSION_ID"
        
        case "$OS_ID" in
            ubuntu|debian|kali)
                OS_TYPE="ubuntu"
                PKG_MANAGER="apt"
                ;;
            fedora|rhel|centos)
                OS_TYPE="fedora"
                PKG_MANAGER="dnf"
                ;;
            opensuse*|suse)
                OS_TYPE="opensuse"
                PKG_MANAGER="zypper"
                ;;
            arch|manjaro)
                OS_TYPE="arch"
                PKG_MANAGER="pacman"
                ;;
            *)
                OS_TYPE="unknown"
                PKG_MANAGER="unknown"
                log_warning "Unknown distribution: $OS_ID"
                ;;
        esac
        echo "  Distribution: $OS_ID $OS_VERSION ($OS_TYPE)"
        echo "  Package Manager: $PKG_MANAGER"
    else
        log_error "Cannot detect Linux distribution"
        exit 1
    fi
}

# Get latest release version from GitHub API
get_latest_version() {
    log_info "Fetching latest release information..."
    
    LATEST_VERSION=$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/')
    
    if [ -z "$LATEST_VERSION" ]; then
        log_error "Failed to fetch latest version from GitHub"
        exit 1
    fi
    
    echo "$LATEST_VERSION"
}

# Get release asset URL
get_asset_url() {
    local version="$1"
    local arch="$2"
    local os_type="$3"
    local pkg_manager="$4"
    
    log_info "Finding release asset for $version ($arch)..."
    
    # Fetch release info
    local release_info
    if [ "$version" = "latest" ]; then
        release_info=$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest")
    else
        release_info=$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/tags/${version}")
    fi
    
    # Determine package format based on distribution
    local asset_extension=""
    case "$pkg_manager" in
        apt)
            asset_extension="\\.deb"
            ;;
        dnf|zypper)
            asset_extension="\\.rpm"
            ;;
        *)
            # Fallback to AppImage for unknown distributions
            asset_extension="\\.AppImage"
            ;;
    esac
    
    # Map architecture to release naming
    local release_arch=""
    case "$arch" in
        x86_64)
            release_arch="amd64"
            ;;
        arm64)
            release_arch="arm64"
            ;;
    esac
    
    # Extract download URL - look for the appropriate package format
    local asset_url=$(echo "$release_info" | grep -o '"browser_download_url": "[^"]*' | grep -o 'http[^"]*' | grep -E "linux.*${release_arch}.*${asset_extension}" | head -1)
    
    if [ -z "$asset_url" ]; then
        log_error "No release asset found for $version on $arch with format $asset_extension"
        echo ""
        echo "Available assets for this release:"
        echo "$release_info" | grep '"browser_download_url":' | sed 's/.*"browser_download_url": "//' | sed 's/"$//'
        exit 1
    fi
    
    echo "$asset_url"
}

# Install dependencies
install_dependencies() {
    log_info "Installing runtime dependencies..."
    
    case "$PKG_MANAGER" in
        apt)
            sudo apt-get update -y
            sudo apt-get install -y \
                libqt6core6 \
                libqt6gui6 \
                libqt6widgets6 \
                libqt6multimedia6 \
                libqt6serialport6 \
                libusb-1.0-0 \
                libudev1 \
                ffmpeg \
                || true
            ;;
        dnf)
            sudo dnf install -y \
                qt6-qtbase \
                qt6-qtmultimedia \
                qt6-qtserialport \
                libusb1 \
                systemd-udev \
                ffmpeg-libs \
                || true
            ;;
        zypper)
            sudo zypper install -y \
                libQt6Core6 \
                libQt6Gui6 \
                libQt6Widgets6 \
                libQt6Multimedia6 \
                libQt6SerialPort6 \
                libusb-1_0-0 \
                libudev1 \
                ffmpeg \
                || true
            ;;
        pacman)
            sudo pacman -S --noconfirm \
                qt6-base \
                qt6-multimedia \
                qt6-serialport \
                libusb \
                systemd-libs \
                ffmpeg \
                || true
            ;;
        *)
            log_warning "Unknown package manager, skipping dependency installation"
            log_info "If the application fails to start, you may need to install Qt6 and FFmpeg dependencies manually"
            ;;
    esac
    
    log_success "Dependencies installed"
}

# Setup permissions
setup_permissions() {
    log_info "Setting up device permissions..."
    
    # Add user to groups
    sudo usermod -a -G dialout,video $USER 2>/dev/null || true
    sudo usermod -a -G uucp $USER 2>/dev/null || true
    
    # Create udev rules
    sudo tee /etc/udev/rules.d/51-openterface.rules > /dev/null << 'EOF'
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"
SUBSYSTEM=="usb", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"
SUBSYSTEM=="ttyUSB", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"
EOF
    
    sudo udevadm control --reload-rules
    sudo udevadm trigger
    
    log_success "Permissions configured"
}

# Download and install release
download_release() {
    local url="$1"
    local pkg_manager="$2"
    local temp_dir=$(mktemp -d)
    local package_name=$(basename "$url")
    
    log_info "Downloading release: $url"
    
    # Download package
    if command -v curl &> /dev/null; then
        curl -fsSL "$url" -o "${temp_dir}/${package_name}"
    elif command -v wget &> /dev/null; then
        wget -q "$url" -O "${temp_dir}/${package_name}"
    else
        log_error "Neither curl nor wget found"
        exit 1
    fi
    
    log_success "Downloaded: ${package_name}"
    
    # Install based on package format
    log_info "Installing package..."
    
    case "$package_name" in
        *.deb)
            log_info "Installing Debian package..."
            sudo dpkg -i "${temp_dir}/${package_name}" || sudo apt-get install -f -y
            log_success "Package installed via dpkg"
            ;;
        *.rpm)
            log_info "Installing RPM package..."
            if [ "$pkg_manager" = "dnf" ]; then
                sudo dnf install -y "${temp_dir}/${package_name}"
            elif [ "$pkg_manager" = "zypper" ]; then
                sudo zypper install -y "${temp_dir}/${package_name}"
            else
                sudo rpm -ivh "${temp_dir}/${package_name}" || sudo rpm -Uvh "${temp_dir}/${package_name}"
            fi
            log_success "Package installed via RPM"
            ;;
        *.AppImage)
            log_info "Installing AppImage..."
            # Create directories
            sudo mkdir -p "${BIN_DIR}"
            sudo mkdir -p "${APP_DIR}"
            sudo mkdir -p "${DESKTOP_DIR}"
            sudo mkdir -p "${ICON_DIR}"
            
            # Install AppImage
            sudo cp "${temp_dir}/${package_name}" "${BIN_DIR}/openterfaceQT"
            sudo chmod +x "${BIN_DIR}/openterfaceQT"
            
            # Extract icon from AppImage if possible
            if command -v appimagetool &> /dev/null; then
                appimagetool --list "${temp_dir}/${package_name}" | grep -i icon | head -1 || true
            fi
            
            log_success "AppImage installed: ${BIN_DIR}/openterfaceQT"
            ;;
        *)
            log_error "Unknown package format: $package_name"
            rm -rf "${temp_dir}"
            exit 1
            ;;
    esac
    
    # Cleanup
    rm -rf "${temp_dir}"
}

# Create wrapper script
create_wrapper() {
    log_info "Creating launcher wrapper..."
    
    # Find Qt plugin path
    local qt_plugin_path=""
    if [ -d "/usr/lib/x86_64-linux-gnu/qt6/plugins" ]; then
        qt_plugin_path="/usr/lib/x86_64-linux-gnu/qt6/plugins"
    elif [ -d "/usr/lib/aarch64-linux-gnu/qt6/plugins" ]; then
        qt_plugin_path="/usr/lib/aarch64-linux-gnu/qt6/plugins"
    elif [ -d "/usr/lib64/qt6/plugins" ]; then
        qt_plugin_path="/usr/lib64/qt6/plugins"
    elif [ -d "/usr/lib/qt6/plugins" ]; then
        qt_plugin_path="/usr/lib/qt6/plugins"
    fi
    
    # Create wrapper script
    sudo tee "${BIN_DIR}/openterfaceQT" > /dev/null << EOF
#!/bin/bash
# Openterface QT Launcher
# Pre-built release installation

export QT_PLUGIN_PATH="${qt_plugin_path}"
export QT_QPA_PLATFORM_PLUGIN_PATH="${qt_plugin_path}/platforms"
export QT_QPA_PLATFORM="xcb"
export LD_LIBRARY_PATH="/usr/lib/x86_64-linux-gnu:/usr/lib/aarch64-linux-gnu:/usr/lib64:/usr/lib:\$LD_LIBRARY_PATH"

exec ${BIN_DIR}/openterfaceQT-bin "\$@"
EOF
    
    sudo chmod +x "${BIN_DIR}/openterfaceQT"
    log_success "Wrapper created: ${BIN_DIR}/openterfaceQT"
}

# Create desktop entry
create_desktop_entry() {
    log_info "Creating desktop integration..."
    
    local icon_file="${ICON_DIR}/openterfaceQT.png"
    if [ ! -f "$icon_file" ]; then
        icon_file="applications-system"
    fi
    
    sudo tee "${DESKTOP_DIR}/com.openterface.openterfaceQT.desktop" > /dev/null << EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=OpenterfaceQT
Comment=KVM over USB for seamless computer control
Exec=${BIN_DIR}/openterfaceQT
Icon=${icon_file}
Terminal=false
Categories=Utility;
Keywords=KVM;USB;remote;control;openterface;server;management;
StartupNotify=true
StartupWMClass=openterfaceQT
EOF
    
    sudo chmod 644 "${DESKTOP_DIR}/com.openterface.openterfaceQT.desktop"
    
    # Update desktop database
    if command -v update-desktop-database &> /dev/null; then
        sudo update-desktop-database "${DESKTOP_DIR}" 2>/dev/null || true
    fi
    
    # Update icon cache
    if command -v gtk-update-icon-cache &> /dev/null; then
        sudo gtk-update-icon-cache -f "${ICON_DIR}" 2>/dev/null || true
    fi
    
    log_success "Desktop entry created"
}

# Remove old installation
cleanup_old() {
    log_info "Checking for existing installations..."
    
    if [ -f "${BIN_DIR}/openterfaceQT" ]; then
        log_info "Removing old installation..."
        sudo rm -f "${BIN_DIR}/openterfaceQT"
        sudo rm -f "${BIN_DIR}/openterfaceQT-bin"
        sudo rm -f "${BIN_DIR}/openterfaceQT-desktop"
    fi
    
    if [ -f "${DESKTOP_DIR}/openterfaceQT.desktop" ]; then
        sudo rm -f "${DESKTOP_DIR}/openterfaceQT.desktop"
    fi
    
    if [ -f "${DESKTOP_DIR}/com.openterface.openterfaceQT.desktop" ]; then
        sudo rm -f "${DESKTOP_DIR}/com.openterface.openterfaceQT.desktop"
    fi
}

# Main installation
main() {
    echo ""
    echo "=================================================="
    echo "  Openterface QT Release Installer"
    echo "=================================================="
    echo ""
    
    # Detect system
    detect_system
    echo ""
    
    # Determine version
    if [ -z "$VERSION" ]; then
        VERSION=$(get_latest_version)
        log_success "Using latest release: $VERSION"
    else
        log_info "Using specified version: $VERSION"
    fi
    echo ""
    
    # Cleanup old installation
    cleanup_old
    echo ""
    
    # Get asset URL
    ASSET_URL=$(get_asset_url "$VERSION" "$ARCH_NAME" "$OS_TYPE" "$PKG_MANAGER")
    echo ""
    
    # Install dependencies
    install_dependencies
    echo ""
    
    # Download and install
    download_release "$ASSET_URL" "$PKG_MANAGER"
    echo ""
    
    # Create wrapper
    create_wrapper
    echo ""
    
    # Setup permissions
    setup_permissions
    echo ""
    
    # Create desktop entry
    create_desktop_entry
    echo ""
    
    # Final summary
    echo ""
    echo "=================================================="
    echo "  ✅ Installation Complete!"
    echo "=================================================="
    echo ""
    echo "Version: $VERSION"
    echo "Architecture: $ARCH_NAME"
    echo "Distribution: $OS_ID $OS_VERSION"
    echo ""
    echo "🚀 How to run:"
    echo "  • Terminal: openterfaceQT"
    echo "  • Desktop: Search for 'OpenterfaceQT' in applications menu"
    echo "  • Binary: ${BIN_DIR}/openterfaceQT"
    echo ""
    echo "📝 Notes:"
    echo "  • You may need to log out and back in for permission changes"
    echo "  • If mouse/keyboard don't work: sudo apt remove brltty"
    echo "  • Desktop menu may require refresh or relogin"
    echo ""
    echo "🔗 Release: https://github.com/${REPO}/releases/tag/${VERSION}"
    echo ""
}

# Run installation
main
