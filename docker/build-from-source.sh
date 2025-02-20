#!/bin/bash
# To install OpenTerface QT, you can run the following command:
# curl -sSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/refs/heads/main/docker/build-from-source.sh | sudo bash

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
    libxi-dev \
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



# Build libxkbcommon
echo "Building libxkbcommon $XKBCOMMON_VERSION from source..."
if [ ! -d "libxkbcommon" ]; then
    curl -L -o libxkbcommon.tar.gz "https://xkbcommon.org/download/libxkbcommon-${XKBCOMMON_VERSION}.tar.xz"
    tar xf libxkbcommon.tar.gz
    mv "libxkbcommon-${XKBCOMMON_VERSION}" libxkbcommon
    rm libxkbcommon.tar.gz
fi

cd libxkbcommon
mkdir -p build
cd build

LIBXKBCOMMON_MESON_FILE="$BUILD_DIR/libxkbcommon/meson.build"
# # Check if the meson.build file exists
# if [ -f "$LIBXKBCOMMON_MESON_FILE" ]; then
#     echo "Modifying $LIBXKBCOMMON_MESON_FILE to link libXau statically..."
    
#     # Add dependency for libXau
#     sed -i "s/\(xcb_dep = dependency('xcb'\),[^)]*)/\1)/" "$LIBXKBCOMMON_MESON_FILE"
#     sed -i "s/\(xcb_xkb_dep = dependency('xcb-xkb'\),[^)]*)/\1)/" "$LIBXKBCOMMON_MESON_FILE"
#     sed -i "/xcb_xkb_dep = dependency('xcb-xkb')/axau_dep = dependency('xau', static: true)" "$LIBXKBCOMMON_MESON_FILE"
#     sed -i "/xau_dep = dependency('xau', static: true)/axdmcp_dep = dependency('xdmcp', static: true)" "$LIBXKBCOMMON_MESON_FILE" 
    
    
#     sed -i "/xcb_xkb_dep,/axau_dep," "$LIBXKBCOMMON_MESON_FILE"
#     sed -i "/xau_dep,/axdmcp_dep," "$LIBXKBCOMMON_MESON_FILE"

# else
#     echo "Error: $LIBXKBCOMMON_MESON_FILE not found."
# fi
    
meson setup --prefix=/usr \
    -Denable-docs=false \
    -Denable-wayland=false \
    -Denable-x11=true \
    -Dxkb-config-root=/usr/share/X11/xkb \
    -Dx-locale-root=/usr/share/X11/locale \
    ..
ninja

echo "Installing libxkbcommon $XKBCOMMON_VERSION..."
cd "$BUILD_DIR"/libxkbcommon/build
sudo ninja install

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
ninja
sudo ninja install

# Build qtshadertools
cd "$BUILD_DIR/qtshadertools"
mkdir -p build && cd build
cmake -GNinja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
    ..
ninja
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
        
        ninja
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

cd Openterface_QT
#git fetch --tags
#LATEST_TAG=$(git describe --tags $(git rev-list --tags --max-count=1))
#git checkout ${LATEST_TAG}

mkdir -p build
cd build
qmake6 ..
make
sudo make install

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
