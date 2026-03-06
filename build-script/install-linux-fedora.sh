#!/bin/bash
# =============================================================================
# Openterface QT Linux Installation Script (Fedora Edition)
# =============================================================================
#
# Modified for Fedora Linux systems
# Original script by TechxArtisan Studio
#
# QUICK INSTALLATION:
# Run this command to download and execute the script directly:
# curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/refs/heads/main/build-script/install-linux-fedora.sh | bash
#
# By default, the script builds the LATEST STABLE VERSION (currently v0.5.17)
#
# To install a specific version/tag instead:
# BUILD_VERSION="v0.3.19" bash <(curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/refs/heads/main/build-script/install-linux-fedora.sh)
#
# To use the latest development version:
# BUILD_VERSION="main" bash <(curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/refs/heads/main/build-script/install-linux-fedora.sh)
#
# =============================================================================

set -e

# Configuration - Version/Tag to build
BUILD_VERSION="${BUILD_VERSION:-}"

# Check for existing system installation
echo "🔍 Checking for existing openterfaceQT installation..."
EXISTING_FOUND=false
EXISTING_LOCATIONS=()

if [ -f "/usr/bin/openterfaceQT" ]; then
    EXISTING_FOUND=true
    EXISTING_LOCATIONS+=("/usr/bin/openterfaceQT")
fi

if [ -f "/usr/local/bin/openterfaceQT" ]; then
    EXISTING_FOUND=true
    EXISTING_LOCATIONS+=("/usr/local/bin/openterfaceQT")
fi

if command -v openterfaceQT &> /dev/null; then
    COMMAND_PATH=$(which openterfaceQT 2>/dev/null || true)
    if [ -n "$COMMAND_PATH" ] && [[ ! " ${EXISTING_LOCATIONS[@]} " =~ " ${COMMAND_PATH} " ]]; then
        EXISTING_FOUND=true
        EXISTING_LOCATIONS+=("$COMMAND_PATH")
    fi
fi

if [ "$EXISTING_FOUND" = true ]; then
    echo "⚠️  Found existing openterfaceQT installation(s):"
    for location in "${EXISTING_LOCATIONS[@]}"; do
        echo "  - $location"
    done
    echo ""
    echo "❌ This may cause conflicts with the new build."
    echo ""
    
    if [[ -t 0 && -t 1 ]]; then
        echo "⚠️  IMPORTANT: Removing existing installations will permanently delete:"
        echo "   - Binary files and executables"
        echo "   - Desktop entries and application shortcuts"
        echo "   - Application icons and data directories"
        echo "   - Package manager installations (if any)"
        echo ""
        read -p "Do you want to remove existing installation(s)? [y/N]: " -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            REMOVE_EXISTING=true
        else
            REMOVE_EXISTING=false
        fi
    else
        echo "🤖 Non-interactive mode detected"
        echo "   For security, existing installations will NOT be removed automatically."
        REMOVE_EXISTING=false
    fi
    
    if [ "$REMOVE_EXISTING" = true ]; then
        echo "🗑️  Removing existing installations..."
        
        for location in "${EXISTING_LOCATIONS[@]}"; do
            if [ -f "$location" ]; then
                echo "  Removing: $location"
                sudo rm -f "$location"
            fi
        done
        
        if [ -f "/usr/share/applications/openterfaceQT.desktop" ]; then
            echo "  Removing desktop file: /usr/share/applications/openterfaceQT.desktop"
            sudo rm -f "/usr/share/applications/openterfaceQT.desktop"
        fi
        
        if [ -d "/usr/share/openterfaceQT" ]; then
            echo "  Removing data directory: /usr/share/openterfaceQT"
            sudo rm -rf "/usr/share/openterfaceQT"
        fi
        
        if [ -f "/usr/share/pixmaps/openterfaceQT.png" ]; then
            echo "  Removing icon: /usr/share/pixmaps/openterfaceQT.png"
            sudo rm -f "/usr/share/pixmaps/openterfaceQT.png"
        fi
        
        echo "✅ Existing installations removed successfully"
    else
        echo "⚠️  Keeping existing installation(s)..."
    fi
else
    echo "✅ No existing system installation found"
fi

echo "🔧 Installing dependencies for Fedora..."
sudo dnf update -y

