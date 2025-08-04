#!/bin/bash
# =============================================================================
# Openterface QT Linux Installation Script
# =============================================================================
#
# This script automates the complete installation of Openterface QT on Linux
# systems, particularly optimized for Kali Linux but compatible with other
# Debian-based distributions.
#
# QUICK INSTALLATION:
# Run this command to download and execute the script directly:
# curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/refs/heads/dev_20250804_add_oneline_buildscript/build-script/install-linux.sh | bash
#
# By default, the script will checkout and build the STABLE_APP_VERSION defined in resources/version.h
#
# To install a specific version/tag instead:
# BUILD_VERSION="v1.0.0" bash <(curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/refs/heads/dev_20250804_add_oneline_buildscript/build-script/install-linux.sh)
#
# To use the latest development version:
# BUILD_VERSION="main" bash <(curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/refs/heads/dev_20250804_add_oneline_buildscript/build-script/install-linux.sh)
#
# OVERVIEW:
# The script performs a comprehensive setup process including dependency
# installation, conflict resolution, permission configuration, source code
# compilation, and system integration.
#
# WORKFLOW:
# 1. CLEANUP PHASE:
#    - Scans for existing openterfaceQT installations in common locations
#    - Offers to remove conflicting installations (binaries, desktop files, etc.)
#    - Handles both manual installations and package manager installations
#
# 2. DEPENDENCY INSTALLATION:
#    - Updates package repositories with authentication flexibility
#    - Installs Qt6 development libraries and tools
#    - Installs FFmpeg libraries for video/audio processing
#    - Installs USB and hardware interface libraries
#    - Installs build tools (cmake, build-essential, pkg-config)
#
# 3. PERMISSION SETUP:
#    - Adds current user to dialout and uucp groups for serial port access
#    - Creates udev rules for Openterface hardware devices (USB/HID access)
#    - Configures permissions for specific vendor/product IDs:
#      * 534d:2109 (Openterface main device)
#      * 1a86:7523 (Serial interface chip)
#
# 4. SOURCE CODE ACQUISITION:
#    - Clones the official Openterface_QT repository if not present
#    - Switches to the project directory for build operations
#
# 5. LOCALIZATION:
#    - Generates Qt translation files using lrelease tool
#    - Handles missing lrelease gracefully with warnings
#
# 6. ARCHITECTURE-AWARE BUILD:
#    - Auto-detects system architecture (x86_64, ARM64/aarch64)
#    - Sets appropriate Qt6 CMake paths for each architecture
#    - Locates FFmpeg static libraries automatically
#    - Configures CMake with architecture-specific settings
#
# 7. COMPILATION & INSTALLATION:
#    - Builds project using parallel compilation (nproc-1 threads)
#    - Installs binaries system-wide using sudo make install
#    - Verifies binary architecture matches system architecture
#
# 8. POST-INSTALLATION:
#    - Provides detailed system information and build verification
#    - Offers troubleshooting guidance for common issues
#    - Explains permission requirements and potential conflicts
#
# REQUIREMENTS:
# - Debian-based Linux distribution (Ubuntu, Kali, etc.)
# - sudo privileges for system modifications
# - Internet connection for package downloads and git cloning
# - Sufficient disk space for Qt6 development tools and dependencies
#
# SUPPORTED ARCHITECTURES:
# - x86_64 (Intel/AMD 64-bit)
# - ARM64/aarch64 (ARM 64-bit, including Raspberry Pi 4+)
#
# TROUBLESHOOTING:
# - Script uses 'set -e' for fail-fast behavior on errors
# - All major operations include status reporting with emoji indicators
# - Architecture mismatches are detected and reported
# - Missing dependencies are handled gracefully where possible
#
# AUTHOR: TechxArtisan Studio
# LICENSE: See LICENSE file in the project repository
# =============================================================================

set -e

# Configuration - Version/Tag to build
# By default, use the STABLE_APP_VERSION from version.h
# Can be overridden by setting BUILD_VERSION environment variable
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

