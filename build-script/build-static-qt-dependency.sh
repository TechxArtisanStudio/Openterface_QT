#!/bin/bash
set -e

# Default behavior is to build and install
BUILD_ENABLED=true
INSTALL_ENABLED=true
INSTALL_PREFIX=/opt/qt-deps

# Check for parameters
for arg in "$@"; do
    case $arg in
        --no-build)
            BUILD_ENABLED=false
            shift
            ;;
        --no-install)
            INSTALL_ENABLED=false
            shift
            ;;
        *)
            echo "Usage: $0 [--no-build] [--no-install]"
            exit 1
            ;;
    esac
done

# Configuration
XKBCOMMON_VERSION=1.7.0
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
XCB_CURSOR_VERSION=0.1.5
XCB_UTIL_WM_VERSION=0.4.2
XCB_UTIL_KEYSYMS_VERSION=0.4.1
XAU_VERSION="1.0.11"
XORG_MACROS_VERSION=1.19.3
XPROTO_VERSION=7.0.31
XRANDR_VERSION="1.5.1"
XRENDER_VERSION="0.9.10"
XTRANS_VERSION="1.5.0"
XEXTPROTO_VERSION="7.3.0"
XEXT_VERSION="1.3.5"
X11_VERSION="1.8.7"
RANDRPROTO_VERSION="1.5.0"
RENDERPROTO_VERSION=0.11.1
LIBXDMCP_VERSION=1.1.4
XKB_CONFIG_VERSION=2.41
XORGPROTO_VERSION=2023.2
LIBXAU_VERSION=1.0.12
EXPAT_VERSION=2.5.0
BZ2_VERSION=1.0.8

# Create build directory
BUILD_DIR=$(pwd)/qt-build
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Install minimal build requirements
sudo apt-get update
sudo apt-get install -y build-essential meson ninja-build bison flex pkg-config python3-pip linux-headers-$(uname -r) \
    autoconf automake libtool autoconf-archive cmake libc6-dev zlib1g-dev gperf

# Check for required tools
command -v curl >/dev/null 2>&1 || { echo "Curl is not installed. Please install Curl."; exit 1; }
command -v cmake >/dev/null 2>&1 || { echo "CMake is not installed. Please install CMake."; exit 1; }
command -v meson >/dev/null 2>&1 || { echo "Meson is not installed. Please install Meson."; exit 1; }
command -v ninja >/dev/null 2>&1 || { echo "Ninja is not installed. Please install Ninja."; exit 1; }

# Build or Install ALSA lib
if $BUILD_ENABLED; then
    echo "Building ALSA $ALSA_VERSION from source..."
    if [ ! -d "alsa-lib" ]; then
        curl -L -o alsa.tar.bz2 "https://www.alsa-project.org/files/pub/lib/alsa-lib-${ALSA_VERSION}.tar.bz2"
        tar xf alsa.tar.bz2
        mv "alsa-lib-${ALSA_VERSION}" alsa-lib
        rm alsa.tar.bz2
    fi

    cd alsa-lib
    ./configure --prefix=$INSTALL_PREFIX --enable-static --disable-shared
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing ALSA $ALSA_VERSION..."
    cd "$BUILD_DIR"/alsa-lib
    sudo make install
fi
cd "$BUILD_DIR"


# Build or Install libsndfile from source
if $BUILD_ENABLED; then
    echo "Building libsndfile $SNDFILE_VERSION from source..."
    if [ ! -d "libsndfile" ]; then
        curl -L -o sndfile.tar.xz "https://github.com/libsndfile/libsndfile/releases/download/${SNDFILE_VERSION}/libsndfile-${SNDFILE_VERSION}.tar.xz"
        tar xf sndfile.tar.xz
        mv "libsndfile-${SNDFILE_VERSION}" libsndfile
        rm sndfile.tar.xz
    fi

    cd libsndfile
    CFLAGS="-fPIC" CXXFLAGS="-fPIC" ./configure --prefix=$INSTALL_PREFIX --enable-static --disable-shared
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing libsndfile $SNDFILE_VERSION..."
    cd "$BUILD_DIR"/libsndfile
    sudo make install
