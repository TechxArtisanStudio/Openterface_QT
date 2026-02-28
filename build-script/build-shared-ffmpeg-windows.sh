#!/bin/bash
# ============================================================================
# FFmpeg Shared Build Script - Windows (external MinGW)
# ============================================================================
# This script builds shared FFmpeg and libjpeg-turbo on Windows using an external
# MinGW toolchain (e.g., C:\mingw64) and a bash shell (Git Bash).
# ============================================================================

set -euo pipefail
set -x  # Verbose execution for CI logs

# On error, dump last part of logs for easier debugging
trap 'echo "FFmpeg build failed - last 200 lines of logs:"; tail -n 200 "${BUILD_DIR}/ffmpeg-build.log" || true; tail -n 200 "${BUILD_DIR}/ffmpeg-configure.log" || true; exit 1' ERR

# Quick help / usage
if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
    cat <<'EOF'
Usage: ./build-shared-ffmpeg-windows.sh [ENV=VAL ...]

This script is intended to be run from an MSYS2 "MSYS2 MinGW 64-bit" shell.
Recommended example:
  SKIP_MSYS_MINGW=1 EXTERNAL_MINGW_MSYS=/mingw64 ./build-shared-ffmpeg-windows.sh

Environment variables supported by this script (set before running / from caller):
  SKIP_MSYS_MINGW=1          -> Skip package-managed install and prefer external MinGW (caller should set EXTERNAL_MINGW_MSYS)
  EXTERNAL_MINGW_MSYS=/c/mingw64 -> Path to external mingw in MSYS-style (set by caller wrapper when using EXTERNAL_MINGW)
  ENABLE_NVENC=1             -> Enable NVENC support (default: 1, set to 0 to disable)
  ENABLE_LIBMFX=1            -> Enable Intel QSV support (default: 1, set to 0 to disable)
  AUTO_INSTALL_FFNV=1        -> Auto-install nv-codec-headers (default: 1, set to 0 to disable)
  NVENC_SDK_PATH=...         -> Path to NVENC SDK (optional, used when ENABLE_NVENC=1; must point to SDK 12.x or newer for FFmpeg 6.1)

GPU Acceleration (enabled by default):
  - Intel QSV (Quick Sync Video): Enabled via libmfx, decoders will be built
  - NVIDIA NVENC/CUDA: Enabled, headers auto-installed (compatible 12.x headers) if not present
  - To disable: set ENABLE_LIBMFX=0 or ENABLE_NVENC=0 before running
EOF
    exit 0
fi

# Detect MSYS2 MinGW64 and set sensible defaults
if [ -n "${MSYSTEM:-}" ] && printf '%s
' "${MSYSTEM}" | grep -qi '^MINGW64'; then
    echo "Detected MSYS2 MINGW64 (MSYSTEM=${MSYSTEM}); defaulting EXTERNAL_MINGW_MSYS=/mingw64"
    : "${EXTERNAL_MINGW_MSYS:=/mingw64}"
fi

# If EXTERNAL_MINGW_MSYS not provided, try common locations
if [ -z "${EXTERNAL_MINGW_MSYS:-}" ]; then
    if [ -d "/mingw64/bin" ]; then
        EXTERNAL_MINGW_MSYS="/mingw64"
    elif [ -d "/c/msys64/mingw64/bin" ]; then
        EXTERNAL_MINGW_MSYS="/c/msys64/mingw64"
    elif [ -d "/c/mingw64/bin" ]; then
        EXTERNAL_MINGW_MSYS="/c/mingw64"
    fi
fi

# Helpful hint when no obvious MSYS2/Mingw detected
if [ -z "${MSYSTEM:-}" ] && [ -z "${EXTERNAL_MINGW_MSYS:-}" ]; then
    cat <<'EOF'
Warning: It looks like you may be running this script outside of MSYS2 MinGW64.
For best results open "MSYS2 MinGW 64-bit" and run:
  SKIP_MSYS_MINGW=1 EXTERNAL_MINGW_MSYS=/mingw64 ./build-shared-ffmpeg-windows.sh
EOF
fi

FFMPEG_VERSION="6.1.1"
LIBJPEG_TURBO_VERSION="3.0.4"
FFMPEG_INSTALL_PREFIX="/c/ffmpeg-shared"
BUILD_DIR="$(pwd)/ffmpeg-build-temp"
DOWNLOAD_URL="https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.bz2"
LIBJPEG_TURBO_URL="https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/${LIBJPEG_TURBO_VERSION}.tar.gz"

# Number of CPU cores for parallel compilation
NUM_CORES=$(nproc)

echo "============================================================================"
echo "FFmpeg Shared Build - Windows (external MinGW)"
echo "============================================================================"
echo "FFmpeg Version: ${FFMPEG_VERSION}"
echo "libjpeg-turbo Version: ${LIBJPEG_TURBO_VERSION}"
echo "Install Prefix: ${FFMPEG_INSTALL_PREFIX}"
echo "Build Directory: ${BUILD_DIR}"
echo "CPU Cores: ${NUM_CORES}"
echo "============================================================================"
echo ""
# Write a short marker to the Windows filesystem so we can confirm the script started when run under MSYS
# (use || true to avoid failing the script if /c is not writable)
echo "FFMPEG_BUILD_SCRIPT_STARTED: $(date)" > /c/ffmpeg_build_started.txt || true