# Check if it's in PATH (but not in the locations we already checked)
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
    
    # Check if running in interactive mode
    if [[ -t 0 && -t 1 ]]; then
        # Interactive mode - prompt user
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
        # Non-interactive mode (e.g., curl | bash)
        echo "🤖 Non-interactive mode detected (e.g., curl | bash)"
        echo "   For security, existing installations will NOT be removed automatically."
        echo "   If you experience conflicts, please run the script interactively:"
        echo "   1. Download: curl -o install-linux.sh https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/refs/heads/dev_20250804_add_oneline_buildscript/build-script/install-linux.sh"
        echo "   2. Run: chmod +x install-linux.sh && ./install-linux.sh"
        echo ""
        REMOVE_EXISTING=false
    fi
    
    if [ "$REMOVE_EXISTING" = true ]; then
        echo "🗑️  Removing existing installations..."
        
        # Remove binaries
        for location in "${EXISTING_LOCATIONS[@]}"; do
            if [ -f "$location" ]; then
                echo "  Removing: $location"
                sudo rm -f "$location"
            fi
        done
        
        # Remove related files
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
        
        # Try package manager removal as well
        echo "  Checking for package manager installation..."
        if dpkg -l | grep -q openterfaceqt 2>/dev/null; then
            echo "  Removing via apt..."
            sudo apt remove -y openterfaceqt 2>/dev/null || true
            sudo apt autoremove -y 2>/dev/null || true
        fi
        
        echo "✅ Existing installations removed successfully"
    else
        echo "⚠️  Keeping existing installation(s)..."
        echo "   Note: You may experience conflicts or run the wrong version."
        echo "   Consider removing them manually if you encounter issues."
    fi
else
    echo "✅ No existing system installation found"
fi

echo "🔧 Installing dependencies for Linux..."
sudo apt-get update -y --allow-releaseinfo-change --allow-unauthenticated || true

echo "📦 Installing build dependencies..."
sudo apt-get install -y --allow-unauthenticated \
    build-essential \
    cmake \
    qt6-base-dev \
    qt6-multimedia-dev \
    qt6-serialport-dev \
    qt6-svg-dev \
    libusb-1.0-0-dev \
    qt6-tools-dev \
    libudev-dev \
    pkg-config \
    libx11-dev \
    libxrandr-dev \
    libxrender-dev \
    libexpat1-dev \
    libfreetype6-dev \
    libfontconfig1-dev \
    libbz2-dev \
    libavformat-dev \
    libavcodec-dev \
    libavutil-dev \
    libswresample-dev \
    libswscale-dev \
    ffmpeg

echo "👥 Setting up user permissions..."
sudo usermod -a -G dialout $USER
sudo usermod -a -G uucp $USER

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
    # Use explicitly specified version
    TARGET_VERSION="$BUILD_VERSION"
    echo "🏷️  Using explicitly specified version: $TARGET_VERSION"
else
    # Extract STABLE_APP_VERSION from version.h to use as default
    if [ -f "resources/version.h" ]; then
        STABLE_VERSION=$(grep '#define STABLE_APP_VERSION' resources/version.h | sed 's/.*"\(.*\)".*/\1/')
        if [ -n "$STABLE_VERSION" ]; then
            TARGET_VERSION="v$STABLE_VERSION"
            echo "🏷️  Using stable version from version.h: $TARGET_VERSION"
        else
            echo "⚠️  Could not extract STABLE_APP_VERSION from version.h"
        fi
    else
        echo "⚠️  version.h not found, using latest main branch"
    fi
fi