fi
cd "$BUILD_DIR"

# Update pkg-config path
export PKG_CONFIG_PATH="$INSTALL_PREFIX/lib/pkgconfig:$INSTALL_PREFIX/local/lib/pkgconfig:$INSTALL_PREFIX/share/pkgconfig"

# Build or Install libpulse
if $BUILD_ENABLED; then
    echo "Building libpulse from source..."
    if [ ! -d "libpulse" ]; then
        curl -L -o libpulse.tar.xz "https://www.freedesktop.org/software/pulseaudio/releases/pulseaudio-${PULSEAUDIO_VERSION}.tar.xz"
        tar xf libpulse.tar.xz
        mv "pulseaudio-${PULSEAUDIO_VERSION}" libpulse
        rm libpulse.tar.xz
    fi

    cd libpulse
    mkdir -p build
    cd build
    meson setup --prefix=$INSTALL_PREFIX \
        -Ddefault_library=static \
        -Ddoxygen=false \
        -Ddaemon=false \
        -Dtests=false \
        -Dman=false \
        -Dudev=disabled \
        -Dsystemd=disabled \
        -Dbluez5=disabled \
        -Dgtk=disabled \
        -Dopenssl=disabled \
        -Dorc=disabled \
        -Dsoxr=disabled \
        -Dspeex=disabled \
        -Dwebrtc-aec=disabled \
        -Dx11=disabled \
        -Dpkg_config_path=$PKG_CONFIG_PATH \
        ..
    ninja
fi

if $INSTALL_ENABLED; then
    echo "Installing libpulse..."
    cd "$BUILD_DIR"/libpulse/build
    sudo ninja install
fi
cd "$BUILD_DIR"

# Build or Install D-Bus
if $BUILD_ENABLED; then
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
    CFLAGS="-fPIC" CXXFLAGS="-fPIC" ./configure --prefix=$INSTALL_PREFIX \
        --enable-static \
        --disable-shared \
        --disable-systemd \
        --disable-selinux \
        --disable-xml-docs \
        --disable-doxygen-docs \
        --disable-ducktype-docs
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing D-Bus $DBUS_VERSION..."
    cd "$BUILD_DIR"/dbus
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install GLib from source
if $BUILD_ENABLED; then
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
    meson setup --prefix=$INSTALL_PREFIX \
        -Ddefault_library=static \
        -Dtests=false \
        ..
    ninja
fi

if $INSTALL_ENABLED; then
    echo "Installing GLib $GLIB_VERSION..."
    cd "$BUILD_DIR"/glib/build
    sudo ninja install
fi
cd "$BUILD_DIR"

# # Build or Install PulseAudio
# if $BUILD_ENABLED; then
#     echo "Building PulseAudio $PULSEAUDIO_VERSION from source..."
#     if [ ! -d "pulseaudio" ]; then
#         curl -L -o pulseaudio.tar.xz "https://www.freedesktop.org/software/pulseaudio/releases/pulseaudio-${PULSEAUDIO_VERSION}.tar.xz"
#         tar xf pulseaudio.tar.xz
#         mv "pulseaudio-${PULSEAUDIO_VERSION}" pulseaudio
#         rm pulseaudio.tar.xz
#     fi

#     cd pulseaudio
#     mkdir -p build
#     cd build

#     meson setup --prefix=$INSTALL_PREFIX \
#         -Ddaemon=false \
#         -Dman=false \
#         -Ddoxygen=false \
#         -Dtests=false \
#         -Ddefault_library=static \
#         -Ddatabase=simple \
#         -Dalsa=enabled \
#         -Dasyncns=disabled \
#         -Davahi=disabled \
#         -Dbluez5=disabled \
#         -Ddbus=enabled \
#         -Dfftw=disabled \
#         -Dglib=enabled \
#         -Dgsettings=disabled \
#         -Dgtk=disabled \
#         -Dhal-compat=false \
#         -Dipv6=false \
#         -Djack=disabled \
#         -Dlirc=disabled \
#         -Dopenssl=disabled \
#         -Dorc=disabled \
#         -Dsamplerate=disabled \
#         -Dsoxr=disabled \
#         -Dspeex=disabled \
#         -Dsystemd=disabled \
#         -Dtcpwrap=disabled \
#         -Dudev=disabled \
#         -Dwebrtc-aec=disabled \
#         -Dx11=disabled \
#         -Dpkg_config_path=$PKG_CONFIG_PATH \
#         ..
#     ninja
# fi

