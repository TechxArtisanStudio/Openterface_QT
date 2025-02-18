#!/bin/bash
set -e

# To install OpenTerface QT, you can run this script as a user with appropriate permissions.

# Install minimal build requirements
sudo apt-get update
sudo apt-get install -y build-essential meson ninja-build bison flex pkg-config python3-pip linux-headers-$(uname -r) \
    autoconf automake libtool autoconf-archive cmake libxml2-dev

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
XCB_PROTO_VERSION=1.16.0
XCB_VERSION=1.16
XCB_UTIL_VERSION=0.4.1
XCB_UTIL_WM_VERSION=0.4.2
XCB_UTIL_KEYSYMS_VERSION=0.4.1
XCB_UTIL_RENDERUTIL_VERSION=0.3.10
XORG_MACROS_VERSION=1.19.3
XPROTO_VERSION=7.0.31
LIBXDMCP_VERSION=1.1.4
FFMPEG_VERSION=6.1.1
XKB_CONFIG_VERSION=2.41
LIBXAU_VERSION=1.0.12
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
    curl -L -o freetype.tar.gz "https://download.savannah.gnu.org/releases/freetype/freetype-${FREETYPE_VERSION}.tar.gz"
    tar xf freetype.tar.gz
    mv "freetype-${FREETYPE_VERSION}" freetype
    rm freetype.tar.gz
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
CFLAGS="-fPIC" CXXFLAGS="-fPIC" ./configure --prefix=/usr --enable-static --disable-shared
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




# Build all XCB components first, then libxkbcommon
# After building xcb-util-xkb, add:

# Update pkg-config path to find XCB
export PKG_CONFIG_PATH="/usr/lib/pkgconfig:/usr/local/lib/pkgconfig"

# Install xkeyboard-config
echo "Installing xkeyboard-config $XKB_CONFIG_VERSION..."
if [ ! -d "xkeyboard-config" ]; then
    curl -L -o xkeyboard-config.tar.xz "https://www.x.org/archive/individual/data/xkeyboard-config/xkeyboard-config-${XKB_CONFIG_VERSION}.tar.xz"
    tar xf xkeyboard-config.tar.xz
    mv "xkeyboard-config-${XKB_CONFIG_VERSION}" xkeyboard-config
    rm xkeyboard-config.tar.xz
fi

cd xkeyboard-config
# Create a build directory for Meson
mkdir -p build
cd build
meson setup --prefix=/usr \
    -Ddefault_library=static \
    ..
ninja
sudo ninja install
cd "$BUILD_DIR"


# Build xorg-macros
echo "Installing xorg-macros $XORG_MACROS_VERSION..."
if [ ! -d "xorg-macros" ]; then
    curl -L -o xorg-macros.tar.bz2 "https://www.x.org/releases/individual/util/util-macros-${XORG_MACROS_VERSION}.tar.bz2"
    tar xf xorg-macros.tar.bz2
    mv "util-macros-${XORG_MACROS_VERSION}" xorg-macros
    rm xorg-macros.tar.bz2
fi

cd xorg-macros
# xorg-macros does not need to be built; it just needs to be installed
./configure --prefix=/usr
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"

# Build xproto
echo "Building xproto $XPROTO_VERSION from source..."
if [ ! -d "xproto" ]; then
    curl -L -o xproto.tar.bz2 "https://www.x.org/releases/individual/proto/xproto-${XPROTO_VERSION}.tar.bz2"
    tar xf xproto.tar.bz2
    mv "xproto-${XPROTO_VERSION}" xproto
    rm xproto.tar.bz2
fi

cd xproto
./configure --prefix=/usr
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"

# Build libXrandr
echo "Building libXrandr from source..."
XRANDR_VERSION="1.5.4"
if [ ! -d "libXrandr" ]; then
    curl -L -o libXrandr.tar.xz "https://www.x.org/releases/individual/lib/libXrandr-${XRANDR_VERSION}.tar.xz"
    tar xf libXrandr.tar.xz
    mv "libXrandr-${XRANDR_VERSION}" libXrandr
    rm libXrandr.tar.xz
fi

cd libXrandr
CFLAGS="-fPIC" ./configure --prefix=/usr \
    --enable-static \
    --disable-shared \
    --with-pic
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"

