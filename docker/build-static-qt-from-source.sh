#!/bin/bash
set -e

# To install OpenTerface QT, you can run this script as a user with appropriate permissions.

# Install minimal build requirements
sudo apt-get update
sudo apt-get install -y build-essential meson ninja-build bison flex pkg-config python3-pip linux-headers-$(uname -r) \
    autoconf automake libtool autoconf-archive \
    libglib2.0-dev libsndfile1-dev
pip3 install cmake

# Configuration
XKBCOMMON_VERSION=1.7.0
LIBUSB_VERSION=1.0.26
DBUS_VERSION=1.15.8
FREETYPE_VERSION=2.13.2
GPERF_VERSION=3.1
FONTCONFIG_VERSION=2.14.2
PULSEAUDIO_VERSION=16.1
ALSA_VERSION=1.2.10
GLIB_VERSION=2.78.3
SNDFILE_VERSION=1.2.0
QT_VERSION=6.5.3
QT_MAJOR_VERSION=6.5
INSTALL_PREFIX=/opt/Qt6
BUILD_DIR=$(pwd)/qt-build
MODULES=("qtbase" "qtshadertools" "qtmultimedia" "qtsvg" "qtserialport")
DOWNLOAD_BASE_URL="https://download.qt.io/archive/qt/$QT_MAJOR_VERSION/$QT_VERSION/submodules"



# Check for required tools
command -v curl >/dev/null 2>&1 || { echo "Curl is not installed. Please install Curl."; exit 1; }
command -v cmake >/dev/null 2>&1 || { echo "CMake is not installed. Please install CMake."; exit 1; }
command -v meson >/dev/null 2>&1 || { echo "Meson is not installed. Please install Meson."; exit 1; }
command -v ninja >/dev/null 2>&1 || { echo "Ninja is not installed. Please install Ninja."; exit 1; }

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Build ALSA lib
echo "Building ALSA $ALSA_VERSION from source..."
if [ ! -d "alsa-lib" ]; then
    curl -L -o alsa.tar.bz2 "https://www.alsa-project.org/files/pub/lib/alsa-lib-${ALSA_VERSION}.tar.bz2"
    tar xf alsa.tar.bz2
    mv "alsa-lib-${ALSA_VERSION}" alsa-lib
    rm alsa.tar.bz2
fi

cd alsa-lib
./configure --prefix=/usr --enable-static --disable-shared
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"

# Build libusb from source
echo "Building libusb $LIBUSB_VERSION from source..."
if [ ! -d "libusb" ]; then
    curl -L -o libusb.tar.bz2 "https://github.com/libusb/libusb/releases/download/v${LIBUSB_VERSION}/libusb-${LIBUSB_VERSION}.tar.bz2"
    tar xf libusb.tar.bz2
    mv "libusb-${LIBUSB_VERSION}" libusb
    rm libusb.tar.bz2
fi

cd libusb
./configure --prefix=/usr --enable-static --disable-shared --disable-udev
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"

# Build gperf from source
echo "Building gperf $GPERF_VERSION from source..."
if [ ! -d "gperf" ]; then
    curl -L -o gperf.tar.gz "https://ftp.gnu.org/gnu/gperf/gperf-${GPERF_VERSION}.tar.gz"
    tar xf gperf.tar.gz
    mv "gperf-${GPERF_VERSION}" gperf
    rm gperf.tar.gz
fi

cd gperf
./configure --prefix=/usr
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"

# Build FreeType
echo "Building FreeType $FREETYPE_VERSION from source..."
if [ ! -d "freetype" ]; then
    curl -L -o freetype.tar.xz "https://download.savannah.gnu.org/releases/freetype/freetype-${FREETYPE_VERSION}.tar.xz"
    tar xf freetype.tar.xz
    mv "freetype-${FREETYPE_VERSION}" freetype
    rm freetype.tar.xz
fi

cd freetype
./configure --prefix=/usr --enable-static --disable-shared
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"

# Build Fontconfig
echo "Building Fontconfig $FONTCONFIG_VERSION from source..."
if [ ! -d "fontconfig" ]; then
    curl -L -o fontconfig.tar.xz "https://www.freedesktop.org/software/fontconfig/release/fontconfig-${FONTCONFIG_VERSION}.tar.xz"
    tar xf fontconfig.tar.xz
    mv "fontconfig-${FONTCONFIG_VERSION}" fontconfig
    rm fontconfig.tar.xz
fi

cd fontconfig
./configure --prefix=/usr --enable-static --disable-shared
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"

# Build D-Bus
echo "Building D-Bus $DBUS_VERSION from source..."
if [ ! -d "dbus" ]; then
    curl -L -o dbus.tar.xz "https://dbus.freedesktop.org/releases/dbus/dbus-${DBUS_VERSION}.tar.xz"
    tar xf dbus.tar.xz
    mv "dbus-${DBUS_VERSION}" dbus
    rm dbus.tar.xz
fi

cd dbus
# Run autogen if configure doesn't exist
if [ ! -f "./configure" ]; then
    ./autogen.sh
fi
CFLAGS="-fPIC" CXXFLAGS="-fPIC" ./configure --prefix=/usr \
    --enable-static \
    --disable-shared \
    --disable-systemd \
    --disable-selinux \
    --disable-xml-docs \
    --disable-doxygen-docs \
    --disable-ducktype-docs
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"