# if $INSTALL_ENABLED; then
#     echo "Installing PulseAudio $PULSEAUDIO_VERSION..."
#     cd "$BUILD_DIR"/pulseaudio/build
#     sudo ninja install
# fi
# cd "$BUILD_DIR"

# Build or Install libexpat
if $BUILD_ENABLED; then
    echo "Building libexpat $EXPAT_VERSION from source..."
    if [ ! -d "libexpat" ]; then
        curl -L -o libexpat.tar.bz2 "https://github.com/libexpat/libexpat/releases/download/R_${EXPAT_VERSION//./_}/expat-${EXPAT_VERSION}.tar.bz2"
        tar xf libexpat.tar.bz2
        mv "expat-${EXPAT_VERSION}" libexpat
        rm libexpat.tar.bz2
    fi

    cd libexpat
    ./configure --prefix=$INSTALL_PREFIX --enable-static --disable-shared
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing libexpat $EXPAT_VERSION..."
    cd "$BUILD_DIR"/libexpat
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install gperf from source
if $BUILD_ENABLED; then
    echo "Building gperf $GPERF_VERSION from source..."
    if [ ! -d "gperf" ]; then
        curl -L -o gperf.tar.gz "https://ftp.gnu.org/gnu/gperf/gperf-${GPERF_VERSION}.tar.gz"
        tar xf gperf.tar.gz
        mv "gperf-${GPERF_VERSION}" gperf
        rm gperf.tar.gz
    fi

    cd gperf
    ./configure --prefix=$INSTALL_PREFIX --enable-static --disable-shared
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing gperf $GPERF_VERSION..."
    cd "$BUILD_DIR"/gperf
    sudo make install
fi
cd "$BUILD_DIR"

# # Build or Install FreeType
# if $BUILD_ENABLED; then
#     echo "Building FreeType $FREETYPE_VERSION from source..."
#     if [ ! -d "freetype" ]; then
#         curl -L -o freetype.tar.gz "https://download.savannah.gnu.org/releases/freetype/freetype-${FREETYPE_VERSION}.tar.gz"
#         tar xf freetype.tar.gz
#         mv "freetype-${FREETYPE_VERSION}" freetype
#         rm freetype.tar.gz
#     fi

#     cd freetype
#     ./configure --prefix=$INSTALL_PREFIX --enable-static --disable-shared
#     make -j$(nproc)
# fi

# if $INSTALL_ENABLED; then
#     echo "Installing FreeType $FREETYPE_VERSION..."
#     cd "$BUILD_DIR"/freetype
#     sudo make install
# fi
# cd "$BUILD_DIR"

# # Build or Install Fontconfig
# if $BUILD_ENABLED; then
#     echo "Building Fontconfig $FONTCONFIG_VERSION from source..."
#     if [ ! -d "fontconfig" ]; then
#         curl -L -o fontconfig.tar.xz "https://www.freedesktop.org/software/fontconfig/release/fontconfig-${FONTCONFIG_VERSION}.tar.xz"
#         tar xf fontconfig.tar.xz
#         mv "fontconfig-${FONTCONFIG_VERSION}" fontconfig
#         rm fontconfig.tar.xz
#     fi

#     cd fontconfig
#     PKG_CONFIG_PATH="$INSTALL_PREFIX/lib/pkgconfig:$INSTALL_PREFIX/local/lib/pkgconfig:$INSTALL_PREFIX/share/pkgconfig" \
#     ./configure --prefix=$INSTALL_PREFIX --enable-static --disable-shared
#     make -j$(nproc)
# fi