# Build xcb-proto
echo "Building xcb-proto $XCB_PROTO_VERSION from source..."
if [ ! -d "xcb-proto" ]; then
    curl -L -o xcb-proto.tar.xz "https://xorg.freedesktop.org/archive/individual/proto/xcb-proto-${XCB_PROTO_VERSION}.tar.xz"
    tar xf xcb-proto.tar.xz
    mv "xcb-proto-${XCB_PROTO_VERSION}" xcb-proto
    rm xcb-proto.tar.xz
fi

cd xcb-proto
./configure --prefix=/usr
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"

# Build libXdmcp
echo "Building libXdmcp $LIBXDMCP_VERSION from source..."
if [ ! -d "libXdmcp" ]; then
    curl -L -o libXdmcp.tar.gz "https://www.x.org/releases/individual/lib/libXdmcp-${LIBXDMCP_VERSION}.tar.gz"
    tar xf libXdmcp.tar.gz
    mv "libXdmcp-${LIBXDMCP_VERSION}" libXdmcp
    rm libXdmcp.tar.gz
fi

cd libXdmcp
CFLAGS="-fPIC" ./configure --prefix=/usr --enable-static --disable-shared
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"


# Build libXau
echo "Building libXau $LIBXAU_VERSION from source..."
if [ ! -d "libXau" ]; then
    curl -L -o libXau.tar.gz "https://www.x.org/releases/individual/lib/libXau-${LIBXAU_VERSION}.tar.gz"
    
    # Check if the download was successful
    if [ $? -ne 0 ]; then
        echo "Error: Failed to download libXau."
        exit 1
    fi

    tar xf libXau.tar.gz
    mv "libXau-${LIBXAU_VERSION}" libXau
    rm libXau.tar.gz
fi

cd libXau
CFLAGS="-fPIC" ./configure --prefix=/usr --enable-static --disable-shared
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"

# Build libxcb
echo "Building libxcb $XCB_VERSION from source..."
if [ ! -d "libxcb" ]; then
    curl -L -o libxcb.tar.xz "https://xorg.freedesktop.org/archive/individual/lib/libxcb-${XCB_VERSION}.tar.xz"
    tar xf libxcb.tar.xz
    mv "libxcb-${XCB_VERSION}" libxcb
    rm libxcb.tar.xz
fi

cd libxcb
CFLAGS="-fPIC" ./configure --prefix=/usr --enable-static --disable-shared --with-libXau
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"

# Build xcb-util
echo "Building xcb-util $XCB_UTIL_VERSION from source..."
if [ ! -d "xcb-util" ]; then
    curl -L -o xcb-util.tar.gz "https://xcb.freedesktop.org/dist/xcb-util-${XCB_UTIL_VERSION}.tar.gz"
    tar xf xcb-util.tar.gz
    mv "xcb-util-${XCB_UTIL_VERSION}" xcb-util
    rm xcb-util.tar.gz
fi

cd xcb-util
CFLAGS="-fPIC" ./configure --prefix=/usr --enable-static --disable-shared
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"

# Build xcb-util-wm
echo "Building xcb-util-wm $XCB_UTIL_WM_VERSION from source..."
if [ ! -d "xcb-util-wm" ]; then
    curl -L -o xcb-util-wm.tar.gz "https://xcb.freedesktop.org/dist/xcb-util-wm-${XCB_UTIL_WM_VERSION}.tar.gz"
    tar xf xcb-util-wm.tar.gz
    mv "xcb-util-wm-${XCB_UTIL_WM_VERSION}" xcb-util-wm
    rm xcb-util-wm.tar.gz
fi

cd xcb-util-wm
CFLAGS="-fPIC" ./configure --prefix=/usr --enable-static --disable-shared
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"

# Build xcb-util-keysyms
echo "Building xcb-util-keysyms $XCB_UTIL_KEYSYMS_VERSION from source..."
if [ ! -d "xcb-util-keysyms" ]; then
    curl -L -o xcb-util-keysyms.tar.gz "https://xcb.freedesktop.org/dist/xcb-util-keysyms-${XCB_UTIL_KEYSYMS_VERSION}.tar.gz"
    tar xf xcb-util-keysyms.tar.gz
    mv "xcb-util-keysyms-${XCB_UTIL_KEYSYMS_VERSION}" xcb-util-keysyms
    rm xcb-util-keysyms.tar.gz
fi

cd xcb-util-keysyms
CFLAGS="-fPIC" ./configure --prefix=/usr --enable-static --disable-shared
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"