echo "📦 Installing build dependencies..."
sudo dnf install -y --skip-unavailable \
    gcc \
    gcc-c++ \
    make \
    cmake \
    qt6-qtbase-devel \
    qt6-qtmultimedia-devel \
    qt6-qtserialport-devel \
    qt6-qtsvg-devel \
    libusb1-devel \
    qt6-linguist \
    systemd-devel \
    pkg-config \
    libX11-devel \
    libXrandr-devel \
    libXrender-devel \
    expat-devel \
    freetype-devel \
    fontconfig-devel \
    bzip2-devel \
    ffmpeg-devel \
    ffmpeg

echo "👥 Setting up user permissions..."
sudo usermod -a -G dialout,video $USER
# uucp group may not exist on all systems
getent group uucp >/dev/null 2>&1 && sudo usermod -a -G uucp $USER || echo "  ℹ️  uucp group not found, skipping..."

echo "🔐 Setting up device permissions..."
echo 'SUBSYSTEM=="hidraw", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/51-openterface.rules
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules
echo 'SUBSYSTEM=="ttyUSB", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules
sudo udevadm control --reload-rules
sudo udevadm trigger

echo "📥 Cloning repository..."
if [ ! -d "Openterface_QT" ]; then
    echo "  Cloning Openterface_QT repository..."
    git clone https://github.com/TechxArtisanStudio/Openterface_QT.git
fi

cd Openterface_QT

# Determine which version to build
TARGET_VERSION=""

if [ -n "$BUILD_VERSION" ]; then
    TARGET_VERSION="$BUILD_VERSION"
    echo "🏷️  Using explicitly specified version: $TARGET_VERSION"
else
    # Use the latest tag by default for most recent stable version
    echo "🔍 Fetching latest tags..."
    git fetch --tags --quiet
    LATEST_TAG=$(git tag -l --sort=-version:refname | grep -E '^v?[0-9]' | head -1)
    if [ -n "$LATEST_TAG" ]; then
        TARGET_VERSION="$LATEST_TAG"
        echo "🏷️  Using latest tagged version: $TARGET_VERSION"
    elif [ -f "resources/version.h" ]; then
        STABLE_VERSION=$(grep '#define STABLE_APP_VERSION' resources/version.h | sed 's/.*"\(.*\)".*/\1/')
        if [ -n "$STABLE_VERSION" ]; then
            TARGET_VERSION="$STABLE_VERSION"
            echo "🏷️  Using stable version from version.h: $TARGET_VERSION"
        else
            echo "⚠️  Could not extract version, using latest main branch"
        fi
    else
        echo "⚠️  version.h not found, using latest main branch"
    fi
fi

if [ -n "$TARGET_VERSION" ]; then
    echo "📦 Checking out version: $TARGET_VERSION"
    git fetch --all --tags
    
    if git rev-parse --verify "refs/tags/$TARGET_VERSION" >/dev/null 2>&1; then
        echo "  Found tag: $TARGET_VERSION"
        git checkout "tags/$TARGET_VERSION"
    elif git rev-parse --verify "refs/remotes/origin/$TARGET_VERSION" >/dev/null 2>&1; then
        echo "  Found branch: $TARGET_VERSION"
        git checkout "$TARGET_VERSION"
    elif git rev-parse --verify "$TARGET_VERSION" >/dev/null 2>&1; then
        echo "  Found commit: $TARGET_VERSION"
        git checkout "$TARGET_VERSION"
    else
        echo "  ❌ Version '$TARGET_VERSION' not found!"
        echo "     Falling back to current branch..."
    fi
    
    CURRENT_COMMIT=$(git rev-parse HEAD)
    CURRENT_TAG=$(git describe --tags --exact-match 2>/dev/null || echo "")
    if [ -n "$CURRENT_TAG" ]; then
        echo "  ✅ Building from tag: $CURRENT_TAG (commit: ${CURRENT_COMMIT:0:8})"
    else
        echo "  ✅ Building from commit: ${CURRENT_COMMIT:0:8}"
    fi