# if $INSTALL_ENABLED; then
#     echo "Installing Fontconfig $FONTCONFIG_VERSION..."
#     cd "$BUILD_DIR"/fontconfig
#     sudo make install
# fi
# cd "$BUILD_DIR"

# Build or Install GLib from source
if $BUILD_ENABLED; then
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
    meson setup --prefix=$INSTALL_PREFIX \
        -Ddefault_library=static \
        -Dtests=false \
        ..
    ninja
fi

if $INSTALL_ENABLED; then
    echo "Installing GLib $GLIB_VERSION..."
    cd "$BUILD_DIR"/glib/build
    sudo ninja install
fi
cd "$BUILD_DIR"

# Update pkg-config path
export PKG_CONFIG_PATH="$INSTALL_PREFIX/lib/pkgconfig:$INSTALL_PREFIX/local/lib/pkgconfig:$INSTALL_PREFIX/share/pkgconfig"

# Build or Install randrproto (part of x11proto-randr)
if $BUILD_ENABLED; then
    echo "Building randrproto from source..."
    if [ ! -d "randrproto" ]; then
        curl -L -o randrproto.tar.bz2 "https://www.x.org/releases/individual/proto/randrproto-${RANDRPROTO_VERSION}.tar.bz2"
        tar xf randrproto.tar.bz2
        mv "randrproto-${RANDRPROTO_VERSION}" randrproto
        rm randrproto.tar.bz2
    fi

    cd randrproto
    ./configure --prefix=$INSTALL_PREFIX
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing randrproto..."
    cd "$BUILD_DIR"/randrproto
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install renderproto
if $BUILD_ENABLED; then
    echo "Building renderproto from source..."
    if [ ! -d "renderproto" ]; then
        curl -L -o renderproto.tar.bz2 "https://www.x.org/releases/individual/proto/renderproto-${RENDERPROTO_VERSION}.tar.bz2"
        tar xf renderproto.tar.bz2
        mv "renderproto-${RENDERPROTO_VERSION}" renderproto
        rm renderproto.tar.bz2
    fi

    cd renderproto
    ./configure --prefix=$INSTALL_PREFIX
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing renderproto..."
    cd "$BUILD_DIR"/renderproto
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install xextproto
if $BUILD_ENABLED; then
    echo "Building xextproto from source..."
    if [ ! -d "xextproto" ]; then
        curl -L -o xextproto.tar.bz2 "https://www.x.org/releases/individual/proto/xextproto-${XEXTPROTO_VERSION}.tar.bz2"
        tar xf xextproto.tar.bz2
        mv "xextproto-${XEXTPROTO_VERSION}" xextproto
        rm xextproto.tar.bz2
    fi

    cd xextproto
    ./configure --prefix=$INSTALL_PREFIX
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing xextproto..."
    cd "$BUILD_DIR"/xextproto
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install xkeyboard-config
if $BUILD_ENABLED; then
    echo "Building xkeyboard-config $XKB_CONFIG_VERSION..."
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
    meson setup --prefix=$INSTALL_PREFIX \
        -Ddefault_library=static \
        ..
    ninja
fi

if $INSTALL_ENABLED; then
    echo "Installing xkeyboard-config $XKB_CONFIG_VERSION..."
    cd "$BUILD_DIR"/xkeyboard-config/build
    sudo ninja install
fi
cd "$BUILD_DIR"

