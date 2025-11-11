#!/bin/bash

set -uo pipefail
IFS=$'\n\t'

# Disable exit-on-error for non-critical operations
# Re-enable with "set -e" when needed
ORIGINAL_OPTS="$-"

APPIMAGE_DIR="${BUILD_DIR}/appimage"

# Determine architecture
ARCH=$(uname -m)
case "${ARCH}" in
    aarch64|arm64) APPIMAGE_ARCH=aarch64;;
    x86_64|amd64) APPIMAGE_ARCH=x86_64;;
    *) APPIMAGE_ARCH=${ARCH};;
esac

# Create build directory
mkdir -p "${BUILD_DIR}" "${APPIMAGE_DIR}"

# Essential GStreamer plugins for video capture
GSTREAMER_PLUGINS=(
    "libgstvideo4linux2.so"        # V4L2 video capture (CRITICAL)
    "libgstv4l2codecs.so"          # V4L2 hardware codecs
    "libgstvideoconvertscale.so"   # Video format conversion and scaling
    "libgstvideorate.so"           # Video frame rate conversion
    "libgstcoreelements.so"        # Core elements (queue, filesrc, etc.)
    "libgsttypefindfunctions.so"   # Type detection
    "libgstapp.so"                 # Application integration
    "libgstplayback.so"           # Playback elements
    "libgstjpeg.so"               # JPEG codec
    "libgstximagesink.so"         # X11 video sink
    "libgstxvimagesink.so"        # XVideo sink
    "libgstautodetect.so"         # Auto detection
    "libgstpulseaudio.so"         # PulseAudio
    "libgstaudioparsers.so"       # Audio parsers
    "libgstaudioconvert.so"       # Audio conversion
    "libgstaudioresample.so"      # Audio resampling
)

echo "Detecting GStreamer plugin directories..."

# Detect GStreamer plugins in container
GSTREAMER_HOST_DIRS=(
	"/opt/gstreamer/lib/${ARCH}-linux-gnu/gstreamer-1.0"
	"/opt/gstreamer/lib/${ARCH}-linux-gnu/"
    "/usr/lib/${ARCH}-linux-gnu/gstreamer-1.0"
    "/usr/lib/gstreamer-1.0"
)

GSTREAMER_HOST_DIR=""
for dir in "${GSTREAMER_HOST_DIRS[@]}"; do
    if [ -d "$dir" ]; then
        GSTREAMER_HOST_DIR="$dir"
        echo "✅ Found GStreamer plugin directory: $GSTREAMER_HOST_DIR"
        break
    fi
done

if [ -z "$GSTREAMER_HOST_DIR" ] || [ ! -f "$GSTREAMER_HOST_DIR/libgstvideo4linux2.so" ]; then
    echo "❌ GStreamer V4L2 plugin not found in container!"
    echo "Installing GStreamer plugins in container..."
    apt update && apt install -y gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad
    
    # Retry detection after installation
    for dir in "${GSTREAMER_HOST_DIRS[@]}"; do
        if [ -d "$dir" ] && [ -f "$dir/libgstvideo4linux2.so" ]; then
            GSTREAMER_HOST_DIR="$dir"
            echo "✅ Found GStreamer plugins after installation: $GSTREAMER_HOST_DIR"
            break
        fi
    done
    
    if [ -z "$GSTREAMER_HOST_DIR" ]; then
        echo "❌ Still no GStreamer plugins found after installation!"
        exit 1
    fi
fi

# Determine version from resources/version.h
VERSION_H="/workspace/src/resources/version.h"
if [ -f "${VERSION_H}" ]; then
    VERSION=$(grep '^#define APP_VERSION' "${VERSION_H}" | grep -oE '[0-9]+(\.[0-9]+)*' | head -n1)
fi
if [ -z "${VERSION}" ]; then
    VERSION="0.4.3.248"
fi

echo "Building AppImage version: ${VERSION}"

echo "🎯 Creating AppImage with essential GStreamer plugins..."

# Create enhanced AppImage
cd /workspace
rm -rf appimage/AppDir
mkdir -p appimage/AppDir/usr/bin appimage/AppDir/usr/lib/gstreamer-1.0 appimage/AppDir/usr/share/applications

echo '✅ Executable copied'
cp build/openterfaceQT appimage/AppDir/usr/bin/
chmod +x appimage/AppDir/usr/bin/openterfaceQT

echo '📦 Copying critical GLIBC libraries for compatibility...'
# CRITICAL: Copy libc.so.6 and related glibc libraries from the build environment
# This ensures the AppImage can run on systems with older GLIBC versions
GLIBC_LIBS=(
    "libc.so.6"
    "libm.so.6"
    "libpthread.so.0"
    "libdl.so.2"
    "librt.so.1"
)

for lib in "${GLIBC_LIBS[@]}"; do
    for SEARCH_DIR in /lib/x86_64-linux-gnu /lib64 /lib /usr/lib/x86_64-linux-gnu /usr/lib; do
        if [ -f "$SEARCH_DIR/$lib" ]; then
            dest_file="appimage/AppDir/usr/lib/$(basename "$lib")"
            if [ ! -e "$dest_file" ]; then
                echo "  ✓ Copying: $lib"
                cp -P "$SEARCH_DIR/$lib" "appimage/AppDir/usr/lib/" || true
            fi
            break
        fi
    done
done
echo '✅ GLIBC libraries copied'

echo '📦 Copying essential GStreamer plugins...'
COPIED_COUNT=0
for plugin in "${GSTREAMER_PLUGINS[@]}"; do
	if [ -f "$GSTREAMER_HOST_DIR/$plugin" ]; then
		echo "✅ Included $plugin"
		cp "$GSTREAMER_HOST_DIR/$plugin" "appimage/AppDir/usr/lib/gstreamer-1.0/" || true
		chmod +x "appimage/AppDir/usr/lib/gstreamer-1.0/$plugin" || true
		COPIED_COUNT=$((COPIED_COUNT + 1))
	else
		echo "⚠️ Missing $plugin"
	fi
done
echo "📦 Copied $COPIED_COUNT essential GStreamer plugins"