# Prepare/verify required toolchain and utilities (external MinGW + bash)
# Default to SKIP_MSYS_MINGW=1 (use external MinGW) unless explicitly overridden
if [ "${SKIP_MSYS_MINGW:-1}" = "1" ]; then
    echo "Step 1/8: SKIP_MSYS_MINGW set (or default) - using external MinGW build environment"
    echo "External MinGW (msys style): ${EXTERNAL_MINGW_MSYS:-/c/mingw64}"
    # Prepend external mingw to PATH so the toolchain is used
    EXTERNAL_MINGW_BIN="${EXTERNAL_MINGW_MSYS:-/c/mingw64}/bin"
    if [ -d "${EXTERNAL_MINGW_BIN}" ]; then
        export PATH="${EXTERNAL_MINGW_BIN}:$PATH"
        echo "PATH updated to prefer external MinGW: ${EXTERNAL_MINGW_BIN}"
    else
        echo "WARNING: External MinGW bin directory not found: ${EXTERNAL_MINGW_BIN}"
        echo "Please ensure your external MinGW (gcc, make, cmake, nasm/yasm) are on PATH."
    fi

    # Sanity checks for mandatory tools (gcc, make/cmake, nasm/yasm, tar, wget/git)
    MISSING=0
    command -v gcc >/dev/null 2>&1 || { echo "ERROR: gcc not found on PATH"; MISSING=1; }
    command -v cmake >/dev/null 2>&1 || { echo "ERROR: cmake not found on PATH"; MISSING=1; }
    command -v make >/dev/null 2>&1 || command -v mingw32-make >/dev/null 2>&1 || { echo "ERROR: make (or mingw32-make) not found on PATH"; MISSING=1; }
    command -v nasm >/dev/null 2>&1 || command -v yasm >/dev/null 2>&1 || { echo "ERROR: nasm or yasm not found on PATH"; MISSING=1; }
    command -v tar >/dev/null 2>&1 || { echo "ERROR: tar not found on PATH"; MISSING=1; }
    command -v wget >/dev/null 2>&1 || command -v curl >/dev/null 2>&1 || { echo "ERROR: wget or curl not found on PATH"; MISSING=1; }
    command -v git >/dev/null 2>&1 || { echo "ERROR: git not found on PATH (required to auto-install nv-codec-headers). Install via pacman: pacman -S git"; MISSING=1; }

    # Additional helpful checks: cmp (diffutils) and make
    command -v cmp >/dev/null 2>&1 || { echo "ERROR: cmp not found on PATH (package: diffutils). Install via pacman: pacman -S diffutils"; MISSING=1; }
    command -v make >/dev/null 2>&1 || command -v mingw32-make >/dev/null 2>&1 || command -v gmake >/dev/null 2>&1 || { echo "ERROR: make not found on PATH. Install via pacman: pacman -S --needed mingw-w64-x86_64-make"; MISSING=1; }

    if [ "$MISSING" -eq 1 ]; then
        echo "One or more required tools are missing. Install the missing tools (gcc, cmake, make/mingw32-make, nasm/yasm, tar, wget/curl, git, bash) and ensure they are on PATH."
        exit 1
    fi
else
    echo "ERROR: SKIP_MSYS_MINGW is not set to 1. Automated package-managed installation was removed from this script."
    echo "Please run this script with SKIP_MSYS_MINGW=1 (the default) and provide an external MinGW toolchain (set EXTERNAL_MINGW or ensure /c/mingw64 exists)."
    exit 1
fi

# Choose CMake generator for external MinGW
GENERATOR="MinGW Makefiles"

# Create build directory
echo "Step 2/8: Creating build directory..."
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
echo "✓ Build directory ready: ${BUILD_DIR}"
echo ""

# Download and build libjpeg-turbo (prefer shared libs for a shared build)
echo "Step 3/8: Building libjpeg-turbo ${LIBJPEG_TURBO_VERSION} (shared)..."
if [ ! -f "${FFMPEG_INSTALL_PREFIX}/bin/libturbojpeg.dll" ] && [ ! -f "${FFMPEG_INSTALL_PREFIX}/lib/libturbojpeg.dll" ]; then
    echo "Downloading libjpeg-turbo..."
    if [ ! -f "libjpeg-turbo.tar.gz" ]; then
        wget "${LIBJPEG_TURBO_URL}" -O "libjpeg-turbo.tar.gz"
    fi
    echo "Extracting libjpeg-turbo..."
    tar xzf libjpeg-turbo.tar.gz
    cd "libjpeg-turbo-${LIBJPEG_TURBO_VERSION}"
    mkdir -p build
    cd build
    echo "Configuring libjpeg-turbo (shared): (generator: ${GENERATOR})"
    cmake .. -G "${GENERATOR}" -DCMAKE_INSTALL_PREFIX="${FFMPEG_INSTALL_PREFIX}" -DCMAKE_BUILD_TYPE=Release -DENABLE_SHARED=ON -DENABLE_STATIC=OFF -DWITH_JPEG8=ON -DWITH_TURBOJPEG=ON -DWITH_ZLIB=ON
    echo "Building libjpeg-turbo with ${NUM_CORES} cores..."
    if [ "${GENERATOR}" = "MinGW Makefiles" ]; then
        mingw32-make -j${NUM_CORES} || make -j${NUM_CORES}
        if [ $? -ne 0 ]; then
            echo "Build failed with mingw32-make, trying make..."
            make -j${NUM_CORES}
        fi
    else
        make -j${NUM_CORES}
    fi
    echo "Installing libjpeg-turbo..."
    if [ "${GENERATOR}" = "MinGW Makefiles" ]; then
        mingw32-make install || make install
    else
        make install
    fi
    cd "${BUILD_DIR}"
    rm -rf "libjpeg-turbo-${LIBJPEG_TURBO_VERSION}"
    echo "✓ libjpeg-turbo built and installed (shared)"