# Build or Install xorg-macros
if $BUILD_ENABLED; then
    echo "Installing xorg-macros $XORG_MACROS_VERSION..."
    if [ ! -d "xorg-macros" ]; then
        curl -L -o xorg-macros.tar.bz2 "https://www.x.org/releases/individual/util/util-macros-${XORG_MACROS_VERSION}.tar.bz2"
        tar xf xorg-macros.tar.bz2
        mv "util-macros-${XORG_MACROS_VERSION}" xorg-macros
        rm xorg-macros.tar.bz2
    fi

    cd xorg-macros
    # xorg-macros does not need to be built; it just needs to be installed
    ./configure --prefix=$INSTALL_PREFIX
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing xorg-macros $XORG_MACROS_VERSION..."
    cd "$BUILD_DIR"/xorg-macros
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install xproto
if $BUILD_ENABLED; then
    echo "Building xproto $XPROTO_VERSION from source..."
    if [ ! -d "xproto" ]; then
        curl -L -o xproto.tar.bz2 "https://www.x.org/releases/individual/proto/xproto-${XPROTO_VERSION}.tar.bz2"
        tar xf xproto.tar.bz2
        mv "xproto-${XPROTO_VERSION}" xproto
        rm xproto.tar.bz2
    fi

    cd xproto
    ./configure --prefix=$INSTALL_PREFIX
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing xproto $XPROTO_VERSION..."
    cd "$BUILD_DIR"/xproto
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install libXau first
if $BUILD_ENABLED; then
    echo "Building libXau from source..."
    if [ ! -d "libXau" ]; then
        curl -L -o libXau.tar.xz "https://www.x.org/releases/individual/lib/libXau-${XAU_VERSION}.tar.xz"
        tar xf libXau.tar.xz
        mv "libXau-${XAU_VERSION}" libXau
        rm libXau.tar.xz
    fi

    cd libXau
    CFLAGS="-fPIC" ./configure --prefix=$INSTALL_PREFIX \
        --enable-static \
        --disable-shared
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing libXau $XAU_VERSION..."
    cd "$BUILD_DIR"/libXau
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install xorgproto (provides core X11 headers)
if $BUILD_ENABLED; then
    echo "Building xorgproto from source..."
    if [ ! -d "xorgproto" ]; then
        curl -L -o xorgproto.tar.xz "https://www.x.org/releases/individual/proto/xorgproto-${XORGPROTO_VERSION}.tar.xz"
        tar xf xorgproto.tar.xz
        mv "xorgproto-${XORGPROTO_VERSION}" xorgproto
        rm xorgproto.tar.xz
    fi

    cd xorgproto
    CFLAGS="-fPIC" ./configure --prefix=$INSTALL_PREFIX \
        --enable-static \
        --disable-shared
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing xorgproto $XORGPROTO_VERSION..."
    cd "$BUILD_DIR"/xorgproto
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install xtrans
if $BUILD_ENABLED; then
    echo "Building xtrans from source..."
    if [ ! -d "xtrans" ]; then
        curl -L -o xtrans.tar.xz "https://www.x.org/releases/individual/lib/xtrans-${XTRANS_VERSION}.tar.xz"
        tar xf xtrans.tar.xz
        mv "xtrans-${XTRANS_VERSION}" xtrans
        rm xtrans.tar.xz
    fi

    cd xtrans
    CFLAGS="-fPIC" ./configure --prefix=$INSTALL_PREFIX
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing xtrans $XTRANS_VERSION..."
    cd "$BUILD_DIR"/xtrans
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install xcb-proto first
if $BUILD_ENABLED; then
    echo "Building xcb-proto..."
    if [ ! -d "xcb-proto" ]; then
        curl -L -o xcb-proto.tar.xz "https://xorg.freedesktop.org/archive/individual/proto/xcb-proto-${XCB_PROTO_VERSION}.tar.xz"
        tar xf xcb-proto.tar.xz
        mv "xcb-proto-${XCB_PROTO_VERSION}" xcb-proto
        rm xcb-proto.tar.xz
    fi

    cd xcb-proto
    CFLAGS="-fPIC" ./configure --prefix=$INSTALL_PREFIX
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing xcb-proto $XCB_PROTO_VERSION..."
    cd "$BUILD_DIR"/xcb-proto
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install libxcb
if $BUILD_ENABLED; then
    echo "Building libxcb..."
    if [ ! -d "libxcb" ]; then
        curl -L -o libxcb.tar.xz "https://xorg.freedesktop.org/archive/individual/lib/libxcb-${XCB_VERSION}.tar.xz"
        tar xf libxcb.tar.xz
        mv "libxcb-${XCB_VERSION}" libxcb
        rm libxcb.tar.xz
    fi

    cd libxcb
    CFLAGS="-fPIC" ./configure --prefix=$INSTALL_PREFIX \
        --enable-static \
        --disable-shared \
        --enable-xinput \
        --enable-xkb \
        --enable-xcb-xinput
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing libxcb $XCB_VERSION..."
    cd "$BUILD_DIR"/libxcb
    sudo make install
