#!/bin/bash
# To install OpenTerface QT, you can run the following command:
# curl -sSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/refs/heads/main/build-script/build-from-source.sh | sudo bash

set -e  # Exit on error 
set -x  # Show commands

# Configuration
XKBCOMMON_VERSION=1.7.0
QT_VERSION="6.5.3"
QT_MAJOR_VERSION="6.5"
INSTALL_PREFIX="/opt/Qt6"
BUILD_DIR="$PWD/qt-build"
MODULES=("qtbase" "qtshadertools" "qtmultimedia" "qtsvg" "qtserialport")
DOWNLOAD_BASE_URL="https://download.qt.io/archive/qt/${QT_MAJOR_VERSION}/${QT_VERSION}/submodules"

# Update and install dependencies with handling for held packages
sudo apt update -y
sudo apt install -y --allow-change-held-packages \
    build-essential \
    libdbus-1-dev \
    libgl1-mesa-dev \
    libasound2-dev \
    '^libxcb.*-dev' \
    libx11-xcb-dev \
    libxrender-dev \
    libxrandr-dev \
    libxi-dev \
    libxkbcommon-dev \
    libxkbcommon-x11-dev \
    libpulse-dev \
    pulseaudio \
    libusb-1.0-0-dev \
    libfontconfig1-dev \
    libfreetype6-dev \
    ninja-build \
    cmake \
    yasm \
    wget \
    unzip \
    git

# Clean up unused packages (optional)
sudo apt autoremove -y

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Download and install FFmpeg 6.1.1
if [ ! -d "FFmpeg-n6.1.1" ]; then
    wget https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n6.1.1.tar.gz
    # Extract the downloaded tarball
    tar -xzf n6.1.1.tar.gz
else
    echo "FFmpeg-n6.1.1 already exists, skipping download."
fi

cd FFmpeg-n6.1.1
# Configure the build
./configure --prefix=/usr/local --enable-shared --disable-static
# Compile the source code
make -j$(nproc)
# Install the compiled binaries
sudo make install
# load the lib
sudo ldconfig
# Clean up
cd ..
rm -rf n6.1.1.tar.gz

# Download and extract modules
for module in "${MODULES[@]}"; do
    if [ ! -d "$module" ]; then
        wget "${DOWNLOAD_BASE_URL}/${module}-everywhere-src-${QT_VERSION}.zip"
        unzip "${module}-everywhere-src-${QT_VERSION}.zip"
        mv "${module}-everywhere-src-${QT_VERSION}" "${module}"
        rm "${module}-everywhere-src-${QT_VERSION}.zip"
    fi
done

# Build qtbase first
cd "$BUILD_DIR/qtbase"
mkdir -p build && cd build
cmake -GNinja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DFEATURE_dbus=ON \
    -DFEATURE_sql=OFF \
    -DFEATURE_testlib=OFF \
    -DFEATURE_icu=OFF \
    -DFEATURE_opengl=ON \
    ..
ninja -j$(nproc)
sudo ninja install

# Build qtshadertools
cd "$BUILD_DIR/qtshadertools"
mkdir -p build && cd build
cmake -GNinja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
    ..
ninja -j$(nproc)
sudo ninja install

# Build other modules
for module in "${MODULES[@]}"; do
    if [[ "$module" != "qtbase" && "$module" != "qtshadertools" ]]; then
        cd "$BUILD_DIR/$module"
        mkdir -p build && cd build
        
        if [[ "$module" == "qtmultimedia" ]]; then
            # Special configuration for qtmultimedia to enable FFmpeg
            cmake -GNinja \
                -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
                -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
                -DFEATURE_ffmpeg=ON \
                -DFFmpeg_DIR="$BUILD_DIR/FFmpeg-n6.1.1" \
                -DFEATURE_pulseaudio=ON \
                ..
        else
            cmake -GNinja \
                -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
                -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
                ..
        fi
        
        ninja -j$(nproc)
        sudo ninja install
    fi
done

#############
# Download latest release and build openterfaceQT from source

export PATH=$INSTALL_PREFIX/bin:$PATH

cd $BUILD_DIR
if [ ! -d "Openterface_QT" ]; then
    git clone https://github.com/TechxArtisanStudio/Openterface_QT.git
fi

cd $BUILD_DIR/Openterface_QT

#git fetch --tags
#LATEST_TAG=$(git describe --tags $(git rev-list --tags --max-count=1))
#git checkout ${LATEST_TAG}

rm -rf build
mkdir build
cd build
cmake  -GNinja -S .. -B . \
-DCMAKE_BUILD_TYPE=Release \
-DCMAKE_PREFIX_PATH=/opt/Qt6 \
-DCMAKE_INSTALL_PREFIX=release \
-DCMAKE_VERBOSE_MAKEFILE=ON 
ninja -j$(nproc)
sudo ninja install

#clean up all the build folder
echo "Cleaning the build folder..."
rm -rf $BUILD_DIR

# Print instructions for running the program
echo "
==========================================================================
Build completed successfully! 

To run OpenTerface QT:
1. First, ensure you have the necessary permissions:
   - Add yourself to the dialout group (for serial port access):
     sudo usermod -a -G dialout $USER
   
   - Set up hidraw permissions (for USB device access):
     echo 'KERNEL== \"hidraw*\", SUBSYSTEM==\"hidraw\", MODE=\"0666\"' | sudo tee /etc/udev/rules.d/51-openterface.rules 
     sudo udevadm control --reload-rules
     sudo udevadm trigger

2. You may need to log out and log back in for the group changes to take effect.

3. You can now run OpenTerface QT by typing:
   openterfaceQT

Note: If you experience issues controlling mouse and keyboard:
- Try removing brltty: sudo apt remove brltty
- Unplug and replug your OpenTerface device
- Check if the serial port is recognized: ls /dev/ttyUSB*
==========================================================================
"