# Handle version/tag checkout
if [ -n "$TARGET_VERSION" ]; then
    echo "📦 Checking out version: $TARGET_VERSION"
    
    # Fetch all tags and branches to ensure we have the target
    git fetch --all --tags
    
    # Check if the version exists as a tag or branch
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
        echo "     Available tags:"
        git tag -l | head -10
        echo "     Available branches:"
        git branch -r | head -10
        echo "     Falling back to current branch..."
    fi
    
    # Show what we're building
    CURRENT_COMMIT=$(git rev-parse HEAD)
    CURRENT_TAG=$(git describe --tags --exact-match 2>/dev/null || echo "")
    if [ -n "$CURRENT_TAG" ]; then
        echo "  ✅ Building from tag: $CURRENT_TAG (commit: ${CURRENT_COMMIT:0:8})"
    else
        echo "  ✅ Building from commit: ${CURRENT_COMMIT:0:8}"
    fi
else
    echo "  📄 Using latest version from current branch"
    # Update to latest if we're on a branch
    CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
    if [ "$CURRENT_BRANCH" != "HEAD" ]; then
        echo "  Updating branch: $CURRENT_BRANCH"
        git pull origin "$CURRENT_BRANCH" || echo "  ⚠️  Could not update branch, using current state"
    fi
    
    CURRENT_COMMIT=$(git rev-parse HEAD)
    echo "  ✅ Building from commit: ${CURRENT_COMMIT:0:8}"
fi

echo "🌐 Generating language files..."
if [ -x "/usr/lib/qt6/bin/lrelease" ]; then
    /usr/lib/qt6/bin/lrelease openterfaceQT.pro
    echo "✅ Language files generated successfully"
else
    echo "⚠️  lrelease not found at /usr/lib/qt6/bin/lrelease, skipping..."
fi

echo "🏗️ Building project with CMake..."
mkdir -p build
cd build

# Detect architecture and set Qt6 cmake path
ARCH=$(dpkg --print-architecture)
UNAME_ARCH=$(uname -m)
echo "Detected architecture (dpkg): $ARCH"
echo "Detected architecture (uname): $UNAME_ARCH"

if [ "$ARCH" = "arm64" ] || [ "$ARCH" = "aarch64" ] || [ "$UNAME_ARCH" = "aarch64" ]; then
    QT_CMAKE_PATH="/usr/lib/aarch64-linux-gnu/cmake/Qt6"
    echo "Building for ARM64/aarch64 architecture"
else
    QT_CMAKE_PATH="/usr/lib/x86_64-linux-gnu/cmake/Qt6"
    echo "Building for x86_64 architecture"
fi

echo "Using Qt6 cmake from: $QT_CMAKE_PATH"

# Auto-detect FFmpeg library paths
echo "🔍 Auto-detecting FFmpeg library paths..."
LIBAVFORMAT=$(find /usr/lib -name "libavformat.a" 2>/dev/null | head -1)
LIBAVCODEC=$(find /usr/lib -name "libavcodec.a" 2>/dev/null | head -1)
LIBAVUTIL=$(find /usr/lib -name "libavutil.a" 2>/dev/null | head -1)
LIBSWRESAMPLE=$(find /usr/lib -name "libswresample.a" 2>/dev/null | head -1)
LIBSWSCALE=$(find /usr/lib -name "libswscale.a" 2>/dev/null | head -1)

# Check if all FFmpeg libraries were found
if [ -n "$LIBAVFORMAT" ] && [ -n "$LIBAVCODEC" ] && [ -n "$LIBAVUTIL" ] && [ -n "$LIBSWRESAMPLE" ] && [ -n "$LIBSWSCALE" ]; then
    echo "✅ Found FFmpeg static libraries:"
    echo "  - libavformat: $LIBAVFORMAT"
    echo "  - libavcodec: $LIBAVCODEC"
    echo "  - libavutil: $LIBAVUTIL"
    echo "  - libswresample: $LIBSWRESAMPLE"
    echo "  - libswscale: $LIBSWSCALE"
    
    FFMPEG_LIBRARIES="$LIBAVFORMAT;$LIBAVCODEC;$LIBAVUTIL;$LIBSWRESAMPLE;$LIBSWSCALE"
    
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$QT_CMAKE_PATH" \
        -DFFMPEG_LIBRARIES="$FFMPEG_LIBRARIES" \
        -DCMAKE_SYSTEM_PROCESSOR="$UNAME_ARCH"