else
    echo "  📄 Using latest version from current branch"
    CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
    if [ "$CURRENT_BRANCH" != "HEAD" ]; then
        echo "  Updating branch: $CURRENT_BRANCH"
        git pull origin "$CURRENT_BRANCH" || echo "  ⚠️  Could not update branch, using current state"
    fi
    
    CURRENT_COMMIT=$(git rev-parse HEAD)
    echo "  ✅ Building from commit: ${CURRENT_COMMIT:0:8}"
fi

echo "🌐 Generating language files..."
if [ -x "/usr/lib64/qt6/bin/lrelease" ]; then
    /usr/lib64/qt6/bin/lrelease openterfaceQT.pro
    echo "✅ Language files generated successfully"
else
    echo "⚠️  lrelease not found at /usr/lib64/qt6/bin/lrelease, skipping..."
fi

echo "🏗️ Building project with CMake..."
mkdir -p build
cd build

# Detect architecture and set Qt6 cmake path
ARCH=$(uname -m)
echo "Detected architecture: $ARCH"

if [ "$ARCH" = "aarch64" ]; then
    QT_CMAKE_PATH="/usr/lib64/cmake/Qt6"
    echo "Building for ARM64/aarch64 architecture"
else
    QT_CMAKE_PATH="/usr/lib64/cmake/Qt6"
    echo "Building for x86_64 architecture"
fi

echo "Using Qt6 cmake from: $QT_CMAKE_PATH"

# Fedora uses shared FFmpeg libraries, not static
echo "🔍 Using system FFmpeg shared libraries..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_CMAKE_PATH" \
    -DCMAKE_SYSTEM_PROCESSOR="$ARCH" \
    -DOPENTERFACE_BUILD_STATIC=OFF \
    -DUSE_SHARED_FFMPEG=ON

make clean

CPU_COUNT=$(nproc)
if [ "$CPU_COUNT" -gt 1 ]; then
    MAKE_JOBS=$((CPU_COUNT - 1))
else
    MAKE_JOBS=1
fi

echo "🔨 Compiling with $MAKE_JOBS jobs..."
make -j$MAKE_JOBS

sudo make install

# Fix Qt plugin path issues for system-wide installation
echo "🔧 Configuring Qt environment for system installation..."

QT_PLUGIN_PATH="/usr/lib64/qt6/plugins"

if [ -d "$QT_PLUGIN_PATH" ]; then
    echo "  Found Qt6 plugins at: $QT_PLUGIN_PATH"
    
    WRAPPER_SCRIPT="/usr/local/bin/openterfaceQT"
    ACTUAL_BINARY="/usr/local/bin/openterfaceQT-bin"
    
    if [ -f "$WRAPPER_SCRIPT" ]; then
        echo "  Creating Qt environment wrapper..."
        sudo mv "$WRAPPER_SCRIPT" "$ACTUAL_BINARY"
        
        sudo tee "$WRAPPER_SCRIPT" > /dev/null << EOF
#!/bin/bash
# Openterface QT Wrapper Script
# Sets proper Qt environment variables for system-wide installation

export QT_PLUGIN_PATH="$QT_PLUGIN_PATH"
export QT_QPA_PLATFORM_PLUGIN_PATH="$QT_PLUGIN_PATH/platforms"
export QT_QPA_PLATFORM="xcb"

export LD_LIBRARY_PATH="/usr/lib64:\$LD_LIBRARY_PATH"

exec "$ACTUAL_BINARY" "\$@"
EOF
        
        sudo chmod +x "$WRAPPER_SCRIPT"
        echo "  ✅ Qt environment wrapper created"
    else
        echo "  ⚠️  Binary not found at expected location: $WRAPPER_SCRIPT"
    fi
else
    echo "  ⚠️  Could not find Qt6 plugins directory"
fi

DESKTOP_LAUNCHER="/usr/local/bin/openterfaceQT-desktop"
sudo tee "$DESKTOP_LAUNCHER" > /dev/null << EOF
#!/bin/bash
# Openterface QT Desktop Launcher

cd "\$HOME"
export QT_PLUGIN_PATH="$QT_PLUGIN_PATH"
export QT_QPA_PLATFORM_PLUGIN_PATH="$QT_PLUGIN_PATH/platforms"
export QT_QPA_PLATFORM="xcb"
export LD_LIBRARY_PATH="/usr/lib64:\$LD_LIBRARY_PATH"

exec /usr/local/bin/openterfaceQT "\$@"
EOF