# Build libsndfile from source
echo "Building libsndfile $SNDFILE_VERSION from source..."
if [ ! -d "libsndfile" ]; then
    curl -L -o sndfile.tar.xz "https://github.com/libsndfile/libsndfile/releases/download/${SNDFILE_VERSION}/libsndfile-${SNDFILE_VERSION}.tar.xz"
    tar xf sndfile.tar.xz
    mv "libsndfile-${SNDFILE_VERSION}" libsndfile
    rm sndfile.tar.xz
fi

cd libsndfile
./configure --prefix=/usr --enable-static --disable-shared
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"

# Build GLib from source
echo "Building GLib $GLIB_VERSION from source..."
if [ ! -d "glib" ]; then
    curl -L -o glib.tar.xz "https://download.gnome.org/sources/glib/2.78/glib-${GLIB_VERSION}.tar.xz"
    tar xf glib.tar.xz
    mv "glib-${GLIB_VERSION}" glib
    rm glib.tar.xz
fi

cd glib
mkdir -p build
cd build
meson setup --prefix=/usr \
    -Ddefault_library=static \
    -Dtests=false \
    ..
ninja
sudo ninja install
cd "$BUILD_DIR"

# Build PulseAudio
echo "Building PulseAudio $PULSEAUDIO_VERSION from source..."
if [ ! -d "pulseaudio" ]; then
    curl -L -o pulseaudio.tar.xz "https://www.freedesktop.org/software/pulseaudio/releases/pulseaudio-${PULSEAUDIO_VERSION}.tar.xz"
    tar xf pulseaudio.tar.xz
    mv "pulseaudio-${PULSEAUDIO_VERSION}" pulseaudio
    rm pulseaudio.tar.xz
fi

cd pulseaudio
mkdir -p build
cd build
meson setup --prefix=/usr \
    -Ddaemon=false \
    -Ddoxygen=false \
    -Dman=false \
    -Dtests=false \
    -Ddefault_library=static \
    -Ddatabase=simple \
    -Dalsa=enabled \
    -Dasyncns=disabled \
    -Davahi=disabled \
    -Dbluez5=disabled \
    -Ddbus=enabled \
    -Dfftw=disabled \
    -Dglib=enabled \
    -Dgsettings=disabled \
    -Dgtk=disabled \
    -Dhal-compat=false \
    -Dipv6=false \
    -Djack=disabled \
    -Dlirc=disabled \
    -Dopenssl=disabled \
    -Dorc=disabled \
    -Dsamplerate=disabled \
    -Dsoxr=disabled \
    -Dspeex=disabled \
    -Dsystemd=disabled \
    -Dtcpwrap=disabled \
    -Dudev=disabled \
    -Dwebrtc-aec=disabled \
    -Dx11=disabled \
    ..
ninja
sudo ninja install
cd "$BUILD_DIR"

# Build libxkbcommon from source
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
meson setup --prefix=/usr \
    -Denable-docs=false \
    -Denable-wayland=false \
    -Denable-x11=true \
    -Ddefault_library=static \
    ..
ninja
sudo ninja install

# Build CH341 driver statically
echo "Building CH341 driver..."
cd "$BUILD_DIR"
if [ ! -d "ch341" ]; then
    mkdir ch341
    cp -r ../driver/linux/* ch341/
fi

cd ch341
make clean
KCPPFLAGS="-static" make
sudo make install

cd "$BUILD_DIR"


# Download and extract modules
for module in "${MODULES[@]}"; do
    if [ ! -d "$module" ]; then
        curl -L -o "$module.zip" "$DOWNLOAD_BASE_URL/$module-everywhere-src-$QT_VERSION.zip"
        unzip -q "$module.zip"
        mv "$module-everywhere-src-$QT_VERSION" "$module"
        rm "$module.zip"
    fi
done

# Build qtbase first
echo "Building qtbase..."
cd "$BUILD_DIR/qtbase"
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DFEATURE_dbus=ON \
    -DFEATURE_sql=OFF \
    -DFEATURE_testlib=OFF \
    -DFEATURE_icu=OFF \
    -DFEATURE_opengl=ON \
    ..

echo "Building qtbase..."
cmake --build .
echo "Installing qtbase..."
cmake --install .


# Build qtshadertools
echo "Building qtshadertools..."
cd "$BUILD_DIR/qtshadertools"
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    ..

echo "Building qtshadertools..."
cmake --build . --parallel
echo "Installing qtshadertools..."
cmake --install .

# Build other modules
for module in "${MODULES[@]}"; do
    if [[ "$module" != "qtbase" && "$module" != "qtshadertools" ]]; then
        cd "$BUILD_DIR/$module"
        mkdir build
        cd build
        echo "Building $module..."
        if [[ "$module" == "qtmultimedia" ]]; then
            # Special configuration for qtmultimedia to enable FFmpeg
            cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
                -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
                -DBUILD_SHARED_LIBS=OFF \
                ..
        else
            cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
                -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
                -DBUILD_SHARED_LIBS=OFF \
                ..
        fi
        
        echo "Building $module..."
        cmake --build . --parallel
        echo "Installing $module..."
        cmake --install .
    fi
done

# Quick fix: Add -loleaut32 to qnetworklistmanager.prl
PRL_FILE="$INSTALL_PREFIX/plugins/networkinformation/qnetworklistmanager.prl"
if [ -f "$PRL_FILE" ]; then
    echo "Updating $PRL_FILE to include -loleaut32..."
    echo "QMAKE_PRL_LIBS += -loleaut32" >> "$PRL_FILE"
else
    echo "Warning: $PRL_FILE not found. Please check the build process."
fi

# Check for Qt6Config.cmake
if [ ! -f "$INSTALL_PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    echo "Error: Qt6Config.cmake not found. Please check the installation."
    exit 1
fi