fi
cd "$BUILD_DIR"

# Now continue with libX11 build
if $BUILD_ENABLED; then
    echo "Building libX11 from source..."
    if [ ! -d "libX11" ]; then
        curl -L -o libX11.tar.xz "https://www.x.org/releases/individual/lib/libX11-${X11_VERSION}.tar.xz"
        tar xf libX11.tar.xz
        mv "libX11-${X11_VERSION}" libX11
        rm libX11.tar.xz
    fi

    cd libX11
    CFLAGS="-fPIC" \
    ./configure --prefix=$INSTALL_PREFIX \
        --enable-static \
        --disable-shared \
        --disable-specs \
        --disable-devel-docs \
        --without-xmlto \
        --without-fop
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing libX11 $X11_VERSION..."
    cd "$BUILD_DIR"/libX11
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install libXrender
if $BUILD_ENABLED; then
    echo "Building libXrender from source..."
    if [ ! -d "libXrender" ]; then
        curl -L -o libXrender.tar.gz "https://www.x.org/releases/individual/lib/libXrender-${XRENDER_VERSION}.tar.gz"
        tar xf libXrender.tar.gz
        mv "libXrender-${XRENDER_VERSION}" libXrender
        rm libXrender.tar.gz
    fi

    cd libXrender
    CFLAGS="-fPIC" PKG_CONFIG_PATH="$INSTALL_PREFIX/lib/pkgconfig:$INSTALL_PREFIX/local/lib/pkgconfig:$INSTALL_PREFIX/share/pkgconfig" \
    ./configure --prefix=$INSTALL_PREFIX \
        --enable-static \
        --disable-shared
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing libXrender $XRENDER_VERSION..."
    cd "$BUILD_DIR"/libXrender
    sudo make install
    sudo ldconfig
fi
cd "$BUILD_DIR"

# Build or Install libXext
if $BUILD_ENABLED; then
    echo "Building libXext from source..."
    if [ ! -d "libXext" ]; then
        curl -L -o libXext.tar.xz "https://www.x.org/releases/individual/lib/libXext-${XEXT_VERSION}.tar.xz"
        tar xf libXext.tar.xz
        mv "libXext-${XEXT_VERSION}" libXext
        rm libXext.tar.xz
    fi

    cd libXext
    CFLAGS="-fPIC" ./configure --prefix=$INSTALL_PREFIX \
        --enable-static \
        --disable-shared \
        --disable-specs \
        --without-xmlto \
        --without-fop
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing libXext $XEXT_VERSION..."
    cd "$BUILD_DIR"/libXext
    sudo make install
fi
cd "$BUILD_DIR"

