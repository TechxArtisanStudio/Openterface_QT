#!/bin/bash
set -e

echo "🔧 Installing dependencies for Kali Linux..."
sudo apt-get update -y --allow-releaseinfo-change --allow-unauthenticated || true

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
    read -p "Do you want to remove existing installation(s)? (Y/n): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Nn]$ ]]; then
        echo "⚠️  Continuing with existing installation present..."
        echo "   Note: You may experience conflicts or run the wrong version."
    else
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
    fi
else
    echo "✅ No existing system installation found"
fi

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