else
    echo "✓ libjpeg-turbo (shared) already installed"
fi

# Determine optional flags for QSV (libmfx) and NVENC
# For a shared build we avoid forcing static linking flags
# Default: Enable GPU acceleration unless explicitly disabled
EXTRA_CFLAGS="-I${FFMPEG_INSTALL_PREFIX}/include"
EXTRA_LDFLAGS="-L${FFMPEG_INSTALL_PREFIX}/lib -lz -lbz2 -llzma -lwinpthread"

# Enable GPU acceleration by default (Intel QSV enabled, NVENC disabled by default)
: "${ENABLE_LIBMFX:=1}"
: "${ENABLE_NVENC:=1}"
: "${AUTO_INSTALL_FFNV:=1}"

# libmfx (QSV): build-time support is enabled whenever ENABLE_LIBMFX=1.
# pkg-config is optional; if it is missing we still pass --enable-libmfx and let
# the ffmpeg code dynamically load mfx.dll from the driver at runtime.  The
# earlier behaviour of disabling libmfx when pkg-config could not find the
# library meant that *no QSV code was built at all*, which is why Intel
# hardware decoding never worked unless a matching pkg-config package was
# installed on the build machine.
if [ "${ENABLE_LIBMFX:-0}" = "1" ]; then
    # Always enable the library and the common decoders we care about.
    ENABLE_LIBMFX="--enable-libmfx \
        --enable-decoder=mjpeg_qsv \
        --enable-decoder=h264_qsv \
        --enable-decoder=hevc_qsv \
        --enable-decoder=vp9_qsv \
        --enable-decoder=vc1_qsv \
        --enable-decoder=mpeg2_qsv \
        --enable-decoder=vp8_qsv" 
        
    if pkg-config --exists libmfx; then
        echo "✓ libmfx pkg-config found; compiling with full QSV support"
    else
        echo "⚠ libmfx pkg-config not found; building with dynamic load."
        echo "   Ensure that the Intel Media SDK/driver is present on the target machine."
    fi
else
    echo "libmfx not enabled; QSV support will be attempted at runtime if available via drivers/SDK (dynamic loading by ffmpeg)."
fi

# CUDA/NVENC: Auto-detect and enable when possible
# Default: disabled (avoid accidental configure failures)
NVENC_ARG="--disable-nvenc"
CUDA_FLAGS=""

# Check if NVENC is explicitly disabled
if [ "${ENABLE_NVENC:-0}" = "0" ]; then
    echo "NVENC explicitly disabled (ENABLE_NVENC=0 or unset); skipping all NVENC/CUDA detection"
    NVENC_ARG="--disable-nvenc"
    CUDA_FLAGS="--disable-cuda --disable-cuvid --disable-nvdec --disable-ffnvcodec"