else
    echo "⚠️  Some FFmpeg static libraries not found, using default paths"
    echo "Found libraries:"
    [ -n "$LIBAVFORMAT" ] && echo "  - libavformat: $LIBAVFORMAT" || echo "  - libavformat: NOT FOUND"
    [ -n "$LIBAVCODEC" ] && echo "  - libavcodec: $LIBAVCODEC" || echo "  - libavcodec: NOT FOUND"
    [ -n "$LIBAVUTIL" ] && echo "  - libavutil: $LIBAVUTIL" || echo "  - libavutil: NOT FOUND"
    [ -n "$LIBSWRESAMPLE" ] && echo "  - libswresample: $LIBSWRESAMPLE" || echo "  - libswresample: NOT FOUND"
    [ -n "$LIBSWSCALE" ] && echo "  - libswscale: $LIBSWSCALE" || echo "  - libswscale: NOT FOUND"
    
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$QT_CMAKE_PATH" \
        -DCMAKE_SYSTEM_PROCESSOR="$UNAME_ARCH"
fi

make -j$(( $(nproc) - 1 ))

sudo make install

# Fix Qt plugin path issues for system-wide installation
echo "🔧 Configuring Qt environment for system installation..."

# Find Qt6 plugin directory
QT_PLUGIN_PATH=""
if [ -d "/usr/lib/x86_64-linux-gnu/qt6/plugins" ]; then
    QT_PLUGIN_PATH="/usr/lib/x86_64-linux-gnu/qt6/plugins"
elif [ -d "/usr/lib/aarch64-linux-gnu/qt6/plugins" ]; then
    QT_PLUGIN_PATH="/usr/lib/aarch64-linux-gnu/qt6/plugins"
elif [ -d "/usr/lib/qt6/plugins" ]; then
    QT_PLUGIN_PATH="/usr/lib/qt6/plugins"
fi

if [ -n "$QT_PLUGIN_PATH" ]; then
    echo "  Found Qt6 plugins at: $QT_PLUGIN_PATH"
    
    # Create a wrapper script that sets the correct environment
    WRAPPER_SCRIPT="/usr/local/bin/openterfaceQT"
    ACTUAL_BINARY="/usr/local/bin/openterfaceQT-bin"
    
    # Move the actual binary
    if [ -f "$WRAPPER_SCRIPT" ]; then
        echo "  Creating Qt environment wrapper..."
        sudo mv "$WRAPPER_SCRIPT" "$ACTUAL_BINARY"
        
        # Create wrapper script with proper Qt environment
        sudo tee "$WRAPPER_SCRIPT" > /dev/null << EOF
#!/bin/bash
# Openterface QT Wrapper Script
# Sets proper Qt environment variables for system-wide installation

export QT_PLUGIN_PATH="$QT_PLUGIN_PATH"
export QT_QPA_PLATFORM_PLUGIN_PATH="$QT_PLUGIN_PATH/platforms"
export QT_QPA_PLATFORM="xcb"

# Add Qt library paths
export LD_LIBRARY_PATH="/usr/lib/x86_64-linux-gnu:/usr/lib/aarch64-linux-gnu:/usr/lib:\$LD_LIBRARY_PATH"

# Run the actual binary
exec "$ACTUAL_BINARY" "\$@"
EOF
        
        # Make wrapper executable
        sudo chmod +x "$WRAPPER_SCRIPT"
        echo "  ✅ Qt environment wrapper created"
    else
        echo "  ⚠️  Binary not found at expected location: $WRAPPER_SCRIPT"
    fi
else
    echo "  ⚠️  Could not find Qt6 plugins directory"
    echo "     You may need to set QT_PLUGIN_PATH manually"
fi

# Also create a desktop-friendly launcher script
DESKTOP_LAUNCHER="/usr/local/bin/openterfaceQT-desktop"
sudo tee "$DESKTOP_LAUNCHER" > /dev/null << EOF
#!/bin/bash
# Openterface QT Desktop Launcher
# Ensures proper environment for desktop launches