sudo chmod +x "$DESKTOP_LAUNCHER"

echo "🔍 Checking built binary architecture..."
if [ -f "openterfaceQT" ]; then
    file openterfaceQT
    echo "Binary architecture verification:"
    readelf -h openterfaceQT | grep -E "(Class|Machine)"
else
    echo "❌ Binary not found after build"
fi

echo "✅ Build complete!"

echo "📋 Application Version Information:"
if [ -f "../resources/version.h" ]; then
    APP_VERSION=$(grep '#define APP_VERSION' ../resources/version.h | sed 's/.*"\(.*\)".*/\1/')
    if [ -n "$APP_VERSION" ]; then
        echo "   Openterface QT version: $APP_VERSION"
    else
        echo "   ⚠️  Could not extract version from version.h"
    fi
else
    echo "   ⚠️  version.h file not found"
fi

echo "🖥️ Creating desktop integration..."
DESKTOP_FILE="/usr/share/applications/openterfaceQT.desktop"
ICON_FILE="/usr/share/pixmaps/openterfaceQT.png"
BINARY_PATH="/usr/local/bin/openterfaceQT-desktop"

if [ -f "../images/icon_256.png" ]; then
    echo "  Installing application icon..."
    sudo cp "../images/icon_256.png" "$ICON_FILE"
elif [ -f "../images/icon_128.png" ]; then
    echo "  Installing application icon (128px)..."
    sudo cp "../images/icon_128.png" "$ICON_FILE"
elif [ -f "../images/icon_64.png" ]; then
    echo "  Installing application icon (64px)..."
    sudo cp "../images/icon_64.png" "$ICON_FILE"
else
    echo "  ⚠️  No application icon found"
    ICON_FILE="applications-system"
fi

echo "  Creating desktop entry..."

DESKTOP_VERSION="1.0"
if [ -f "../resources/version.h" ]; then
    EXTRACTED_VERSION=$(grep '#define APP_VERSION' ../resources/version.h | sed 's/.*"\(.*\)".*/\1/')
    if [ -n "$EXTRACTED_VERSION" ]; then
        DESKTOP_VERSION="$EXTRACTED_VERSION"
    fi
fi

sudo tee "$DESKTOP_FILE" > /dev/null << EOF
[Desktop Entry]
Version=$DESKTOP_VERSION
Type=Application
Name=Openterface QT
Comment=KVM over USB for seamless computer control (v$DESKTOP_VERSION)
GenericName=KVM over USB
Exec=$BINARY_PATH
Icon=$ICON_FILE
Terminal=false
Categories=Utility;Accessory;
Keywords=KVM;USB;remote;control;openterface;server;management;hardware;accessory;
StartupNotify=true
StartupWMClass=openterfaceQT
EOF

sudo chmod 644 "$DESKTOP_FILE"

echo "  Refreshing desktop database..."
if command -v update-desktop-database &> /dev/null; then
    sudo update-desktop-database /usr/local/share/applications/ 2>/dev/null || true
    sudo update-desktop-database /usr/share/applications/ 2>/dev/null || true
fi

echo "  Refreshing icon cache..."
if command -v gtk-update-icon-cache &> /dev/null; then
    sudo gtk-update-icon-cache -f /usr/local/share/icons/hicolor 2>/dev/null || true
    sudo gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true
fi

# Also update mime database if available
if command -v update-mime-database &> /dev/null; then
    sudo update-mime-database /usr/local/share/mime 2>/dev/null || true
fi

echo "✅ Desktop integration completed!"
echo ""
echo "🔍 System Information:"
if [ -n "$APP_VERSION" ]; then
    echo "Application version: $APP_VERSION"
fi
echo "Current system architecture: $(uname -m)"
echo "Built binary location: $(pwd)/openterfaceQT"
echo "System binary location: /usr/local/bin/openterfaceQT"
echo "Desktop launcher: /usr/local/bin/openterfaceQT-desktop"
echo ""
echo "🚀 How to run the application:"
echo "  Method 1 (Desktop): Search for 'Openterface QT' in your application menu"
echo "  Method 2 (Terminal): openterfaceQT"
echo "  Method 3 (Build dir): cd $(pwd) && ./openterfaceQT"
echo ""
echo "⚠️  You may need to reboot for permission changes to take effect."