# Build libxcb-randr
echo "Building libxcb-randr from source..."
if [ ! -d "libxcb-randr" ]; then
    curl -L -o libxcb-randr.tar.xz "https://xcb.freedesktop.org/dist/xcb-util-renderutil-${XCB_UTIL_RENDERUTIL_VERSION}.tar.xz"
    tar xf libxcb-randr.tar.xz
    mv "xcb-util-renderutil-${XCB_UTIL_RENDERUTIL_VERSION}" libxcb-randr
    rm libxcb-randr.tar.xz
fi

cd libxcb-randr
CFLAGS="-fPIC" ./configure --prefix=/usr --enable-static --disable-shared
make -j$(nproc)
sudo make install
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
# Check if the meson.build file exists
if [ -f "$LIBXKBCOMMON_MESON_FILE" ]; then
    echo "Modifying $LIBXKBCOMMON_MESON_FILE to link libXau statically..."
    
    # Add dependency for libXau
    sed -i "s/\(xcb_dep = dependency('xcb'\),[^)]*)/\1)/" "$LIBXKBCOMMON_MESON_FILE"
    sed -i "s/\(xcb_xkb_dep = dependency('xcb-xkb'\),[^)]*)/\1)/" "$LIBXKBCOMMON_MESON_FILE"
    sed -i "/xcb_xkb_dep = dependency('xcb-xkb')/axau_dep = dependency('xau', static: true)" "$LIBXKBCOMMON_MESON_FILE"
    sed -i "/xau_dep = dependency('xau', static: true)/axdmcp_dep = dependency('xdmcp', static: true)" "$LIBXKBCOMMON_MESON_FILE" 

    # Ensure libXau is linked with xkbcli-interactive-x11
    sed -i "/xcb_xkb_dep,/axau_dep," "$LIBXKBCOMMON_MESON_FILE"
    sed -i "/xau_dep,/axdmcp_dep," "$LIBXKBCOMMON_MESON_FILE"
else
    echo "Error: $LIBXKBCOMMON_MESON_FILE not found."
fi
 
meson setup --prefix=/usr \
    -Denable-docs=false \
    -Denable-wayland=false \
    -Denable-x11=true \
    -Ddefault_library=static \
    -Dxkb-config-root=/usr/share/X11/xkb \
    -Dx-locale-root=/usr/share/X11/locale \
    ..
ninja
sudo ninja install
cd "$BUILD_DIR"

# Install NASM
echo "Installing NASM..."
NASM_VERSION="2.16.01"
curl -L -o nasm.tar.xz "https://www.nasm.us/pub/nasm/releasebuilds/${NASM_VERSION}/nasm-${NASM_VERSION}.tar.xz"
tar xf nasm.tar.xz
cd "nasm-${NASM_VERSION}"
./configure --prefix=/usr
make -j$(nproc)
sudo make install
cd "$BUILD_DIR"
rm -rf "nasm-${NASM_VERSION}" nasm.tar.xz

# Build FFmpeg
echo "Building FFmpeg $FFMPEG_VERSION from source..."
if [ ! -d "FFmpeg-n${FFMPEG_VERSION}" ]; then
    curl -L -o ffmpeg.tar.gz "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n${FFMPEG_VERSION}.tar.gz"
    tar -xzf ffmpeg.tar.gz
    rm ffmpeg.tar.gz
else
    echo "FFmpeg-n${FFMPEG_VERSION} already exists, skipping download."
fi

cd "FFmpeg-n${FFMPEG_VERSION}"
# Configure the build with only free components
./configure --prefix=/usr/local \
    --disable-shared \
    --enable-gpl \
    --enable-version3 \
    --disable-nonfree \
    --disable-doc \
    --disable-programs \
    --enable-pic \
    --enable-static
make -j$(nproc)
sudo make install
sudo ldconfig
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

sudo apt-get install -y libgl1-mesa-dev libglu1-mesa-dev libxrender-dev libxi-dev

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
    -DFEATURE_opengl_desktop=OFF \
    -DFEATURE_opengles2=ON \
    -DCMAKE_PREFIX_PATH="/usr" \
    -DFEATURE_accessibility=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DQT_BUILD_EXAMPLES=OFF \
    -DQT_BUILD_TESTS=OFF \
    -DFEATURE_static_runtime=ON \
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