# Now build libXrandr with explicit paths
if $BUILD_ENABLED; then
    echo "Building libXrandr from source..."

    if [ ! -d "libXrandr" ]; then
        curl -L -o libXrandr.tar.gz "https://www.x.org/releases/individual/lib/libXrandr-${XRANDR_VERSION}.tar.gz"
        tar xf libXrandr.tar.gz
        mv "libXrandr-${XRANDR_VERSION}" libXrandr
        rm libXrandr.tar.gz
    fi

    cd libXrandr
    CPPFLAGS="-I$INSTALL_PREFIX/include" \
    CFLAGS="-fPIC -I$INSTALL_PREFIX/include" \
    LDFLAGS="-L$INSTALL_PREFIX/lib" \
    RANDR_CFLAGS="-I$INSTALL_PREFIX/include" \
    RANDR_LIBS="-L$INSTALL_PREFIX/lib -lX11 -lXext -lXrender" \
    ./configure --prefix=$INSTALL_PREFIX \
        --enable-static \
        --disable-shared \
        --with-pic
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing libXrandr $XRANDR_VERSION..."
    cd "$BUILD_DIR"/libXrandr
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install xcb-util
if $BUILD_ENABLED; then
    echo "Building xcb-util $XCB_UTIL_VERSION from source..."
    if [ ! -d "xcb-util" ]; then
        curl -L -o xcb-util.tar.gz "https://xcb.freedesktop.org/dist/xcb-util-${XCB_UTIL_VERSION}.tar.gz"
        tar xf xcb-util.tar.gz
        mv "xcb-util-${XCB_UTIL_VERSION}" xcb-util
        rm xcb-util.tar.gz
    fi

    cd xcb-util
    CFLAGS="-fPIC" ./configure --prefix=$INSTALL_PREFIX --enable-static --disable-shared
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing xcb-util $XCB_UTIL_VERSION..."
    cd "$BUILD_DIR"/xcb-util
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install xcb-util-wm
if $BUILD_ENABLED; then
    echo "Building xcb-util-wm $XCB_UTIL_WM_VERSION from source..."
    if [ ! -d "xcb-util-wm" ]; then
        curl -L -o xcb-util-wm.tar.gz "https://xcb.freedesktop.org/dist/xcb-util-wm-${XCB_UTIL_WM_VERSION}.tar.gz"
        tar xf xcb-util-wm.tar.gz
        mv "xcb-util-wm-${XCB_UTIL_WM_VERSION}" xcb-util-wm
        rm xcb-util-wm.tar.gz
    fi

    cd xcb-util-wm
    CFLAGS="-fPIC" ./configure --prefix=$INSTALL_PREFIX --enable-static --disable-shared
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing xcb-util-wm $XCB_UTIL_WM_VERSION..."
    cd "$BUILD_DIR"/xcb-util-wm
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install xcb-util-keysyms
if $BUILD_ENABLED; then
    echo "Building xcb-util-keysyms $XCB_UTIL_KEYSYMS_VERSION from source..."
    if [ ! -d "xcb-util-keysyms" ]; then
        curl -L -o xcb-util-keysyms.tar.gz "https://xcb.freedesktop.org/dist/xcb-util-keysyms-${XCB_UTIL_KEYSYMS_VERSION}.tar.gz"
        tar xf xcb-util-keysyms.tar.gz
        mv "xcb-util-keysyms-${XCB_UTIL_KEYSYMS_VERSION}" xcb-util-keysyms
        rm xcb-util-keysyms.tar.gz
    fi

    cd xcb-util-keysyms
    CFLAGS="-fPIC" ./configure --prefix=$INSTALL_PREFIX --enable-static --disable-shared
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing xcb-util-keysyms $XCB_UTIL_KEYSYMS_VERSION..."
    cd "$BUILD_DIR"/xcb-util-keysyms
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install xcb-util-image
if $BUILD_ENABLED; then
    echo "Building xcb-util-image from source..."
    if [ ! -d "xcb-util-image" ]; then
        curl -L -o xcb-util-image.tar.gz "https://xcb.freedesktop.org/dist/xcb-util-image-0.4.1.tar.gz"
        tar xf xcb-util-image.tar.gz
        mv "xcb-util-image-0.4.1" xcb-util-image
        rm xcb-util-image.tar.gz
    fi

    cd xcb-util-image
    CFLAGS="-fPIC" ./configure --prefix=$INSTALL_PREFIX --enable-static --disable-shared
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing xcb-util-image..."
    cd "$BUILD_DIR"/xcb-util-image
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install xcb-util-renderutil
if $BUILD_ENABLED; then
    echo "Building xcb-util-renderutil from source..."
    if [ ! -d "xcb-util-renderutil" ]; then
        curl -L -o xcb-util-renderutil.tar.gz "https://xcb.freedesktop.org/dist/xcb-util-renderutil-0.3.10.tar.gz"
        tar xf xcb-util-renderutil.tar.gz
        mv "xcb-util-renderutil-0.3.10" xcb-util-renderutil
        rm xcb-util-renderutil.tar.gz
    fi

    cd xcb-util-renderutil
    CFLAGS="-fPIC" ./configure --prefix=$INSTALL_PREFIX --enable-static --disable-shared
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing xcb-util-renderutil..."
    cd "$BUILD_DIR"/xcb-util-renderutil
    sudo make install