else
    echo "NVENC enabled (ENABLE_NVENC=1); attempting NVENC/CUDA detection..."

    # Helper to build nv-codec-headers (ffnvcodec) into FFMPEG_INSTALL_PREFIX
    build_ffnvcodec_headers() {
        echo "Attempting to build nv-codec-headers (ffnvcodec) into ${FFMPEG_INSTALL_PREFIX}..."
        # Create a temporary working dir (fallback to ${BUILD_DIR} if mktemp fails)
        TMPDIR=$(mktemp -d 2>/dev/null || true)
        if [ -z "${TMPDIR}" ] || [ ! -d "${TMPDIR}" ]; then
            TMPDIR="${BUILD_DIR}/nv-code-headers-tmp.$$"
            mkdir -p "${TMPDIR}"
        fi
        echo "Using temp dir: ${TMPDIR}"
        cd "${TMPDIR}"

        git clone https://github.com/FFmpeg/nv-codec-headers.git
        cd nv-codec-headers
        # Try to use a tag compatible with FFmpeg 6.x (need 12.1+ for FFmpeg 6.1)
        git fetch --tags 2>/dev/null || true
        # Try newer tags that are compatible with FFmpeg 6.1
        if git rev-list -n 1 n12.1.14.0 >/dev/null 2>&1; then
            git checkout n12.1.14.0
        elif git rev-list -n 1 n12.0.16.1 >/dev/null 2>&1; then
            git checkout n12.0.16.1
        else
            echo "Warning: Using latest nv-codec-headers from main branch"
        fi

        make PREFIX="${FFMPEG_INSTALL_PREFIX}" || { echo "ERROR: building nv-codec-headers failed"; cd "${BUILD_DIR}"; rm -rf "${TMPDIR}"; return 1; }
        make PREFIX="${FFMPEG_INSTALL_PREFIX}" install || { echo "ERROR: installing nv-codec-headers failed"; cd "${BUILD_DIR}"; rm -rf "${TMPDIR}"; return 1; }

        # Locate installed .pc file and set PKG_CONFIG_PATH using MSYS-style path
        find_pc() {
            # First try MSYS-style path
            if [ -f "${FFMPEG_INSTALL_PREFIX}/lib/pkgconfig/ffnvcodec.pc" ]; then
                echo "${FFMPEG_INSTALL_PREFIX}/lib/pkgconfig"
                return 0
            fi
            # Try Windows-style C:/... path
            winp=$(echo "${FFMPEG_INSTALL_PREFIX}" | sed -E 's|^/([a-zA-Z])/(.*)|\U\1:/\2|')
            if [ -f "${winp}/lib/pkgconfig/ffnvcodec.pc" ]; then
                # Convert to MSYS path
                drive=$(echo "$winp" | cut -c1 | tr '[:upper:]' '[:lower:]')
                rest=$(echo "$winp" | sed -E 's|^[A-Za-z]:/(.*)|\1|')
                echo "/${drive}/${rest}/lib/pkgconfig"
                return 0
            fi
            return 1
        }

        if pkgdir=$(find_pc); then
            export PKG_CONFIG_PATH="${pkgdir}:${PKG_CONFIG_PATH:-}"
            echo "Updated PKG_CONFIG_PATH to include: ${pkgdir}"
        else
            echo "WARNING: ffnvcodec.pc not found under ${FFMPEG_INSTALL_PREFIX} (checked MSYS and Windows-style paths)."
            echo "Contents of ${FFMPEG_INSTALL_PREFIX} (for debug):"
            ls -la "${FFMPEG_INSTALL_PREFIX}" || true
        fi

        # Quick debug: try pkg-config now; if it fails attempt to copy the .pc into common MSYS pkgconfig dirs
        if ! pkg-config --exists ffnvcodec >/dev/null 2>&1; then
            echo "pkg-config cannot find ffnvcodec yet; attempting to copy .pc into known MSYS pkgconfig locations..."
            PC_SRC="${pkgdir}/ffnvcodec.pc"
            for dest in "/c/msys64/mingw64/lib/pkgconfig" "/mingw64/lib/pkgconfig" "/c/msys64/usr/lib/pkgconfig"; do
                if [ -d "$dest" ]; then
                    prefix_dir=$(echo "$dest" | sed -E 's|/lib/pkgconfig$||')
                    echo "Copying $PC_SRC to $dest with prefix=$prefix_dir"
                    sed "s|^prefix=.*|prefix=${prefix_dir}|" "$PC_SRC" > "$dest/ffnvcodec.pc" || continue
                    export PKG_CONFIG_PATH="$dest:${PKG_CONFIG_PATH:-}"
                    echo "Updated PKG_CONFIG_PATH to include: $dest"
                    break
                fi
            done
        fi

        # Final check
        if pkg-config --exists ffnvcodec >/dev/null 2>&1; then
            echo "ffnvcodec is now discoverable via pkg-config"
        else
            echo "ERROR: auto-install completed but pkg-config still cannot find ffnvcodec"
            echo "Checked paths: ${PKG_CONFIG_PATH}"
            echo "Contents of any pkgconfig dirs:"
            for p in $(echo "${PKG_CONFIG_PATH}" | tr ':' '\n'); do ls -la "$p" 2>/dev/null || true; done
            cd "${BUILD_DIR}"
            rm -rf "${TMPDIR}"
            return 1
        fi

        cd "${BUILD_DIR}"
        rm -rf "${TMPDIR}"
        echo "nv-codec-headers installed into ${FFMPEG_INSTALL_PREFIX}"
        return 0
    }

    # Detection strategy (in order):
    # 1) pkg-config ffnvcodec (packaged headers/libs)
    # 2) NVENC SDK present (nvEncodeAPI.h)
    # 3) If AUTO_INSTALL_FFNV=1: attempt to build nv-codec-headers automatically into FFMPEG_INSTALL_PREFIX
    
    # helper: ensure prefix header contains new fields
    nvenc_header_valid() {
        hdr="${FFMPEG_INSTALL_PREFIX}/include/nvEncodeAPI.h"
        if [ -f "$hdr" ] && grep -q 'pixelBitDepthMinus8' "$hdr"; then
            return 0
        fi
        return 1
    }

    if pkg-config --exists ffnvcodec >/dev/null 2>&1; then
        if nvenc_header_valid; then
            echo "ffnvcodec detected via pkg-config; enabling NVENC/ffnvcodec support"
            NVENC_ARG="--enable-nvenc"
            # Use pkg-config to get cflags/libs if available
            EXTRA_CFLAGS="${EXTRA_CFLAGS} $(pkg-config --cflags ffnvcodec 2>/dev/null || true)"
            EXTRA_LDFLAGS="${EXTRA_LDFLAGS} $(pkg-config --libs-only-L ffnvcodec 2>/dev/null || true) $(pkg-config --libs-only-l ffnvcodec 2>/dev/null || true)"
            CUDA_FLAGS="--enable-cuda --enable-cuvid --enable-nvdec --enable-ffnvcodec --enable-decoder=h264_cuvid --enable-decoder=hevc_cuvid --enable-decoder=mjpeg_cuvid"
        else
            echo "⚠ ffnvcodec pkg-config exists but header in ${FFMPEG_INSTALL_PREFIX}/include is missing or outdated; removing stub and outdated header, then continuing detection."
            rm -f "${FFMPEG_INSTALL_PREFIX}/lib/pkgconfig/ffnvcodec.pc"
            rm -f "${FFMPEG_INSTALL_PREFIX}/include/nvEncodeAPI.h"
        fi
    fi

    # continue with SDK detection only if NVENC_ARG not set
    if [ "${NVENC_ARG}" != "--enable-nvenc" ]; then
        # Try NVENC SDK headers if provided by user or found in common locations
        NVENC_SDK_PATH="${NVENC_SDK_PATH:-/c/Program Files/NVIDIA Video Codec SDK}"
        
        # helper: check nvEncodeAPI.h contains fields introduced in 12.x headers
        nvenc_sdk_valid() {
            hdr="$1/include/nvEncodeAPI.h"
            if [ -f "$hdr" ] && grep -q 'pixelBitDepthMinus8' "$hdr"; then
                return 0
            fi
            return 1
        }

        if nvenc_sdk_valid "${NVENC_SDK_PATH}"; then
            echo "NVENC SDK headers found and appear up-to-date: ${NVENC_SDK_PATH}/include"
            NVENC_ARG="--enable-nvenc"
            EXTRA_CFLAGS="${EXTRA_CFLAGS} -I${NVENC_SDK_PATH}/include"
            # If ffnvcodec pkg-config is absent we still attempt to enable CUDA decoders, but configure may fail if libraries are missing
            if pkg-config --exists ffnvcodec >/dev/null 2>&1; then
                CUDA_FLAGS="--enable-cuda --enable-cuvid --enable-nvdec --enable-ffnvcodec --enable-decoder=h264_cuvid --enable-decoder=hevc_cuvid --enable-decoder=mjpeg_cuvid"
            else
                echo "Warning: ffnvcodec pkg-config not found. Enabling CUDA/NVENC may still fail if runtime libraries are missing."
                CUDA_FLAGS="--enable-cuda --enable-cuvid --enable-nvdec --enable-decoder=h264_cuvid --enable-decoder=hevc_cuvid --enable-decoder=mjpeg_cuvid"
            fi
        else
            if [ -f "${NVENC_SDK_PATH}/include/nvEncodeAPI.h" ]; then
                echo "⚠ NVENC SDK headers at ${NVENC_SDK_PATH} appear too old or incompatible; ignoring them and falling back to auto-install if enabled."
            else
                echo "NVENC SDK headers not found in ${NVENC_SDK_PATH}."
            fi
            # clear the path so the later logic doesn't mistake them for valid headers
            NVENC_SDK_PATH=""
            # Try auto-install if requested, or automatically when ENABLE_NVENC=1
            # the previous branch may have ignored an out‑of‑date SDK by clearing NVENC_SDK_PATH
            if [ "${AUTO_INSTALL_FFNV:-0}" = "1" ] || [ "${ENABLE_NVENC:-0}" = "1" ]; then
                if [ "${ENABLE_NVENC:-0}" = "1" ] && [ "${AUTO_INSTALL_FFNV:-0}" = "0" ]; then
                    echo "ENABLE_NVENC=1 detected but no NVENC dependencies found. Automatically enabling AUTO_INSTALL_FFNV=1..."
                    AUTO_INSTALL_FFNV=1
                fi
                echo "AUTO_INSTALL_FFNV=1: attempting to build and install nv-codec-headers into ${FFMPEG_INSTALL_PREFIX}"
                if build_ffnvcodec_headers; then
                    if pkg-config --exists ffnvcodec >/dev/null 2>&1; then
                        echo "ffnvcodec now available after auto-install; enabling NVENC/ffnvcodec support"
                        NVENC_ARG="--enable-nvenc"
                        EXTRA_CFLAGS="${EXTRA_CFLAGS} $(pkg-config --cflags ffnvcodec 2>/dev/null || true)"
                        EXTRA_LDFLAGS="${EXTRA_LDFLAGS} $(pkg-config --libs-only-L ffnvcodec 2>/dev/null || true) $(pkg-config --libs-only-l ffnvcodec 2>/dev/null || true)"
                        CUDA_FLAGS="--enable-cuda --enable-cuvid --enable-nvdec --enable-ffnvcodec --enable-decoder=h264_cuvid --enable-decoder=hevc_cuvid --enable-decoder=mjpeg_cuvid"
                    else
                        echo "⚠ auto-install completed but pkg-config still cannot find ffnvcodec; disabling NVENC"
                        NVENC_ARG="--disable-nvenc"
                        CUDA_FLAGS="--disable-cuda --disable-cuvid --disable-nvdec --disable-ffnvcodec"
                    fi
                else
                    echo "⚠ auto-install of nv-codec-headers failed; disabling NVENC"
                    NVENC_ARG="--disable-nvenc"
                    CUDA_FLAGS="--disable-cuda --disable-cuvid --disable-nvdec --disable-ffnvcodec"
                fi
            else
                echo "⚠ ENABLE_NVENC=1 but neither ffnvcodec pkg-config nor NVENC SDK headers found (looked at ${NVENC_SDK_PATH}). Disabling NVENC."
                NVENC_ARG="--disable-nvenc"
                CUDA_FLAGS="--disable-cuda --disable-cuvid --disable-nvdec --disable-ffnvcodec"
            fi
        fi
    fi
