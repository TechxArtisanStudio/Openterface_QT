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
    git clone https://github.com/TechxArtisanStudio/Openterface_QT.git
fi
cd Openterface_QT

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

echo "🖥️ Creating desktop integration..."
# Create desktop entry
DESKTOP_FILE="/usr/share/applications/openterfaceQT.desktop"
ICON_FILE="/usr/share/pixmaps/openterfaceQT.png"
BINARY_PATH="/usr/local/bin/openterfaceQT"

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
sudo tee "$DESKTOP_FILE" > /dev/null << EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=Openterface QT
Comment=KVM over USB for seamless computer control
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
echo "Current system architecture: $(uname -m)"
echo "Current system platform: $(uname -s)"
echo "Built binary location: $(pwd)/openterfaceQT"
echo ""
echo "To run the application:"
echo "  cd $(pwd)"
echo "  ./openterfaceQT"
echo ""
echo "If you get architecture errors (like 'x86_64-binfmt-P' or 'cannot execute binary file'):"
echo "  1. Make sure you're building on the same architecture you plan to run on"
echo "  2. Clean build directory: rm -rf build && mkdir build"
echo "  3. Re-run this script"
echo ""
echo "💡 If mouse/keyboard don't work, try: sudo apt remove brltty"
echo "⚠️  You may need to reboot for permission changes to take effect."