fi
cd "$BUILD_DIR"

# Build or Install xcb-util-cursor
if $BUILD_ENABLED; then
    echo "Building xcb-util-cursor $XCB_CURSOR_VERSION from source..."
    if [ ! -d "xcb-util-cursor" ]; then
        curl -L -o xcb-util-cursor.tar.gz "https://xcb.freedesktop.org/dist/xcb-util-cursor-${XCB_CURSOR_VERSION}.tar.gz"
        tar xf xcb-util-cursor.tar.gz
        mv "xcb-util-cursor-${XCB_CURSOR_VERSION}" xcb-util-cursor
        rm xcb-util-cursor.tar.gz
    fi

    cd xcb-util-cursor
    CFLAGS="-fPIC" ./configure --prefix=$INSTALL_PREFIX --enable-static --disable-shared
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing xcb-util-cursor $XCB_CURSOR_VERSION..."
    cd "$BUILD_DIR"/xcb-util-cursor
    sudo make install
fi
cd "$BUILD_DIR"

# Verify pkg-config files are installed
echo "Verifying pkg-config files..."
pkg-config --exists x11 && echo "x11.pc found" || echo "x11.pc not found"
pkg-config --exists xext && echo "xext.pc found" || echo "xext.pc not found"
pkg-config --exists xrender && echo "xrender.pc found" || echo "xrender.pc not found"
pkg-config --exists xcb-image && echo "xcb-image.pc found" || echo "xcb-image.pc not found"
pkg-config --exists xcb-cursor && echo "xcb-cursor.pc found" || echo "xcb-cursor.pc not found"
pkg-config --exists xcb-renderutil && echo "xcb-renderutil.pc found" || echo "xcb-renderutil.pc not found"

# Build libXdmcp
if $BUILD_ENABLED; then
    echo "Building libXdmcp $LIBXDMCP_VERSION from source..."
    if [ ! -d "libXdmcp" ]; then
        curl -L -o libXdmcp.tar.gz "https://www.x.org/releases/individual/lib/libXdmcp-${LIBXDMCP_VERSION}.tar.gz"
        tar xf libXdmcp.tar.gz
        mv "libXdmcp-${LIBXDMCP_VERSION}" libXdmcp
        rm libXdmcp.tar.gz
    fi

    cd libXdmcp
    CFLAGS="-fPIC" ./configure --prefix=$INSTALL_PREFIX --enable-static --disable-shared
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing libXdmcp $LIBXDMCP_VERSION..."
    cd "$BUILD_DIR"/libXdmcp
    sudo make install
fi
cd "$BUILD_DIR"

# Build libxkbcommon
if $BUILD_ENABLED; then
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
     
     
        sed -i "/xcb_xkb_dep,/axau_dep," "$LIBXKBCOMMON_MESON_FILE"
        sed -i "/xau_dep,/axdmcp_dep," "$LIBXKBCOMMON_MESON_FILE"

    else
        echo "Error: $LIBXKBCOMMON_MESON_FILE not found."
    fi
     
    meson setup --prefix=$INSTALL_PREFIX \
        -Denable-docs=false \
        -Denable-wayland=false \
        -Denable-x11=true \
        -Ddefault_library=static \
        -Dxkb-config-root=$INSTALL_PREFIX/share/X11/xkb \
        -Dx-locale-root=$INSTALL_PREFIX/share/X11/locale \
        ..
    ninja
fi

if $INSTALL_ENABLED; then
    echo "Installing libxkbcommon $XKBCOMMON_VERSION..."
    cd "$BUILD_DIR"/libxkbcommon/build
    sudo ninja install
fi
cd "$BUILD_DIR"


# At the end, export the PKG_CONFIG_PATH
export PKG_CONFIG_PATH="$INSTALL_PREFIX/lib/pkgconfig:$INSTALL_PREFIX/local/lib/pkgconfig:$INSTALL_PREFIX/share/pkgconfig"

echo "All Qt dependencies have been processed successfully"