fi

# Print detection summary for the user
echo "NVENC detection: NVENC_ARG='${NVENC_ARG}' CUDA_FLAGS='${CUDA_FLAGS}' EXTRA_CFLAGS='${EXTRA_CFLAGS}' EXTRA_LDFLAGS='${EXTRA_LDFLAGS}'"

# If NVENC disabled, remove ffnvcodec.pc to prevent FFmpeg from detecting it
if [ "${ENABLE_NVENC:-0}" = "0" ]; then
    rm -f "${FFMPEG_INSTALL_PREFIX}/lib/pkgconfig/ffnvcodec.pc"
fi

echo ""

# Download FFmpeg source
echo "Step 4/8: Downloading FFmpeg ${FFMPEG_VERSION}..."
if [ ! -f "ffmpeg-${FFMPEG_VERSION}.tar.bz2" ]; then
    wget "${DOWNLOAD_URL}" -O "ffmpeg-${FFMPEG_VERSION}.tar.bz2"
    echo "✓ Downloaded FFmpeg source"
else
    echo "✓ FFmpeg source already downloaded"
fi

echo ""

# Extract source
echo "Step 5/8: Extracting source code..."
if [ ! -d "ffmpeg-${FFMPEG_VERSION}" ]; then
    tar -xf "ffmpeg-${FFMPEG_VERSION}.tar.bz2"
    echo "✓ Source extracted"