cd "\$HOME"
export QT_PLUGIN_PATH="$QT_PLUGIN_PATH"
export QT_QPA_PLATFORM_PLUGIN_PATH="$QT_PLUGIN_PATH/platforms"
export QT_QPA_PLATFORM="xcb"
export LD_LIBRARY_PATH="/usr/lib/x86_64-linux-gnu:/usr/lib/aarch64-linux-gnu:/usr/lib:\$LD_LIBRARY_PATH"

exec /usr/local/bin/openterfaceQT "\$@"
EOF

sudo chmod +x "$DESKTOP_LAUNCHER"

# Check the built binary architecture
echo "🔍 Checking built binary architecture..."
if [ -f "openterfaceQT" ]; then
    file openterfaceQT
    echo "Binary architecture verification:"
    readelf -h openterfaceQT | grep -E "(Class|Machine)"
else
    echo "❌ Binary not found after build"
fi

echo "✅ Build complete!"

# Extract version from version.h
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
# Create desktop entry
DESKTOP_FILE="/usr/share/applications/openterfaceQT.desktop"
ICON_FILE="/usr/share/pixmaps/openterfaceQT.png"
BINARY_PATH="/usr/local/bin/openterfaceQT-desktop"

# Copy icon if it exists in the project
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
    echo "  ⚠️  No application icon found, desktop entry will use default icon"
    ICON_FILE="applications-system"
fi

# Create desktop entry file
echo "  Creating desktop entry..."

# Extract version for desktop entry
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
Categories=System;Utility;Network;RemoteAccess;
Keywords=KVM;USB;remote;control;openterface;
StartupNotify=true
StartupWMClass=openterfaceQT
MimeType=
Actions=

[Desktop Action NewWindow]
Name=New Window
Exec=$BINARY_PATH
EOF

# Set proper permissions
sudo chmod 644 "$DESKTOP_FILE"

# Update desktop database
if command -v update-desktop-database &> /dev/null; then
    echo "  Updating desktop database..."
    sudo update-desktop-database /usr/share/applications/
fi

# Update icon cache if available
if command -v gtk-update-icon-cache &> /dev/null; then
    echo "  Updating icon cache..."
    sudo gtk-update-icon-cache -t /usr/share/pixmaps/ 2>/dev/null || true
fi

echo "✅ Desktop integration completed!"
echo "   Application should now appear in your desktop environment's application menu"
echo ""
echo "🔍 System Information:"
if [ -n "$APP_VERSION" ]; then
    echo "Application version: $APP_VERSION"
fi
echo "Current system architecture: $(uname -m)"
echo "Current system platform: $(uname -s)"
echo "Built binary location: $(pwd)/openterfaceQT"
echo "System binary location: /usr/local/bin/openterfaceQT"
echo "Desktop launcher: /usr/local/bin/openterfaceQT-desktop"
echo ""
echo "🚀 How to run the application:"
echo "  Method 1 (Desktop): Search for 'Openterface QT' in your application menu"
echo "  Method 2 (Terminal): openterfaceQT"
echo "  Method 3 (Build dir): cd $(pwd) && ./openterfaceQT"
echo ""
echo "🔧 Troubleshooting Qt plugin issues:"
echo "  If you get 'Qt platform plugin' errors:"
echo "  1. Try: export QT_PLUGIN_PATH=$QT_PLUGIN_PATH"
echo "  2. Try: export QT_QPA_PLATFORM=xcb"
echo "  3. Use the desktop launcher: /usr/local/bin/openterfaceQT-desktop"
echo ""
echo "If you get architecture errors (like 'x86_64-binfmt-P' or 'cannot execute binary file'):"
echo "  1. Make sure you're building on the same architecture you plan to run on"
echo "  2. Clean build directory: rm -rf build && mkdir build"
echo "  3. Re-run this script"
echo ""
echo "💡 If mouse/keyboard don't work, try: sudo apt remove brltty"
echo "⚠️  You may need to reboot for permission changes to take effect."