# Copy dependencies of GStreamer plugins
echo "📦 Copying dependencies for GStreamer plugins..."
mkdir -p "appimage/AppDir/usr/lib"
for plugin in "${GSTREAMER_PLUGINS[@]}"; do
	if [ -f "appimage/AppDir/usr/lib/gstreamer-1.0/$plugin" ]; then
		echo "Checking dependencies for $plugin"
		ldd "appimage/AppDir/usr/lib/gstreamer-1.0/$plugin" 2>/dev/null | grep -v "linux-vdso" | grep -v "ld-linux" | awk '{print $3}' | while read -r dep; do
			if [ -f "$dep" ] && [[ "$dep" == /usr/lib/* || "$dep" == /lib/* ]] && [ ! -f "appimage/AppDir/usr/lib/$(basename "$dep")" ]; then
				echo "  Copying dependency: $(basename "$dep")"
				cp "$dep" "appimage/AppDir/usr/lib/" || true
			fi
		done || true
	fi
done
echo "✅ GStreamer plugin dependencies copied"

# Copy critical system libraries that must be bundled to avoid GLIBC conflicts
echo "📦 Copying critical system libraries (libusb, libdrm, libudev)..."
mkdir -p "appimage/AppDir/usr/lib"

# Debug: Show what libusb files exist in the system
echo "  🔍 Searching for libusb in system..."
LIBUSB_FOUND=$(find /opt/ffmpeg/lib /opt /usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib -name "libusb*" 2>/dev/null | head -10)
if [ -n "$LIBUSB_FOUND" ]; then
	echo "    Found libusb files:"
	echo "$LIBUSB_FOUND" | while read -r lib; do
		echo "      - $lib"
	done
else
	echo "    ⚠️  No libusb files found in system paths!"
	echo "    Checking if libusb package is installed..."
	dpkg -l | grep -i libusb || echo "    libusb not installed, attempting to install..."
	apt-get update -qq && apt-get install -y libusb-1.0-0 libusb-1.0-0-dev 2>&1 | tail -5 || true
fi
echo ""

CRITICAL_LIBS=(
    "libusb-1.0.so*"
    "libusb-1.0-0*"
    "libusb.so*"
    "libdrm.so*"
    "libudev.so*"
)

for pattern in "${CRITICAL_LIBS[@]}"; do
    # Search in build environment library paths (these have compatible GLIBC)
    FOUND=0
    for SEARCH_DIR in /opt/ffmpeg/lib /opt /usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib; do
        if [ -d "$SEARCH_DIR" ]; then
            # Use find with -print0 and xargs for better handling
            found_count=$(find "$SEARCH_DIR" -maxdepth 1 -name "$pattern" 2>/dev/null | wc -l)
            if [ "$found_count" -gt 0 ]; then
                echo "  Found $found_count match(es) for pattern '$pattern' in $SEARCH_DIR"
                find "$SEARCH_DIR" -maxdepth 1 -name "$pattern" 2>/dev/null | while IFS= read -r file; do
                    if [ -f "$file" ] || [ -L "$file" ]; then
                        dest_file="appimage/AppDir/usr/lib/$(basename "$file")"
                        if [ ! -e "$dest_file" ]; then
                            echo "    ✓ Copying: $(basename "$file")"
                            cp -P "$file" "appimage/AppDir/usr/lib/" 2>&1 || {
                                echo "    ⚠️  Warning: Failed to copy $file"
                            }
                        else
                            echo "    ✓ Already exists: $(basename "$file")"
                        fi
                    fi
                done
                FOUND=1
                break  # Found in this directory, stop searching for this pattern
            fi
        fi
    done
    if [ $FOUND -eq 0 ]; then
        echo "  ⏭️  Pattern not found: $pattern (skipping)"
    fi
done
echo "✅ Critical system libraries copying completed"

# Copy JPEG libraries for image codec support (required by Qt and FFmpeg)
echo "📦 Copying JPEG libraries for image codec support..."
mkdir -p "appimage/AppDir/usr/lib"
JPEG_COPIED=0
for SEARCH_DIR in /opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib; do
    if [ -d "$SEARCH_DIR" ]; then
        # Copy libjpeg libraries (both files and symlinks)
        LIBJPEG_COUNT=$(find "$SEARCH_DIR" -maxdepth 1 \( -name "libjpeg.so*" -type f -o -name "libjpeg.so*" -type l \) 2>/dev/null | wc -l)
        if [ "$LIBJPEG_COUNT" -gt 0 ]; then
            echo "  ✅ Found libjpeg in $SEARCH_DIR"
            find "$SEARCH_DIR" -maxdepth 1 \( -name "libjpeg.so*" -type f -o -name "libjpeg.so*" -type l \) 2>/dev/null | while read -r file; do
                if [ ! -f "appimage/AppDir/usr/lib/$(basename "$file")" ]; then
                    echo "    Copying: $(basename "$file")"
                    cp -a "$file" "appimage/AppDir/usr/lib/"
                fi
            done
        fi
        # Copy libturbojpeg libraries (both files and symlinks)
        LIBTURBOJPEG_COUNT=$(find "$SEARCH_DIR" -maxdepth 1 \( -name "libturbojpeg.so*" -type f -o -name "libturbojpeg.so*" -type l \) 2>/dev/null | wc -l)
        if [ "$LIBTURBOJPEG_COUNT" -gt 0 ]; then
            echo "  ✅ Found libturbojpeg in $SEARCH_DIR"
            find "$SEARCH_DIR" -maxdepth 1 \( -name "libturbojpeg.so*" -type f -o -name "libturbojpeg.so*" -type l \) 2>/dev/null | while read -r file; do
                if [ ! -f "appimage/AppDir/usr/lib/$(basename "$file")" ]; then
                    echo "    Copying: $(basename "$file")"
                    cp -a "$file" "appimage/AppDir/usr/lib/"
                fi
            done
        fi
    fi
done
echo "✅ JPEG libraries processed for AppImage"

# Copy EGL and GPU rendering libraries (required for Qt GUI rendering)
echo "📦 Copying EGL and GPU rendering libraries for GUI support..."
mkdir -p "appimage/AppDir/usr/lib"
EGL_LIBS=(
    "libEGL.so.1"
    "libEGL.so"
    "libGLESv2.so.2"
    "libGLESv2.so"
    "libGL.so.1"
    "libGL.so"
    "libGLX.so.0"
    "libGLX.so"
    "libglvnd.so.0"
    "libglvnd_pthread.so.0"
    "libglvnd_dl.so.0"
)

# Track which libraries were found
declare -A EGL_FOUND

# Search in multiple directories
for SEARCH_DIR in /opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib /usr/lib64 /lib/x86_64-linux-gnu /lib; do
    if [ -d "$SEARCH_DIR" ]; then
        for lib in "${EGL_LIBS[@]}"; do
            # Skip if already found
            if [ "${EGL_FOUND[$lib]:-}" = "1" ]; then
                continue
            fi
            
            # Search for the library (including wildcards for versioned libs)
            found_files=$(find "$SEARCH_DIR" -maxdepth 1 \( -name "$lib" -o -name "${lib}.*" \) 2>/dev/null || true)
            
            if [ -n "$found_files" ]; then
                echo "$found_files" | while read -r file; do
                    if [ -f "$file" ] || [ -L "$file" ]; then
                        dest_file="appimage/AppDir/usr/lib/$(basename "$file")"
                        if [ ! -e "$dest_file" ]; then
                            echo "  ✓ Copying: $(basename "$file") from $SEARCH_DIR"
                            cp -P "$file" "appimage/AppDir/usr/lib/" 2>&1 || {
                                echo "  ⚠️  Warning: Failed to copy $file"
                            }
                        fi
                    fi
                done
                EGL_FOUND[$lib]=1
            fi
        done
    fi
done

# Verify critical EGL libraries were copied
echo "📊 Verifying EGL library installation..."
MISSING_LIBS=()
for lib in "libEGL.so.1" "libGL.so.1" "libGLX.so.0"; do
    if [ ! -e "appimage/AppDir/usr/lib/$lib" ]; then
        MISSING_LIBS+=("$lib")
        echo "  ⚠️  Missing critical library: $lib"
    else
        echo "  ✓ Found: $lib"
    fi
done

if [ ${#MISSING_LIBS[@]} -gt 0 ]; then
    echo "⚠️  WARNING: Some critical EGL libraries are missing!"
    echo "    Attempting to install mesa-libEGL..."
    
    # Try to install EGL libraries if missing
    if command -v apt-get >/dev/null 2>&1; then
        apt-get update -qq && apt-get install -y libgl1 libegl1 libglx0 libglvnd0 2>/dev/null || true
        
        # Retry copying after installation
        for lib in "${MISSING_LIBS[@]}"; do
            for SEARCH_DIR in /usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib; do
                if [ -f "$SEARCH_DIR/$lib" ]; then
                    echo "  ✓ Found and copying: $lib from $SEARCH_DIR"
                    cp -P "$SEARCH_DIR/$lib" "appimage/AppDir/usr/lib/"
                    break
                fi
            done
        done
    fi
fi

echo "✅ EGL and GPU rendering libraries processed for AppImage"

# Try to find and copy icon
mkdir -p appimage/AppDir/usr/share/pixmaps
mkdir -p appimage/AppDir/usr/share/icons/hicolor/256x256/apps

ICON_FOUND=false
for icon_path in \
    '/workspace/src/images/icon_256.png' \
    '/workspace/src/images/icon_128.png' \
    '/workspace/src/resources/icons/openterfaceQT.png' \
    '/workspace/src/icons/openterface.png'; do
    if [ -f "$icon_path" ]; then
        cp "$icon_path" "appimage/AppDir/usr/share/icons/hicolor/256x256/apps/openterfaceqt.png"
        cp "$icon_path" "appimage/AppDir/usr/share/pixmaps/openterfaceqt.png"
        echo "✅ Icon copied from $icon_path"
        ICON_FOUND=true
        break
    fi
done

if [ "$ICON_FOUND" = false ]; then
    echo '⚠️ No icon found, creating placeholder...'
    # Create a minimal PNG placeholder using ImageMagick if available
    convert -size 64x64 xc:blue appimage/AppDir/usr/share/pixmaps/openterfaceqt.png 2>/dev/null || {
        echo 'No ImageMagick, using text placeholder...'
        echo 'OpenterfaceQT Icon' > appimage/AppDir/usr/share/pixmaps/openterfaceqt.txt
    }
fi

# Copy icon to root  
cp appimage/AppDir/usr/share/pixmaps/openterfaceqt.png appimage/AppDir/ 2>/dev/null || true

# Continue to comprehensive AppImage creation section with Docker runtime support
# (This section was simplified and moved to the end of the script for proper runtime handling)
echo "📦 Copied $COPIED_COUNT essential GStreamer plugins"
echo "✅ Proceeding to comprehensive AppImage creation with Docker runtime support"
cd /workspace

# Determine linuxdeploy architecture tag from Debian arch
case "${ARCH}" in
	amd64|x86_64) APPIMAGE_ARCH=x86_64;;
	arm64|aarch64) APPIMAGE_ARCH=aarch64;;
	armhf|armv7l) APPIMAGE_ARCH=armhf;;
	*) echo "Warning: unknown arch '${ARCH}', defaulting to x86_64"; APPIMAGE_ARCH=x86_64;;
esac

# Set up variables for comprehensive AppImage creation
SRC="/workspace/src"
BUILD="/workspace/build" 
APPDIR="${APPIMAGE_DIR}/AppDir"
DESKTOP_OUT="${APPDIR}/openterfaceqt.desktop"
APPIMAGE_OUT="${BUILD}"

# Create comprehensive AppDir structure for the final AppImage
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin" "${APPDIR}/usr/lib" "${APPDIR}/usr/share/applications" "${APPDIR}/usr/share/pixmaps"

# Copy the executable to the comprehensive AppDir
cp "${BUILD}/openterfaceQT" "${APPDIR}/usr/bin/"
chmod +x "${APPDIR}/usr/bin/openterfaceQT"

echo "✅ Setting up comprehensive AppImage structure with Docker runtime support"

# Create desktop file for comprehensive AppImage
cat > "${APPDIR}/usr/share/applications/openterfaceqt.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=OpenterfaceQT
Comment=Openterface Mini-KVM Host Application  
Exec=openterfaceQT
Icon=openterfaceqt
Categories=Utility;
StartupNotify=true
Terminal=false
EOF

# Copy desktop file to root of AppDir
cp "${APPDIR}/usr/share/applications/openterfaceqt.desktop" "${DESKTOP_OUT}"

# Copy GStreamer plugins to comprehensive AppImage
echo "Including GStreamer plugins for video capture in comprehensive AppImage..."
mkdir -p "${APPDIR}/usr/lib/gstreamer-1.0"
for plugin in "${GSTREAMER_PLUGINS[@]}"; do
	if [ -f "$GSTREAMER_HOST_DIR/$plugin" ]; then
		cp "$GSTREAMER_HOST_DIR/$plugin" "${APPDIR}/usr/lib/gstreamer-1.0/"
		echo "✓ Copied plugin: ${plugin}"
	fi
done

# Copy critical system libraries that must be bundled to avoid GLIBC conflicts
echo "Including critical system libraries (libusb, libdrm, libudev) in comprehensive AppImage..."
mkdir -p "${APPDIR}/usr/lib"

# Debug: Show what libusb files exist in the system
echo "  🔍 Searching for libusb in system..."
LIBUSB_FOUND_COMP=$(find /opt/ffmpeg/lib /opt /usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib -name "libusb*" 2>/dev/null | head -10)
if [ -n "$LIBUSB_FOUND_COMP" ]; then
	echo "    Found libusb files:"
	echo "$LIBUSB_FOUND_COMP" | while read -r lib; do
		echo "      - $lib"
	done
else
	echo "    ⚠️  No libusb files found in system paths!"
	echo "    Checking if libusb package is installed..."
	dpkg -l | grep -i libusb || echo "    libusb not installed, attempting to install..."
	apt-get update -qq && apt-get install -y libusb-1.0-0 libusb-1.0-0-dev 2>&1 | tail -5 || true
fi
echo ""

CRITICAL_LIBS=(
    "libusb-1.0.so*"
    "libusb-1.0-0*"
    "libusb.so*"
    "libdrm.so*"
    "libudev.so*"
)

for pattern in "${CRITICAL_LIBS[@]}"; do
    # Search in build environment library paths (these have compatible GLIBC)
    FOUND_CRITICAL=0
    for SEARCH_DIR in /opt/ffmpeg/lib /opt /usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib; do
        if [ -d "$SEARCH_DIR" ]; then
            # Count matches first
            found_count=$(find "$SEARCH_DIR" -maxdepth 1 -name "$pattern" 2>/dev/null | wc -l)
            if [ "$found_count" -gt 0 ]; then
                echo "  Found $found_count match(es) for pattern '$pattern' in $SEARCH_DIR"
                find "$SEARCH_DIR" -maxdepth 1 -name "$pattern" 2>/dev/null | while IFS= read -r file; do
                    if [ -f "$file" ] || [ -L "$file" ]; then
                        dest_file="${APPDIR}/usr/lib/$(basename "$file")"
                        if [ ! -e "$dest_file" ]; then
                            echo "    ✓ Copying: $(basename "$file")"
                            cp -P "$file" "${APPDIR}/usr/lib/" 2>&1 || {
                                echo "    ⚠️  Warning: Failed to copy $file"
                            }
                        else
                            echo "    ✓ Already exists: $(basename "$file")"
                        fi
                    fi
                done
                FOUND_CRITICAL=1
                break  # Found in this directory, stop searching for this pattern
            fi
        fi
    done
    if [ $FOUND_CRITICAL -eq 0 ]; then
        echo "  ⏭️  Pattern not found in system: $pattern (skipping)"
    fi
done
echo "✅ Critical system libraries copied to comprehensive AppImage"

# Copy critical GLIBC libraries to ensure compatibility across systems
echo "📦 Copying critical GLIBC libraries for broader compatibility..."
mkdir -p "${APPDIR}/usr/lib"

# CRITICAL: Copy libc.so.6 and related glibc libraries from the build environment
# This ensures the AppImage can run on systems with older GLIBC versions
# by providing the exact version that all bundled libraries were compiled against
GLIBC_LIBS=(
    "libc.so.6"
    "libm.so.6"
    "libpthread.so.0"
    "libdl.so.2"
    "librt.so.1"
    "libnss_compat.so.2"
    "libnss_files.so.2"
    "libnss_dns.so.2"
    "libresolv.so.2"
    "libcrypt.so.1"
    "libutil.so.1"
    "ld-linux-x86-64.so.2"
)

echo "  🔍 Locating glibc libraries..."
for lib in "${GLIBC_LIBS[@]}"; do
    for SEARCH_DIR in /lib/x86_64-linux-gnu /lib64 /lib /usr/lib/x86_64-linux-gnu /usr/lib; do
        if [ -f "$SEARCH_DIR/$lib" ]; then
            dest_file="${APPDIR}/usr/lib/$(basename "$lib")"
            if [ ! -e "$dest_file" ]; then
                echo "  ✓ Copying: $lib from $SEARCH_DIR"
                cp -P "$SEARCH_DIR/$lib" "${APPDIR}/usr/lib/" 2>&1 || {
                    echo "  ⚠️  Warning: Failed to copy $SEARCH_DIR/$lib"
                }
            fi
            break
        fi
    done
done

# Also try to find glibc version directory for proper loader setup
GLIBC_VERSION=$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1 || echo "unknown")
echo "  ℹ️  Build environment glibc version: $GLIBC_VERSION"

echo "✅ Critical GLIBC libraries copied"

# Copy libstdbuf.so from coreutils (required for stdbuf functionality)
echo "📦 Copying libstdbuf.so from coreutils..."
mkdir -p "${APPDIR}/usr/lib"
STDBUF_FOUND=0
for SEARCH_DIR in /usr/libexec/coreutils /opt /usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib; do
    if [ -d "$SEARCH_DIR" ]; then
        found_files=$(find "$SEARCH_DIR" -maxdepth 1 -name "libstdbuf.so*" 2>/dev/null || true)
        if [ -n "$found_files" ]; then
            echo "$found_files" | while read -r file; do
                if [ -f "$file" ] || [ -L "$file" ]; then
                    dest_file="${APPDIR}/usr/lib/$(basename "$file")"
                    if [ ! -e "$dest_file" ]; then
                        echo "  ✓ Copying: $(basename "$file") from $SEARCH_DIR"
                        cp -P "$file" "${APPDIR}/usr/lib/" 2>&1 || {
                            echo "  ⚠️  Warning: Failed to copy $file"
                        }
                    else
                        echo "  ✓ Already exists: $(basename "$file")"
                    fi
                fi
            done
            STDBUF_FOUND=1
            break
        fi
    fi
done

if [ $STDBUF_FOUND -eq 0 ]; then
    echo "  ⏭️  libstdbuf.so not found (optional, skipping)"
fi
echo "✅ libstdbuf.so processing completed"

# Copy JPEG libraries for image codec support
echo "Including JPEG libraries for image codec support in comprehensive AppImage..."
mkdir -p "${APPDIR}/usr/lib"
for SEARCH_DIR in /opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib; do
	if [ -d "$SEARCH_DIR" ]; then
		# Copy libjpeg libraries (both files and symlinks)
		LIBJPEG_COUNT=$(find "$SEARCH_DIR" -maxdepth 1 \( -name "libjpeg.so*" -type f -o -name "libjpeg.so*" -type l \) 2>/dev/null | wc -l)
		if [ "$LIBJPEG_COUNT" -gt 0 ]; then
			find "$SEARCH_DIR" -maxdepth 1 \( -name "libjpeg.so*" -type f -o -name "libjpeg.so*" -type l \) 2>/dev/null | while read -r file; do
				if [ ! -f "${APPDIR}/usr/lib/$(basename "$file")" ]; then
					cp -a "$file" "${APPDIR}/usr/lib/"
					echo "✓ Copied JPEG library: $(basename "$file")"
				fi
			done
		fi
		# Copy libturbojpeg libraries (both files and symlinks)
		LIBTURBOJPEG_COUNT=$(find "$SEARCH_DIR" -maxdepth 1 \( -name "libturbojpeg.so*" -type f -o -name "libturbojpeg.so*" -type l \) 2>/dev/null | wc -l)
		if [ "$LIBTURBOJPEG_COUNT" -gt 0 ]; then
			find "$SEARCH_DIR" -maxdepth 1 \( -name "libturbojpeg.so*" -type f -o -name "libturbojpeg.so*" -type l \) 2>/dev/null | while read -r file; do
				if [ ! -f "${APPDIR}/usr/lib/$(basename "$file")" ]; then
					cp -a "$file" "${APPDIR}/usr/lib/"
					echo "✓ Copied TurboJPEG library: $(basename "$file")"
				fi
			done
		fi
	fi
done

# Copy EGL and GPU rendering libraries (required for Qt GUI rendering)
echo "Including EGL and GPU rendering libraries for GUI support in comprehensive AppImage..."
mkdir -p "${APPDIR}/usr/lib"
EGL_LIBS=(
	"libEGL.so.1"
	"libEGL.so"
	"libGLESv2.so.2"
	"libGLESv2.so"
	"libGL.so.1"
	"libGL.so"
	"libGLX.so.0"
	"libGLX.so"
	"libglvnd.so.0"
	"libglvnd_pthread.so.0"
	"libglvnd_dl.so.0"
)

# Track which libraries were found
declare -A EGL_FOUND_COMP

# Search in multiple directories including /lib paths
for SEARCH_DIR in /opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib /usr/lib64 /lib/x86_64-linux-gnu /lib; do
	if [ -d "$SEARCH_DIR" ]; then
		for lib in "${EGL_LIBS[@]}"; do
			# Skip if already found
			if [ "${EGL_FOUND_COMP[$lib]:-}" = "1" ]; then
				continue
			fi
			
			# Search for the library (including wildcards for versioned libs)
			found_files=$(find "$SEARCH_DIR" -maxdepth 1 \( -name "$lib" -o -name "${lib}.*" \) 2>/dev/null || true)
			
			if [ -n "$found_files" ]; then
				echo "$found_files" | while read -r file; do
					if [ -f "$file" ] || [ -L "$file" ]; then
						dest_file="${APPDIR}/usr/lib/$(basename "$file")"
						if [ ! -e "$dest_file" ]; then
							echo "  ✓ Copying: $(basename "$file") from $SEARCH_DIR"
							cp -P "$file" "${APPDIR}/usr/lib/" 2>&1 || {
								echo "  ⚠️  Warning: Failed to copy $file"
							}
						fi
					fi
				done
				EGL_FOUND_COMP[$lib]=1
			fi
		done
	fi
done

# Verify critical EGL libraries were copied
echo "📊 Verifying EGL library installation in comprehensive AppImage..."
MISSING_LIBS_COMP=()
for lib in "libEGL.so.1" "libGL.so.1" "libGLX.so.0"; do
	if [ ! -e "${APPDIR}/usr/lib/$lib" ]; then
		MISSING_LIBS_COMP+=("$lib")
		echo "  ⚠️  Missing critical library: $lib"
	else
		echo "  ✓ Found: $lib"
	fi
done

if [ ${#MISSING_LIBS_COMP[@]} -gt 0 ]; then
	echo "⚠️  WARNING: Some critical EGL libraries are missing!"
	echo "    Attempting to install mesa-libEGL..."
	
	# Try to install EGL libraries if missing
	if command -v apt-get >/dev/null 2>&1; then
		apt-get update -qq && apt-get install -y libgl1 libegl1 libglx0 libglvnd0 2>/dev/null || true
		
		# Retry copying after installation
		for lib in "${MISSING_LIBS_COMP[@]}"; do
			for SEARCH_DIR in /usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib; do
				if [ -f "$SEARCH_DIR/$lib" ]; then
					echo "  ✓ Found and copying: $lib from $SEARCH_DIR"
					cp -P "$SEARCH_DIR/$lib" "${APPDIR}/usr/lib/"
					break
				fi
			done
		done
	fi
fi

echo "✓ EGL and GPU rendering libraries processed"

# Copy AppStream/metainfo (optional)

# Copy Qt platform plugins (CRITICAL for GUI applications)
echo "Including Qt platform plugins for GUI support in comprehensive AppImage..."
mkdir -p "${APPDIR}/usr/plugins/platforms"

QT_PLATFORM_PLUGINS=(
    "libqxcb.so"
    "libqoffscreen.so"
    "libqminimal.so"
)

# Also ensure xcb-cursor library is included (required by xcb plugin)
XCB_DEPS=(
    "libxcb-cursor.so.0"
    "libxcb-cursor.so"
)

# Copy xcb dependencies first
for SEARCH_DIR in /usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib; do
    if [ -d "$SEARCH_DIR" ]; then
        for dep in "${XCB_DEPS[@]}"; do
            found_files=$(find "$SEARCH_DIR" -maxdepth 1 \( -name "$dep" -o -name "${dep}.*" \) 2>/dev/null || true)
            if [ -n "$found_files" ]; then
                echo "$found_files" | while read -r file; do
                    if [ -f "$file" ] && [ ! -f "${APPDIR}/usr/lib/$(basename "$file")" ]; then
                        echo "  ✓ Copying xcb dependency: $(basename "$file")"
                        cp "$file" "${APPDIR}/usr/lib/"
                    fi
                done
            fi
        done
    fi
done

# Search for Qt platform plugins in multiple directories
for SEARCH_DIR in /opt/Qt6/plugins/platforms /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms; do
    if [ -d "$SEARCH_DIR" ]; then
        echo "  Searching Qt plugins in: $SEARCH_DIR"
        for plugin in "${QT_PLATFORM_PLUGINS[@]}"; do
            # Search for the plugin (including wildcards for versioned plugins)
            found_files=$(find "$SEARCH_DIR" -maxdepth 1 \( -name "$plugin" -o -name "${plugin}.*" \) 2>/dev/null || true)
            
            if [ -n "$found_files" ]; then
                echo "$found_files" | while read -r file; do
                    if [ -f "$file" ]; then
                        dest_file="${APPDIR}/usr/plugins/platforms/$(basename "$file")"
                        if [ ! -e "$dest_file" ]; then
                            echo "  ✓ Copying Qt platform plugin: $(basename "$file") from $SEARCH_DIR"
                            cp "$file" "${APPDIR}/usr/plugins/platforms/"
                            
                            # Also copy plugin dependencies
                            ldd "$file" 2>/dev/null | grep -v "linux-vdso" | grep -v "ld-linux" | awk '{print $3}' | while read -r dep; do
                                if [ -f "$dep" ] && [[ "$dep" == /usr/lib/* || "$dep" == /lib/* ]] && [ ! -f "${APPDIR}/usr/lib/$(basename "$dep")" ]; then
                                    echo "    Copying dependency: $(basename "$dep")"
                                    cp "$dep" "${APPDIR}/usr/lib/"
                                fi
                            done
                        fi
                    fi
                done
            fi
        done
    fi
done

# Verify Qt platform plugins were copied
echo "📊 Verifying Qt platform plugin installation..."
MISSING_QT_PLUGINS=()
for plugin in "${QT_PLATFORM_PLUGINS[@]}"; do
    if [ ! -e "${APPDIR}/usr/plugins/platforms/$plugin" ]; then
        MISSING_QT_PLUGINS+=("$plugin")
        echo "  ⚠️  Missing Qt platform plugin: $plugin"
    else
        echo "  ✓ Found Qt platform plugin: $plugin"
    fi
done

if [ ${#MISSING_QT_PLUGINS[@]} -gt 0 ]; then
    echo "⚠️  WARNING: Some Qt platform plugins are missing!"
    echo "    This may cause GUI display issues in the AppImage"
fi

echo "✓ Qt platform plugins processed"
if [ -f "${SRC}/packaging/appimage/com.openterface.openterfaceQT.metainfo.xml" ]; then
	mkdir -p "${APPDIR}/usr/share/metainfo"
	cp "${SRC}/packaging/appimage/com.openterface.openterfaceQT.metainfo.xml" "${APPDIR}/usr/share/metainfo/"
fi

# Copy translations if present
if ls "${BUILD}"/openterface_*.qm >/dev/null 2>&1; then
	mkdir -p "${APPDIR}/usr/share/openterfaceQT/translations"
	cp "${BUILD}"/openterface_*.qm "${APPDIR}/usr/share/openterfaceQT/translations/" || true
fi

# Try to locate an icon and install it as openterfaceQT.png
ICON_SRC=""
for p in \
	"${SRC}/images/icon_256.png" \
	"${SRC}/images/icon_256.svg" \
	"${SRC}/images/icon_128.png" \
	"${SRC}/images/icon_64.png" \
	"${SRC}/images/icon_32.png" \
	"${SRC}/resources/icons/openterfaceQT.png" \
	"${SRC}/resources/icons/openterface.png" \
	"${SRC}/icons/openterfaceQT.png" \
	"${SRC}/icons/openterface.png" \
	"${SRC}"/openterface*.png \
	"${SRC}"/resources/*.png; do
	if [ -f "$p" ]; then ICON_SRC="$p"; break; fi
done
if [ -n "${ICON_SRC}" ]; then
	ICON_EXT="${ICON_SRC##*.}"
	if [ "${ICON_EXT}" = "svg" ]; then
		mkdir -p "${APPDIR}/usr/share/icons/hicolor/scalable/apps"
		cp "${ICON_SRC}" "${APPDIR}/usr/share/icons/hicolor/scalable/apps/openterfaceQT.svg" || true
	else
		mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"
		cp "${ICON_SRC}" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/openterfaceQT.${ICON_EXT}" || true
	fi
	# Also copy to pixmaps and root
	mkdir -p "${APPDIR}/usr/share/pixmaps"
	cp "${ICON_SRC}" "${APPDIR}/usr/share/pixmaps/openterfaceqt.${ICON_EXT}" || true
	cp "${ICON_SRC}" "${APPDIR}/openterfaceqt.${ICON_EXT}" || true
else
	echo "No icon found; continuing without a custom icon."
fi

# Create diagnostic helper script for troubleshooting GLIBC issues
mkdir -p "${APPDIR}/usr/bin"
cat > "${APPDIR}/usr/bin/diagnose-appimage.sh" << 'EOFDIAG'
#!/bin/bash
# Diagnostic script for OpenterfaceQT AppImage
# This script helps troubleshoot GLIBC and library compatibility issues

HERE="$(dirname "$(readlink -f "${0}")/../..")"
echo "=== OpenterfaceQT AppImage Diagnostic Report ==="
echo ""
echo "📍 AppImage Location: $HERE"
echo ""

echo "📊 System Information:"
echo "  Architecture: $(uname -m)"
echo "  System GLIBC: $(ldd --version 2>/dev/null | head -1)"
echo "  System libc path: $(ldconfig -p 2>/dev/null | grep libc.so.6 | head -1 | awk '{print $NF}')"
echo ""

echo "📦 Bundled GLIBC in AppImage:"
if [ -f "$HERE/usr/lib/libc.so.6" ]; then
    echo "  ✓ Found: $HERE/usr/lib/libc.so.6"
    file "$HERE/usr/lib/libc.so.6"
    # Try to get version info
    echo "  Version info:"
    strings "$HERE/usr/lib/libc.so.6" 2>/dev/null | grep "^GLIBC_" | sort | tail -5 || echo "    (unable to extract)"
else
    echo "  ✗ NOT FOUND: $HERE/usr/lib/libc.so.6"
fi
echo ""

echo "📦 Bundled critical libraries:"
for lib in libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1 libusb-1.0.so.0 libstdbuf.so; do
    if [ -f "$HERE/usr/lib/$lib" ] || [ -f "$HERE/usr/lib/${lib}.0" ] || [ -f "$HERE/usr/lib/${lib}.1" ]; then
        echo "  ✓ Found: $lib"
    else
        echo "  ✗ Missing: $lib"
    fi
done
echo ""

echo "🔍 Library dependency check for main executable:"
if [ -f "$HERE/usr/bin/openterfaceQT" ]; then
    echo "  Checking: $HERE/usr/bin/openterfaceQT"
    if command -v ldd >/dev/null 2>&1; then
        echo "  Dependencies:"
        ldd "$HERE/usr/bin/openterfaceQT" 2>&1 | head -20 || echo "    (unable to list)"
        echo ""
        echo "  Missing dependencies:"
        ldd "$HERE/usr/bin/openterfaceQT" 2>&1 | grep "not found" || echo "    (none found - OK!)"
    else
        echo "  ldd not available"
    fi
else
    echo "  ✗ Executable not found: $HERE/usr/bin/openterfaceQT"
fi
echo ""

echo "📁 AppImage library directory contents:"
echo "  usr/lib/ has $(ls -1 "$HERE/usr/lib/" 2>/dev/null | wc -l) files"
echo "  First 20 files:"
ls -1 "$HERE/usr/lib/" 2>/dev/null | head -20 | sed 's/^/    /'
echo ""

echo "✅ Diagnostic report complete"
EOFDIAG

chmod +x "${APPDIR}/usr/bin/diagnose-appimage.sh"
echo "✓ Created diagnostic helper: diagnose-appimage.sh (run inside extracted AppImage)"


# Prefer preinstalled linuxdeploy and plugin in the image; fallback to download
TOOLS_DIR="${APPIMAGE_DIR}/tools"
LINUXDEPLOY_BIN=""

# Ensure AppImages run inside containers without FUSE (also set in Dockerfile)
export APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN:-1}

# Check for pre-downloaded runtime from Docker image
mkdir -p "${TOOLS_DIR}"
DOCKER_RUNTIME_FILE="/opt/appimage-runtime/runtime-${APPIMAGE_ARCH}"
TOOLS_RUNTIME_FILE="${TOOLS_DIR}/runtime-${APPIMAGE_ARCH}"

# Prioritize pre-downloaded runtime from Docker image
if [ -f "${DOCKER_RUNTIME_FILE}" ]; then
	echo "✓ Using pre-downloaded runtime from Docker environment: ${DOCKER_RUNTIME_FILE}"
	cp "${DOCKER_RUNTIME_FILE}" "${TOOLS_RUNTIME_FILE}"
	chmod +x "${TOOLS_RUNTIME_FILE}"
	echo "✓ Runtime copied to tools directory: ${TOOLS_RUNTIME_FILE}"
elif [ -f "${TOOLS_RUNTIME_FILE}" ]; then
	echo "✓ Runtime already available in tools directory: ${TOOLS_RUNTIME_FILE}"
else
	echo "⚠ Warning: No pre-downloaded runtime found at ${DOCKER_RUNTIME_FILE}"
	echo "⚠ AppImage creation will proceed without custom runtime (linuxdeploy will download automatically)"
fi

if command -v linuxdeploy >/dev/null 2>&1 && command -v linuxdeploy-plugin-qt >/dev/null 2>&1; then
	echo "Using preinstalled linuxdeploy and linuxdeploy-plugin-qt"
	LINUXDEPLOY_BIN="$(command -v linuxdeploy)"
	# Ensure the plugin directory is on PATH
	PLUGIN_DIR="$(dirname "$(command -v linuxdeploy-plugin-qt)")"
	export PATH="${PLUGIN_DIR}:${PATH}"
else
	echo "linuxdeploy not found in image; downloading tools..."
	# Download helper (curl with wget fallback)
	_fetch() {
		local url="$1" out="$2"
		if command -v curl >/dev/null 2>&1; then
			curl -fL "${url}" -o "${out}"
		elif command -v wget >/dev/null 2>&1; then
			wget -qO "${out}" "${url}"
		else
			echo "Neither curl nor wget found for downloading ${url}" >&2
			return 1
		fi
	}

	mkdir -p "${TOOLS_DIR}"
	pushd "${TOOLS_DIR}" >/dev/null
	LDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${APPIMAGE_ARCH}.AppImage"
	LDEPLOY_QT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${APPIMAGE_ARCH}.AppImage"
	
	echo "Downloading linuxdeploy from ${LDEPLOY_URL}"
	_fetch "${LDEPLOY_URL}" linuxdeploy.AppImage
	echo "Downloading linuxdeploy-plugin-qt from ${LDEPLOY_QT_URL}"
	_fetch "${LDEPLOY_QT_URL}" linuxdeploy-plugin-qt.AppImage
	
	# Use pre-downloaded runtime from Docker image if available
	if [ -f "/opt/appimage-runtime/runtime-${APPIMAGE_ARCH}" ]; then
		echo "✓ Using pre-downloaded runtime from Docker environment"
		cp "/opt/appimage-runtime/runtime-${APPIMAGE_ARCH}" "runtime-${APPIMAGE_ARCH}"
		chmod +x "runtime-${APPIMAGE_ARCH}"
		echo "✓ Runtime copied successfully"
	else
		echo "⚠ No pre-downloaded runtime found, linuxdeploy will download automatically"
	fi
	
	chmod +x linuxdeploy.AppImage linuxdeploy-plugin-qt.AppImage
	# Use the downloaded linuxdeploy and make plugin available on PATH
	LINUXDEPLOY_BIN="${TOOLS_DIR}/linuxdeploy.AppImage"
	export PATH="${TOOLS_DIR}:${PATH}"
	popd >/dev/null
fi

# Build AppImage with comprehensive runtime support
pushd "${APPIMAGE_DIR}" >/dev/null

# For static builds, skip Qt plugin since Qt is statically linked
if [ "${OPENTERFACE_BUILD_STATIC}" != "ON" ]; then
	USE_QT_PLUGIN=true
else
	USE_QT_PLUGIN=false
fi

# Set runtime file path - prioritize Docker pre-downloaded runtime
RUNTIME_FILE="${TOOLS_DIR}/runtime-${APPIMAGE_ARCH}"
DOCKER_RUNTIME_FILE="/opt/appimage-runtime/runtime-${APPIMAGE_ARCH}"
LOCAL_RUNTIME_FILE="./runtime-${APPIMAGE_ARCH}"

# Copy runtime to current directory where appimagetool can find it
if [ -f "${RUNTIME_FILE}" ]; then
	echo "✓ Copying runtime to AppImage build directory for appimagetool"
	cp "${RUNTIME_FILE}" "${LOCAL_RUNTIME_FILE}"
	chmod +x "${LOCAL_RUNTIME_FILE}"
elif [ -f "${DOCKER_RUNTIME_FILE}" ]; then
	echo "✓ Copying Docker runtime to AppImage build directory for appimagetool"
	cp "${DOCKER_RUNTIME_FILE}" "${LOCAL_RUNTIME_FILE}"
	chmod +x "${LOCAL_RUNTIME_FILE}"
	RUNTIME_FILE="${LOCAL_RUNTIME_FILE}"
fi
DOCKER_RUNTIME_FILE="/opt/appimage-runtime/runtime-${APPIMAGE_ARCH}"

# Ensure we have the runtime from Docker environment
if [ -f "${DOCKER_RUNTIME_FILE}" ] && [ ! -f "${RUNTIME_FILE}" ]; then
	echo "✓ Copying Docker pre-downloaded runtime to tools directory"
	cp "${DOCKER_RUNTIME_FILE}" "${RUNTIME_FILE}"
	chmod +x "${RUNTIME_FILE}"
fi

if [ -f "${LOCAL_RUNTIME_FILE}" ]; then
	# Set multiple environment variables that appimagetool might recognize
	export APPIMAGE_RUNTIME_FILE="${LOCAL_RUNTIME_FILE}"
	export RUNTIME_FILE="${LOCAL_RUNTIME_FILE}"
	export APPIMAGETOOL_RUNTIME="${LOCAL_RUNTIME_FILE}"
	export RUNTIME="${LOCAL_RUNTIME_FILE}"
	# Unset UPDATE_INFORMATION to avoid format errors in appimagetool
	unset UPDATE_INFORMATION
	unset LDAI_UPDATE_INFORMATION
	echo "✓ Using local runtime file: ${LOCAL_RUNTIME_FILE}"
	ls -lh "${LOCAL_RUNTIME_FILE}"
elif [ -f "${DOCKER_RUNTIME_FILE}" ]; then
	# Use Docker runtime directly if tools copy doesn't exist
	echo "✓ Using Docker runtime directly: ${DOCKER_RUNTIME_FILE}"
	export APPIMAGE_RUNTIME_FILE="${DOCKER_RUNTIME_FILE}"
	export RUNTIME_FILE="${DOCKER_RUNTIME_FILE}"
	export APPIMAGETOOL_RUNTIME="${DOCKER_RUNTIME_FILE}"
	export RUNTIME="${DOCKER_RUNTIME_FILE}"
	unset UPDATE_INFORMATION
	unset LDAI_UPDATE_INFORMATION
	ls -lh "${DOCKER_RUNTIME_FILE}"
else
	echo "⚠ Warning: No runtime file available, linuxdeploy will download it automatically"
	unset APPIMAGE_RUNTIME_FILE
	unset RUNTIME_FILE
	unset APPIMAGETOOL_RUNTIME
	unset RUNTIME
	unset UPDATE_INFORMATION
	unset LDAI_UPDATE_INFORMATION
fi

# Debug: Show environment variables for AppImage creation
echo "=== AppImage Build Environment ==="
echo "APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN}"
echo "APPIMAGE_RUNTIME_FILE=${APPIMAGE_RUNTIME_FILE:-<not set>}"
echo "RUNTIME_FILE=${RUNTIME_FILE:-<not set>}"
echo "APPIMAGETOOL_RUNTIME=${APPIMAGETOOL_RUNTIME:-<not set>}"
echo "RUNTIME=${RUNTIME:-<not set>}"
echo "UPDATE_INFORMATION=${UPDATE_INFORMATION:-<not set>}"
echo "LINUXDEPLOY_BIN=${LINUXDEPLOY_BIN}"
echo "================================="

# Debug: Show the actual linuxdeploy command being executed
echo "=== LinuxDeploy Command Debug ==="
echo "LINUXDEPLOY_BIN: ${LINUXDEPLOY_BIN}"
echo "APPDIR: ${APPDIR}"
echo "DESKTOP_OUT: ${DESKTOP_OUT}"
echo "ICON_SRC: ${ICON_SRC}"
echo "USE_QT_PLUGIN: ${USE_QT_PLUGIN}"
echo "================================"

# Debug: Show what libraries are in the AppDir before linuxdeploy
echo "📊 Verifying libusb in AppDir before linuxdeploy..."
LIBUSB_COUNT=$(find "${APPDIR}/usr/lib" -name "libusb*" 2>/dev/null | wc -l)
if [ "$LIBUSB_COUNT" -gt 0 ]; then
	echo "  ✅ libusb found in AppDir:"
	find "${APPDIR}/usr/lib" -name "libusb*" 2>/dev/null | while read -r lib; do
		echo "     - $lib"
		file "$lib" 2>/dev/null || echo "       (unable to determine file type)"
	done
else
	echo "  ❌ libusb NOT found in AppDir!"
	echo "  Available system libraries:"
	ls -lh "${APPDIR}/usr/lib/" | head -20
fi
echo ""

# Set LD_LIBRARY_PATH to include ffmpeg and gstreamer libraries for dependency resolution
export LD_LIBRARY_PATH="/opt/ffmpeg/lib:/opt/gstreamer/lib:/opt/gstreamer/lib/${ARCH}-linux-gnu:/opt/Qt6/lib:$LD_LIBRARY_PATH"
export PATH="/opt/Qt6/bin:$PATH"
export QT_PLUGIN_PATH="/opt/Qt6/plugins:$QT_PLUGIN_PATH"
export QML2_IMPORT_PATH="/opt/Qt6/qml:$QML2_IMPORT_PATH"

# CRITICAL: Save libraries BEFORE linuxdeploy (it will blacklist some)
echo "📦 Pre-linuxdeploy: Saving critical libraries to prevent blacklisting..."
mkdir -p "${APPDIR}/usr/lib/linuxdeploy_protected"

# These libraries WILL be blacklisted by linuxdeploy - save them now
PROTECTED_LIBS=(
	"libc.so.6"
	"libm.so.6" 
	"libpthread.so.0"
	"libdl.so.2"
	"librt.so.1"
	"libgcc_s.so.1"
	"libstdc++.so.6"
	"ld-linux-x86-64.so.2"
	"libnss_compat.so.2"
	"libnss_files.so.2"
	"libnss_dns.so.2"
	"libresolv.so.2"
	"libcrypt.so.1"
	"libutil.so.1"
)

PROTECTED_COUNT=0
for lib in "${PROTECTED_LIBS[@]}"; do
	if [ -f "${APPDIR}/usr/lib/$lib" ]; then
		cp -P "${APPDIR}/usr/lib/$lib" "${APPDIR}/usr/lib/linuxdeploy_protected/" || true
		PROTECTED_COUNT=$((PROTECTED_COUNT + 1))
	fi
done
echo "  ✓ Protected $PROTECTED_COUNT critical libraries"

# Build the command with proper argument handling
LINUXDEPLOY_ARGS=(
	"--appdir" "${APPDIR}"
	"--executable" "${APPDIR}/usr/bin/openterfaceQT"
	"--desktop-file" "${DESKTOP_OUT}"
)

if [ -n "${ICON_SRC}" ]; then
	LINUXDEPLOY_ARGS+=("--icon-file" "${ICON_SRC}")
fi

if [ "${USE_QT_PLUGIN}" = "true" ]; then
	LINUXDEPLOY_ARGS+=("--plugin" "qt")
fi

LINUXDEPLOY_ARGS+=("--output" "appimage")

echo "Final command: ${LINUXDEPLOY_BIN}" "${LINUXDEPLOY_ARGS[@]}"

# Try running linuxdeploy without the appimage output plugin first
LINUXDEPLOY_ARGS_NO_OUTPUT=(
	"--appdir" "${APPDIR}"
	"--executable" "${APPDIR}/usr/bin/openterfaceQT"
	"--desktop-file" "${DESKTOP_OUT}"
)

if [ -n "${ICON_SRC}" ]; then
	LINUXDEPLOY_ARGS_NO_OUTPUT+=("--icon-file" "${ICON_SRC}")
fi

if [ "${USE_QT_PLUGIN}" = "true" ]; then
	LINUXDEPLOY_ARGS_NO_OUTPUT+=("--plugin" "qt")
fi

echo "Running linuxdeploy without output plugin..."
echo "⚠️  IMPORTANT: linuxdeploy will skip/blacklist certain libraries"
echo "    (libc.so.6, libgcc_s.so.1, libstdc++.so.6)"
echo "    These have been backed up and will be restored after linuxdeploy completes"
echo ""

# Run linuxdeploy with environment variable to suppress some warnings
# Note: linuxdeploy will still skip blacklisted libraries, but we'll restore them after
export LDAI_SKIP_GLIBC=1
if "${LINUXDEPLOY_BIN}" "${LINUXDEPLOY_ARGS_NO_OUTPUT[@]}"; then
	echo "✓ linuxdeploy completed"
else
	LINUXDEPLOY_EXIT=$?
	echo "⚠️  linuxdeploy exited with code: $LINUXDEPLOY_EXIT"
	if [ $LINUXDEPLOY_EXIT -eq 141 ] || [ $LINUXDEPLOY_EXIT -eq 3 ]; then
		echo "   (This may be a SIGPIPE or similar non-critical error)"
		echo "   Continuing..."
	fi
fi

# CRITICAL FIX: Restore critical libraries that linuxdeploy blacklisted
echo "🔧 Restoring critical libraries that linuxdeploy skipped..."
echo "   (linuxdeploy blacklists: libc.so.6, libgcc_s.so.1, libstdc++.so.6, etc.)"

if [ -d "${APPDIR}/usr/lib/linuxdeploy_protected" ]; then
	RESTORED_COUNT=0
	for lib in "${APPDIR}/usr/lib/linuxdeploy_protected"/*; do
		if [ -f "$lib" ]; then
			libname=$(basename "$lib")
			# Check if linuxdeploy removed it
			if [ ! -f "${APPDIR}/usr/lib/$libname" ]; then
				echo "  ✓ Restoring blacklisted library: $libname"
				cp -P "$lib" "${APPDIR}/usr/lib/" || echo "    ⚠️  Failed to restore $libname"
				RESTORED_COUNT=$((RESTORED_COUNT + 1))
			else
				echo "  ℹ️  Library still present: $libname"
			fi
		fi
	done
	echo "  ✅ Restored $RESTORED_COUNT blacklisted libraries"
	rm -rf "${APPDIR}/usr/lib/linuxdeploy_protected"
else
	echo "  ⚠️  No protected backup found!"
fi

# Double-check critical libraries are present
echo ""
echo "📊 Verifying critical libraries after linuxdeploy..."
CRITICAL_LIBS=("libc.so.6" "libm.so.6" "libpthread.so.0" "libdl.so.2" "libgcc_s.so.1" "libstdc++.so.6")
MISSING_LIBS=()
for lib in "${CRITICAL_LIBS[@]}"; do
	if [ -f "${APPDIR}/usr/lib/$lib" ]; then
		echo "  ✓ Present: $lib"
	else
		echo "  ✗ MISSING: $lib"
		MISSING_LIBS+=("$lib")
	fi
done

if [ ${#MISSING_LIBS[@]} -gt 0 ]; then
	echo ""
	echo "❌ ERROR: Critical libraries are missing!"
	echo "Missing: ${MISSING_LIBS[*]}"
	echo ""
	echo "This may cause the AppImage to fail with GLIBC version errors."
	echo "Available libraries in ${APPDIR}/usr/lib:"
	ls -1 "${APPDIR}/usr/lib" | grep -E "lib(c|m|gcc|stdc)" | head -10
	echo ""
fi

# After linuxdeploy, ensure we have the correct Qt platform plugins with all dependencies
echo "Ensuring Qt platform plugins are properly bundled after linuxdeploy..."
mkdir -p "${APPDIR}/usr/plugins/platforms"

QT_PLATFORM_PLUGINS=(
    "libqxcb.so"
    "libqoffscreen.so" 
    "libqminimal.so"
)

# Also ensure xcb-cursor library is included (required by xcb plugin)
XCB_DEPS=(
    "libxcb-cursor.so.0"
    "libxcb-cursor.so"
)

# Copy xcb dependencies first
for SEARCH_DIR in /usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib; do
    if [ -d "$SEARCH_DIR" ]; then
        for dep in "${XCB_DEPS[@]}"; do
            found_files=$(find "$SEARCH_DIR" -maxdepth 1 \( -name "$dep" -o -name "${dep}.*" \) 2>/dev/null || true)
            if [ -n "$found_files" ]; then
                echo "$found_files" | while read -r file; do
                    if [ -f "$file" ] && [ ! -f "${APPDIR}/usr/lib/$(basename "$file")" ]; then
                        echo "  ✓ Copying xcb dependency: $(basename "$file")"
                        cp "$file" "${APPDIR}/usr/lib/"
                    fi
                done
            fi
        done
    fi
done

# Force copy Qt platform plugins (override any that linuxdeploy copied)
for SEARCH_DIR in /opt/Qt6/plugins/platforms /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms; do
    if [ -d "$SEARCH_DIR" ]; then
        echo "  Ensuring Qt plugins from: $SEARCH_DIR"
        for plugin in "${QT_PLATFORM_PLUGINS[@]}"; do
            found_files=$(find "$SEARCH_DIR" -maxdepth 1 \( -name "$plugin" -o -name "${plugin}.*" \) 2>/dev/null || true)
            
            if [ -n "$found_files" ]; then
                echo "$found_files" | while read -r file; do
                    if [ -f "$file" ]; then
                        dest_file="${APPDIR}/usr/plugins/platforms/$(basename "$file")"
                        echo "  ✓ Forcing Qt platform plugin: $(basename "$file")"
                        cp "$file" "${APPDIR}/usr/plugins/platforms/"
                        
                        # Also copy plugin dependencies
                        ldd "$file" 2>/dev/null | grep -v "linux-vdso" | grep -v "ld-linux" | awk '{print $3}' | while read -r dep; do
                            if [ -f "$dep" ] && [[ "$dep" == /usr/lib/* || "$dep" == /lib/* ]] && [ ! -f "${APPDIR}/usr/lib/$(basename "$dep")" ]; then
                                echo "    Copying dependency: $(basename "$dep")"
                                cp "$dep" "${APPDIR}/usr/lib/"
                            fi
                        done
                    fi
                done
            fi
        done
    fi
done

echo "✓ Qt platform plugins ensured"

# CRITICAL FIX: Remove coreutils utilities that have incompatible GLIBC versions
# These utilities (like stdbuf) should come from the HOST system, not the AppImage
# This prevents GLIBC_2.38 dependency errors from bundled coreutils
echo "🔧 Removing problematic coreutils utilities that may have newer GLIBC dependencies..."
PROBLEM_BINS=(
    "stdbuf"           # Most common culprit - uses libstdbuf.so.so
    "time"             # May also have version issues
    "timeout"          # May have version issues
)

for bin in "${PROBLEM_BINS[@]}"; do
    if [ -f "${APPDIR}/usr/bin/$bin" ]; then
        echo "  🗑️  Removing ${bin} to avoid GLIBC compatibility issues"
        rm -f "${APPDIR}/usr/bin/$bin"
    fi
done

# Also remove problematic coreutils libraries
echo "Removing bundled coreutils utilities that might cause GLIBC issues..."
rm -rf "${APPDIR}/usr/libexec/coreutils/" 2>/dev/null || true
echo "  ✓ Coreutils utilities removed"

# Create custom AppRun script with proper environment setup after linuxdeploy to avoid override
cat > "${APPDIR}/AppRun" << 'EOF'
#!/bin/bash
# AppRun script for OpenterfaceQT enhanced AppImage with GStreamer plugins
# This script ensures compatibility across systems with different glibc versions

# Set the directory where this script is located
HERE="$(dirname "$(readlink -f "${0}")")"

# CRITICAL: Set library path to use AppImage bundled libraries FIRST
# This prevents loading incompatible system libraries with different GLIBC versions
# The order is important:
# 1. AppImage's own libc.so.6 and glibc libraries (built in the container environment)
# 2. Other AppImage libraries
# 3. System libraries (as fallback only)
export LD_LIBRARY_PATH="${HERE}/usr/lib:${HERE}/usr/lib/x86_64-linux-gnu:${HERE}/lib:${HERE}/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH}"

# CRITICAL: Ensure system PATH comes AFTER AppImage bin for utilities
# But this allows fallback to host system utilities (like stdbuf) with compatible GLIBC
export PATH="${HERE}/usr/bin:${HERE}/bin:/usr/local/bin:/usr/bin:/bin"

# IMPORTANT: Also set LD_PRELOAD to force usage of bundled libc if available
# This ensures all child processes use the same glibc version
if [ -f "${HERE}/usr/lib/libc.so.6" ]; then
    # Preload the bundled libc to ensure consistency
    export LD_PRELOAD="${HERE}/usr/lib/libc.so.6${LD_PRELOAD:+:$LD_PRELOAD}"
fi

# Set dynamic linker to use bundled loader if available
# This is critical for glibc compatibility
if [ -f "${HERE}/usr/lib/ld-linux-x86-64.so.2" ]; then
    # Some systems may respect this, though not all
    export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
fi

# Set GStreamer plugin path to our bundled plugins
export GST_PLUGIN_PATH="${HERE}/usr/lib/gstreamer-1.0:${GST_PLUGIN_PATH}"

# Set Qt plugin path
export QT_PLUGIN_PATH="${HERE}/usr/plugins:${QT_PLUGIN_PATH}"

# Ensure we don't have conflicting environment variables that might bypass our setup
unset LD_ORIGIN_PATH

# CRITICAL: Disable any LD_AUDIT hooks that might interfere
# strace and other debugging tools can cause issues with AppImage
unset LD_AUDIT

# Debug: Show library path (uncomment for debugging)
# echo "LD_LIBRARY_PATH=${LD_LIBRARY_PATH}"
# echo "LD_PRELOAD=${LD_PRELOAD}"
# echo "PATH=${PATH}"
# echo "GST_PLUGIN_PATH=${GST_PLUGIN_PATH}"
# echo "QT_PLUGIN_PATH=${QT_PLUGIN_PATH}"

# Run the application
exec "${HERE}/usr/bin/openterfaceQT" "$@"
EOF

chmod +x "${APPDIR}/AppRun"

# Then use appimagetool directly with explicit runtime file
echo "Running appimagetool with explicit runtime file..."
APPIMAGETOOL_FAILED=0
if command -v appimagetool >/dev/null 2>&1; then
	if [ -f "${RUNTIME_FILE}" ]; then
		echo "Using runtime file: ${RUNTIME_FILE}"
		appimagetool --runtime-file "${RUNTIME_FILE}" "${APPDIR}" || APPIMAGETOOL_FAILED=$?
	else
		echo "Warning: Runtime file not found, running appimagetool without runtime file"
		appimagetool "${APPDIR}" || APPIMAGETOOL_FAILED=$?
	fi
else
	echo "appimagetool not found, trying linuxdeploy with output plugin..."
	LINUXDEPLOY_ARGS+=("--output" "appimage")
	"${LINUXDEPLOY_BIN}" "${LINUXDEPLOY_ARGS[@]}" || APPIMAGETOOL_FAILED=$?
fi

if [ $APPIMAGETOOL_FAILED -ne 0 ] && [ $APPIMAGETOOL_FAILED -ne 141 ]; then
	echo "⚠️  appimagetool exited with code: $APPIMAGETOOL_FAILED"
fi

# Normalize output name
APPIMAGE_FILENAME="openterfaceQT_${VERSION}_${APPIMAGE_ARCH}.AppImage"
# Move whichever AppImage got produced
FOUND_APPIMAGE=$(ls -1 *.AppImage 2>/dev/null | grep -v -E '^linuxdeploy|^linuxdeploy-plugin-qt' | head -n1 || echo "")
if [ -n "${FOUND_APPIMAGE}" ]; then
	chmod +x "${FOUND_APPIMAGE}"
	mv "${FOUND_APPIMAGE}" "${APPIMAGE_OUT}/${APPIMAGE_FILENAME}"
	echo "✅ AppImage created at ${APPIMAGE_OUT}/${APPIMAGE_FILENAME}"
	echo "📊 AppImage size: $(ls -lh "${APPIMAGE_OUT}/${APPIMAGE_FILENAME}" | awk '{print $5}')"
else
	echo "⚠️  No AppImage found in current directory"
	echo "Checking for any .AppImage files in system..."
	find . -name "*.AppImage" -type f 2>/dev/null | head -5 | while read -r img; do
		echo "  Found: $img"
		if [ -f "$img" ]; then
			SIZE=$(ls -lh "$img" | awk '{print $5}')
			echo "  Size: $SIZE"
			chmod +x "$img"
			FINAL_NAME=$(basename "$img" | sed "s/_continuous/${VERSION}/g")
			mv "$img" "${APPIMAGE_OUT}/${FINAL_NAME}" 2>/dev/null && echo "  ✓ Moved to ${APPIMAGE_OUT}/${FINAL_NAME}" || echo "  ✗ Failed to move"
		fi
	done
	
	# Final check
	if ls "${APPIMAGE_OUT}"/*.AppImage >/dev/null 2>&1; then
		echo "✅ AppImage created successfully"
		ls -lh "${APPIMAGE_OUT}"/*.AppImage
	else
		echo "❌ Error: AppImage build did not produce an output." >&2
		exit 1
	fi
fi

popd >/dev/null