else
    echo "✓ Source already extracted"
fi
cd "ffmpeg-${FFMPEG_VERSION}"
echo ""

# Avoid sed errors due to Windows-style temp paths (e.g., C:\Users\RUNNER~1\...)
# Use the build directory itself for temp files to avoid permission issues
CLEAN_TMP="${BUILD_DIR}/temp"
mkdir -p "$CLEAN_TMP"
# Clean any stale files
rm -rf "$CLEAN_TMP"/* 2>/dev/null || true
# Verify write access by testing with gcc (not just touch)
echo "int main() { return 0; }" > "$CLEAN_TMP/test.c"
if ! gcc -c "$CLEAN_TMP/test.c" -o "$CLEAN_TMP/test.o" 2>/dev/null; then
    echo "WARNING: GCC cannot write to $CLEAN_TMP, falling back to MSYS /tmp"
    CLEAN_TMP="/tmp/ffmpeg-build-$$"
    mkdir -p "$CLEAN_TMP"
fi
rm -f "$CLEAN_TMP/test.c" "$CLEAN_TMP/test.o" 2>/dev/null || true

# Set temp environment variables - use both MSYS and Windows-style paths
export TMP="$CLEAN_TMP"
export TEMP="$CLEAN_TMP"
export TMPDIR="$CLEAN_TMP"

# Also clear any Windows-style temp vars that might interfere
unset ORIGINAL_PATH MSYSTEM_PREFIX CHERE_INVOKING

echo "Using temporary directory: $TMPDIR"
echo "Verifying temp directory is writable for GCC..."
if gcc -xc -c -o "$TMPDIR/gcc-test.o" - <<< "int main(){return 0;}" 2>/dev/null; then
    echo "✓ GCC can write to temp directory"
    rm -f "$TMPDIR/gcc-test.o"
else
    echo "✗ WARNING: GCC may have issues writing to temp directory"
fi

# Configure FFmpeg for shared build
echo "Step 6/8: Configuring FFmpeg for shared build..."
echo "This may take a few minutes..."
echo ""

# Set PKG_CONFIG_PATH to find libjpeg-turbo
export PKG_CONFIG_PATH="${FFMPEG_INSTALL_PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

# Print effective options for debugging
echo "CONFIGURE OPTIONS: NVENC_ARG='${NVENC_ARG}' CUDA_FLAGS='${CUDA_FLAGS}' ENABLE_LIBMFX='${ENABLE_LIBMFX}' EXTRA_CFLAGS='${EXTRA_CFLAGS}' EXTRA_LDFLAGS='${EXTRA_LDFLAGS}'"

echo "Running: ./configure --prefix='${FFMPEG_INSTALL_PREFIX}' --enable-shared --disable-static ..."

CONFIG_LOG="${BUILD_DIR}/ffmpeg-configure.log"
echo "Configure output will be saved to: ${CONFIG_LOG}"

# Run configure and capture exit status more reliably
set +e  # Temporarily disable exit on error
./configure \
    --prefix="${FFMPEG_INSTALL_PREFIX}" \
    --arch=x86_64 \
    --target-os=mingw32 \
    --enable-shared \
    --disable-static \
    --enable-gpl \
    --enable-version3 \
    --enable-nonfree \
    --disable-debug \
    --disable-programs \
    --disable-doc \
    --disable-htmlpages \
    --disable-manpages \
    --disable-podpages \
    --disable-txtpages \
    --disable-outdevs \
    --enable-avcodec \
    --enable-avformat \
    --enable-avutil \
    --enable-swresample \
    --enable-swscale \
    --enable-avdevice \
    --enable-avfilter \
    --enable-postproc \
    --enable-network \
    --enable-runtime-cpudetect \
    --enable-pthreads \
    --disable-w32threads \
    --enable-zlib \
    --enable-bzlib \
    --enable-lzma \
    ${ENABLE_LIBMFX} \
    --enable-dxva2 \
    --enable-d3d11va \
    --enable-hwaccels \
    --enable-decoder=mjpeg \
    ${NVENC_ARG} ${CUDA_FLAGS} \
    --pkg-config-flags="" \
    --extra-cflags="${EXTRA_CFLAGS}" \
    --extra-ldflags="${EXTRA_LDFLAGS}" \
    > "${CONFIG_LOG}" 2>&1

CONFIGURE_EXIT_CODE=$?
set -e  # Re-enable exit on error

# Always show the configure output for debugging
cat "${CONFIG_LOG}"

if [ $CONFIGURE_EXIT_CODE -ne 0 ]; then
    echo ""
    echo "✗ FFmpeg configure failed with exit code $CONFIGURE_EXIT_CODE!"
    echo "Configure log saved to: ${CONFIG_LOG}"
    echo "Last 50 lines of configure output:"
    tail -n 50 "${CONFIG_LOG}" || true
    exit 1
fi

echo ""
echo "✓ FFmpeg configure completed successfully!"

# Build FFmpeg
echo "Step 7/8: Building FFmpeg..."
echo "This will take 30-60 minutes depending on your CPU..."
echo "Using 2 CPU cores for compilation (reduced for stability)"
echo ""

# Capture build output for debugging
BUILD_LOG="${BUILD_DIR}/ffmpeg-build.log"
echo "Build output will be saved to: ${BUILD_LOG}"

make -j2 > "${BUILD_LOG}" 2>&1

if [ $? -ne 0 ]; then
    echo "✗ FFmpeg build failed!"
    echo "Build output (last 100 lines):"
    tail -100 "${BUILD_LOG}" || true
    exit 1
fi

# Ensure install directory exists
mkdir -p "${FFMPEG_INSTALL_PREFIX}"

# Install FFmpeg
echo "Step 8/8: Installing FFmpeg to ${FFMPEG_INSTALL_PREFIX}..."
make install

echo "✓ Build and installation complete"
echo ""

# After install, make sure pkgconfig files for the optional GPU libraries
# are present in the prefix.  These are not strictly required for the
# runtime, but the user was expecting to see libmfx.pc/ffnvcodec.pc in the
# output, so we copy or emit tiny stubs here.

# (dynamic GPU libraries such as nvcuvid.dll, cudart*.dll or libmfx.dll are
# provided by the driver/Intel Media SDK and are **not** built by this script;
# they live in C:\Windows\System32 on machines with the appropriate driver.)

echo "Step 8a/8: installing helper pkg-config stubs for GPU libraries..."
mkdir -p "${FFMPEG_INSTALL_PREFIX}/lib/pkgconfig"
if [ "${ENABLE_LIBMFX:-0}" = "1" ]; then
    if [ -f "/mingw64/lib/pkgconfig/libmfx.pc" ]; then
        cp "/mingw64/lib/pkgconfig/libmfx.pc" "${FFMPEG_INSTALL_PREFIX}/lib/pkgconfig/"
        echo "✓ copied libmfx.pc from system pkgconfig"
    else
        cat > "${FFMPEG_INSTALL_PREFIX}/lib/pkgconfig/libmfx.pc" <<'EOF'
prefix=${FFMPEG_INSTALL_PREFIX}
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: libmfx
Description: Intel Media SDK (stub)
Version: unknown
Libs: -lmfx
Cflags: -I\${includedir}
EOF
        echo "✓ generated stub libmfx.pc (headers only)"
    fi
fi

# NVENC support: copy header if available, and warn about runtime libraries
if [ "${NVENC_ARG:-}" = "--enable-nvenc" ]; then
    # ensure pkgconfig stub exists
    if [ ! -f "${FFMPEG_INSTALL_PREFIX}/lib/pkgconfig/ffnvcodec.pc" ]; then
        cat > "${FFMPEG_INSTALL_PREFIX}/lib/pkgconfig/ffnvcodec.pc" <<'EOF'
prefix=${FFMPEG_INSTALL_PREFIX}
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: ffnvcodec
Description: NVIDIA NVCodec headers (stub)
Version: unknown
Libs:
Cflags: -I\${includedir}
EOF
        echo "✓ generated stub ffnvcodec.pc (headers only)"
    fi
    # install header from SDK or auto-installed headers if present
    if [ -n "${NVENC_SDK_PATH:-}" ] && [ -f "${NVENC_SDK_PATH}/include/nvEncodeAPI.h" ]; then
        cp "${NVENC_SDK_PATH}/include/nvEncodeAPI.h" "${FFMPEG_INSTALL_PREFIX}/include/" 2>/dev/null || true
        echo "✓ copied nvEncodeAPI.h from SDK into prefix/include"
        # copy runtime dll if bundled with SDK (some SDK versions include it)
        for dll in "nvEncodeAPI.dll" "nvEncodeAPI64.dll"; do
            if [ -f "${NVENC_SDK_PATH}/lib/x64/$dll" ]; then
                cp "${NVENC_SDK_PATH}/lib/x64/$dll" "${FFMPEG_INSTALL_PREFIX}/bin/" 2>/dev/null || true
                echo "✓ copied $dll from SDK to prefix/bin"
            fi
        done
    elif [ -f "${FFMPEG_INSTALL_PREFIX}/include/nvEncodeAPI.h" ]; then
        echo "✓ nvEncodeAPI.h already present in prefix/include"
    else
        echo "⚠ nvEncodeAPI.h not found; header support may be missing."
    fi
    # runtime libraries are not bundled, warn if missing
    if [ ! -f "/c/Windows/System32/nvcuvid.dll" ] && [ ! -f "/c/Windows/System32/cuda.dll" ]; then
        echo "⚠ GPU runtime libraries (nvcuvid.dll/cuda.dll) not found on build host; they will be required at runtime."
    fi
fi

echo "============================================================================"
echo "Verifying installation (shared)..."
echo "============================================================================"

# Check for DLLs
if ls "${FFMPEG_INSTALL_PREFIX}/bin/"*.dll >/dev/null 2>&1 || ls "${FFMPEG_INSTALL_PREFIX}/lib/"*.dll >/dev/null 2>&1; then
    echo "✓ FFmpeg shared libraries (DLLs) installed successfully!"

    # Check for import libraries (.dll.a) required by CMake
    echo "Checking for FFmpeg import libraries (.dll.a)..."
    MISSING_LIBS=0
    for lib in avdevice avfilter avformat avcodec swresample swscale avutil; do
        if [ -f "${FFMPEG_INSTALL_PREFIX}/lib/lib${lib}.dll.a" ]; then
            echo "✓ Found: lib${lib}.dll.a"
        else
            echo "✗ Missing: lib${lib}.dll.a"
            MISSING_LIBS=1
        fi
    done

    if [ $MISSING_LIBS -eq 1 ]; then
        echo "✗ Some FFmpeg import libraries are missing. Build may have failed for some components."
        exit 1
    fi

    echo "✓ All required FFmpeg import libraries found!"
    echo ""

    echo "Installed FFmpeg libraries (DLLs):"
    ls -lh "${FFMPEG_INSTALL_PREFIX}/bin/"*.dll 2>/dev/null || true
    ls -lh "${FFMPEG_INSTALL_PREFIX}/lib/"*.dll 2>/dev/null || true
    echo ""
    echo "Installed FFmpeg import libraries (.dll.a):"
    ls -lh "${FFMPEG_INSTALL_PREFIX}/lib/"libav*.dll.a 2>/dev/null || true
    ls -lh "${FFMPEG_INSTALL_PREFIX}/lib/"libsw*.dll.a 2>/dev/null || true
    echo ""
    echo "Installed libjpeg-turbo libraries (DLLs):"
    ls -lh "${FFMPEG_INSTALL_PREFIX}/bin/"*jpeg*.dll 2>/dev/null || ls -lh "${FFMPEG_INSTALL_PREFIX}/lib/"*jpeg*.dll 2>/dev/null || true
    echo ""
    echo "Include directories:"
    ls -d "${FFMPEG_INSTALL_PREFIX}/include/"lib* 2>/dev/null || true
    echo ""
    echo "============================================================================"
    echo "Installation Summary"
    echo "============================================================================"
    echo "Install Path: ${FFMPEG_INSTALL_PREFIX}"
    echo "Libraries (DLLs): ${FFMPEG_INSTALL_PREFIX}/bin or ${FFMPEG_INSTALL_PREFIX}/lib"
    echo "Headers: ${FFMPEG_INSTALL_PREFIX}/include"
    echo "pkg-config: ${FFMPEG_INSTALL_PREFIX}/lib/pkgconfig"
    echo ""
    echo "Components installed (shared):"
    echo "  ✓ FFmpeg ${FFMPEG_VERSION} (shared)"
    if [ "${NVENC_ARG:-}" = "--enable-nvenc" ]; then
        echo "    - built with NVIDIA NVENC support"
    fi
    echo "  ✓ libjpeg-turbo ${LIBJPEG_TURBO_VERSION} (shared)"
    echo ""
    echo "============================================================================"
else
    echo "✗ Installation verification failed (no DLLs found)!"
    exit 1
fi

echo ""
echo "Build completed successfully!"
echo ""
echo "To use this FFmpeg in your CMake project (shared):"
echo "  set FFMPEG_PREFIX=${FFMPEG_INSTALL_PREFIX}"
echo "  or pass -DFFMPEG_PREFIX=${FFMPEG_INSTALL_PREFIX} to cmake"
echo ""
echo "> Note: For runtime, ensure ${FFMPEG_INSTALL_PREFIX}/bin is in your PATH or bundle the DLLs with your application."

echo ""
exit 0
