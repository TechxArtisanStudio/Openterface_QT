#!/usr/bin/env bash
set -euo pipefail

# Cross-platform Bash script to build static Qt (MSYS/MinGW on Windows and Unix-like systems)
# Usage: ./build-static-qt-from-source.sh [<SOURCE_DIR>] [<VCPKG_ROOT>]

# Defaults (override via env or args)
SOURCE_DIR="${1:-$(pwd)}"
VCPKG_ROOT="${2:-${VCPKG_ROOT:-}}"

QT_VERSION="6.6.3"
QT_MAJOR_VERSION="6.6"
INSTALL_PREFIX="/c/Qt6"
BUILD_DIR="${SOURCE_DIR}/qt-build"
# Keep downloaded zip archives? Set KEEP_ZIPS=0 to remove them after extraction.
KEEP_ZIPS="${KEEP_ZIPS:-1}"
# Number of parallel ninja jobs. Override with JOBS env (e.g., JOBS=1 ./script)
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 1)}"
MODULES=(qtbase qtshadertools qtmultimedia qtsvg qtserialport qttools)
DOWNLOAD_BASE_URL="https://download.qt.io/archive/qt/${QT_MAJOR_VERSION}/${QT_VERSION}/submodules"

# Detect platform
uname_s=$(uname -s || echo unknown)
case "$uname_s" in
  MINGW*|MSYS*|MSYS_NT*)
    PLATFORM="windows"
    # sensible defaults for MSYS/MinGW environment
    VCPKG_ROOT="${VCPKG_ROOT:-/c/vcpkg}"
    VCPKG_TRIPLET="x64-mingw-static"
    # prefer /mingw64 if present
    MINGW_PATH="${MINGW_PATH:-/mingw64}"
    ;;
  Darwin*)
    PLATFORM="darwin"
    VCPKG_ROOT="${VCPKG_ROOT:-/usr/local/vcpkg}"
    VCPKG_TRIPLET="x64-osx"
    ;;
  Linux*)
    PLATFORM="linux"
    VCPKG_ROOT="${VCPKG_ROOT:-/usr/local/vcpkg}"
    VCPKG_TRIPLET="x64-linux"
    ;;
  *)
    PLATFORM="unknown"
    VCPKG_ROOT="${VCPKG_ROOT:-/usr/local/vcpkg}"
    VCPKG_TRIPLET="x64"
    ;;
esac

# Prefer lld on Windows if available (helps avoid GNU ld running out of memory)
LLD_FLAGS=""
if [ "$PLATFORM" = "windows" ]; then
  if command -v ld.lld >/dev/null 2>&1 || command -v lld >/dev/null 2>&1; then
    LLD_FLAGS="-fuse-ld=lld"
    echo "INFO: lld detected; will add '${LLD_FLAGS}' to linker flags"
  else
    echo "INFO: lld not found; install with: pacman -S mingw-w64-x86_64-lld (optional but recommended to avoid ld OOM)"
  fi
fi

# Convert to Windows paths if needed (cygpath available)
winpath() {
  if command -v cygpath >/dev/null 2>&1; then
    cygpath -w "$1"
  else
    echo "$1"
  fi
}

# OpenSSL location: prefer MSYS/Mingw-provided OpenSSL on Windows, otherwise use repo-local or central vcpkg as fallback
if [ -n "${OPENSSL_ROOT:-}" ] && [ -d "${OPENSSL_ROOT}" ]; then
  # USER_OVERRIDE: OPENSSL_ROOT pre-set in environment
  :
else
  if [ "$PLATFORM" = "windows" ]; then
    MSYS_OPENSSL_DIR="${MINGW_PATH}"
    if [ -d "${MSYS_OPENSSL_DIR}/lib" ] && [ -f "${MSYS_OPENSSL_DIR}/lib/libssl.a" ]; then
      OPENSSL_ROOT="${MSYS_OPENSSL_DIR}"
      echo "INFO: Using MSYS OpenSSL at ${OPENSSL_ROOT}"
    else
      # Try repo-local vcpkg_installed as a fallback but warn (vcpkg is not preferred)
      REPO_VCPKG_INSTALLED="${SOURCE_DIR}/vcpkg_installed/${VCPKG_TRIPLET}"
      if [ -d "$REPO_VCPKG_INSTALLED" ]; then
        echo "WARNING: MSYS OpenSSL not found; falling back to repo-local vcpkg installed at $REPO_VCPKG_INSTALLED"
        OPENSSL_ROOT="$REPO_VCPKG_INSTALLED"
      else
        echo "ERROR: No suitable OpenSSL found. Install MSYS OpenSSL (pacman -S mingw-w64-x86_64-openssl) or set OPENSSL_ROOT to a valid path." >&2
        exit 1
      fi
    fi
  else
    # Non-Windows: prefer repo-local vcpkg install, otherwise central vcpkg
    REPO_VCPKG_INSTALLED="${SOURCE_DIR}/vcpkg_installed/${VCPKG_TRIPLET}"
    CENTRAL_VCPKG_INSTALLED="${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}"
    if [ -d "$REPO_VCPKG_INSTALLED" ]; then
      echo "INFO: Using repo-local vcpkg installed OpenSSL at $REPO_VCPKG_INSTALLED"
      OPENSSL_ROOT="$REPO_VCPKG_INSTALLED"
    elif [ -d "$CENTRAL_VCPKG_INSTALLED" ]; then
      echo "INFO: Using central vcpkg installed OpenSSL at $CENTRAL_VCPKG_INSTALLED"
      OPENSSL_ROOT="$CENTRAL_VCPKG_INSTALLED"
    else
      echo "ERROR: No OpenSSL found. Set OPENSSL_ROOT to a valid path or install via vcpkg or system package manager." >&2
      exit 1
    fi
  fi
fi
OPENSSL_LIB_DIR="$OPENSSL_ROOT/lib"
OPENSSL_INCLUDE_DIR="$OPENSSL_ROOT/include"

# Sanity checks for required tools
for cmd in curl cmake ninja; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "ERROR: Required command '$cmd' not found. Please install it and retry." >&2
    exit 1
  fi
done

# Determine a zip extraction method (unzip preferred, then bsdtar/tar/7z/python/pwsh)
extract_zip() {
  local zipfile="$1"
  local outdir="$2"
  mkdir -p "$outdir"
  if command -v unzip >/dev/null 2>&1; then
    unzip -q "$zipfile" -d "$outdir"
  elif command -v bsdtar >/dev/null 2>&1; then
    bsdtar -xf "$zipfile" -C "$outdir"
  elif command -v tar >/dev/null 2>&1; then
    tar -xf "$zipfile" -C "$outdir"
  elif command -v 7z >/dev/null 2>&1; then
    7z x "$zipfile" -o"$outdir" >/dev/null
  elif command -v python3 >/dev/null 2>&1 || command -v python >/dev/null 2>&1; then
    local py
    if command -v python3 >/dev/null 2>&1; then
      py=python3
    else
      py=python
    fi
    "$py" -c "import sys,zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$zipfile" "$outdir"
  elif command -v pwsh >/dev/null 2>&1; then
    pwsh -NoProfile -Command "Expand-Archive -Path '$zipfile' -DestinationPath '$outdir' -Force"
  elif command -v powershell >/dev/null 2>&1; then
    powershell -NoProfile -Command "Expand-Archive -Path '$zipfile' -DestinationPath '$outdir' -Force"
  else
    echo "ERROR: No zip extraction tool found. Install 'unzip', 'bsdtar', '7z', or python/powershell and retry." >&2
    exit 1
  fi
}

if [ ! -d "$OPENSSL_LIB_DIR" ]; then
  echo "INFO: OpenSSL lib folder not found at $OPENSSL_LIB_DIR"
  if [ "$PLATFORM" = "windows" ]; then
    echo "Install MSYS OpenSSL with: pacman -S mingw-w64-x86_64-openssl, or set OPENSSL_ROOT to point to a valid installation."
  else
    echo "You may need to run 'vcpkg install openssl --triplet=${VCPKG_TRIPLET}' or set OPENSSL_ROOT to a folder that contains libssl.a and libcrypto.a"
  fi
fi

if [ ! -f "$OPENSSL_LIB_DIR/libcrypto.a" ] || [ ! -f "$OPENSSL_LIB_DIR/libssl.a" ]; then
  echo "ERROR: OpenSSL static libraries libcrypto.a and/or libssl.a not found in $OPENSSL_LIB_DIR" >&2
  echo "Please install OpenSSL static libraries for the selected triplet or set OPENSSL_ROOT to point to a valid install." >&2
  exit 1
fi

if [ ! -f "$OPENSSL_INCLUDE_DIR/openssl/ssl.h" ]; then
  echo "ERROR: OpenSSL headers not found at $OPENSSL_INCLUDE_DIR/openssl" >&2
  exit 1
fi

echo "Using platform: $PLATFORM"
echo "Qt version: $QT_VERSION"
echo "Source dir: $SOURCE_DIR"
echo "Vcpkg root: $VCPKG_ROOT"
echo "OpenSSL root: $OPENSSL_ROOT"
echo "KEEP_ZIPS: ${KEEP_ZIPS} (set KEEP_ZIPS=0 to remove archives after extraction)"
echo "Parallel jobs (JOBS): ${JOBS} (override with JOBS=1 ./build-static-qt-from-source.sh)"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Ensure an existing module directory contains CMakeLists.txt
# Select the best CMakeLists.txt file inside a tree.
# Prefers files containing 'cmake_minimum_required' or 'project(' and avoids test directories.
select_best_cmakelists() {
  local base="$1"
  local candidates
  # Find candidates up to depth 6
  mapfile -t candidates < <(find "$base" -maxdepth 6 -type f -name 'CMakeLists.txt' 2>/dev/null || true)
  local best=""
  for c in "${candidates[@]}"; do
    # Skip obvious test files
    case "$c" in
      */tests/*|*/test/*|*/cmake/tests/*) continue ;;
    esac
    if grep -q -E 'cmake_minimum_required|project\s*\(' "$c" 2>/dev/null; then
      best="$c"
      break
    fi
    if [ -z "$best" ]; then
      best="$c"
    else
      # prefer shallower candidate
      if [ $(awk -F/ '{print NF}' <<< "$c") -lt $(awk -F/ '{print NF}' <<< "$best") ]; then
        best="$c"
      fi
    fi
  done
  echo "$best"
}

ensure_module_has_cmakelists() {
  local mdir="$1"
  if [ -f "$mdir/CMakeLists.txt" ]; then
    return 0
  fi
  # Try to pick the best CMakeLists.txt from the tree
  candidate=$(select_best_cmakelists "$mdir" || true)
  if [ -n "$candidate" ]; then
    rootdir=$(dirname "$candidate")
    if [ "$rootdir" != "$mdir" ]; then
      echo "Normalizing existing module $mdir: moving contents from $rootdir to $mdir"
      tmpdir="${mdir}._tmp"
      mkdir -p "$tmpdir"
      shopt -s dotglob nullglob >/dev/null 2>&1 || true
      mv "$rootdir"/* "$tmpdir"/ 2>/dev/null || cp -a "$rootdir/." "$tmpdir/"
      # Clean target and move
      rm -rf "$mdir"/* || true
      mv "$tmpdir"/* "$mdir"/ 2>/dev/null || cp -a "$tmpdir/." "$mdir/"
      rm -rf "$tmpdir"
      shopt -u dotglob nullglob >/dev/null 2>&1 || true
    fi
    return 0
  fi
  return 1
}

# Function to download & extract module
# Normalizes the extracted layout so the module dir ($m) contains CMakeLists.txt
download_and_extract() {
  local m=$1
  if [ -d "$m" ]; then
    echo "Module $m already present, checking layout..."
    if ensure_module_has_cmakelists "$m"; then
      echo "Module $m layout OK."
    else
      echo "Warning: module $m does not contain CMakeLists.txt and automatic normalization failed."
      echo "Please inspect $m or remove it to force a fresh download."
    fi
    return
  fi
  local zipname="${m}.zip"
  local url="${DOWNLOAD_BASE_URL}/${m}-everywhere-src-${QT_VERSION}.zip"
  echo "Downloading $url ..."
  curl -L -o "$zipname" "$url"
  echo "Extracting $zipname ..."
  extract_zip "$zipname" "${m}-tmp"

  # If the extracted tree already has CMakeLists.txt at the top, just move it
  if [ -f "${m}-tmp/CMakeLists.txt" ]; then
    mv "${m}-tmp" "$m"
  else
    # Try to find a CMakeLists.txt within the extracted tree (depth 1-2)
    candidate=$(find "${m}-tmp" -maxdepth 3 -type f -name "CMakeLists.txt" -print -quit || true)
    if [ -n "$candidate" ]; then
      rootdir=$(dirname "$candidate")
      echo "Detected CMakeLists.txt at $rootdir; normalizing to $m"
      mkdir -p "$m"
      # Move contents of the detected rootdir into the desired module dir
      shopt -s dotglob nullglob || true
      mv "$rootdir"/* "$m"/ 2>/dev/null || cp -a "$rootdir/." "$m/"
      shopt -u dotglob nullglob || true
    else
      # Fallback: try to find a directory matching the module name and move it
      topdir=$(find "${m}-tmp" -maxdepth 2 -type d -name "*${m}*" -print -quit || true)
      if [ -n "$topdir" ]; then
        mv "$topdir" "$m"
      else
        # Fallback: move everything (last resort)
        mv "${m}-tmp" "$m"
      fi
    fi
  fi

  if [ "${KEEP_ZIPS}" = "1" ]; then
    echo "Preserving downloaded archive: $BUILD_DIR/$zipname"
  else
    rm -f "$zipname"
  fi
  rm -rf "${m}-tmp"
}

# Download modules
for m in "${MODULES[@]}"; do
  download_and_extract "$m"
done

# Build qtbase first
echo "Building qtbase..."
# Ensure qtbase has a CMakeLists.txt; try to auto-fix before failing
if ! [ -f "${BUILD_DIR}/qtbase/CMakeLists.txt" ]; then
  echo "qtbase does not contain CMakeLists.txt; attempting to normalize layout..."
  if ensure_module_has_cmakelists "${BUILD_DIR}/qtbase"; then
    echo "Normalization successful."
  else
    echo "ERROR: qtbase is missing CMakeLists.txt after normalization. Here is the directory listing for diagnostics:"
    ls -R "${BUILD_DIR}/qtbase" || true
    echo "Suggestion: remove ${BUILD_DIR}/qtbase and re-run the script to force a clean download/extract."
    exit 1
  fi
fi
mkdir -p qtbase/build
cd qtbase/build

cmake_args=(
  -G "Ninja"
  -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}"
  -DBUILD_SHARED_LIBS=OFF
  -DFEATURE_dbus=ON
  -DFEATURE_sql=OFF
  -DFEATURE_testlib=OFF
  -DFEATURE_icu=OFF
  -DFEATURE_opengl=ON
  -DFEATURE_openssl=ON
  -DINPUT_openssl=linked
  -DOPENSSL_ROOT_DIR="${OPENSSL_ROOT}"
  -DOPENSSL_INCLUDE_DIR="${OPENSSL_INCLUDE_DIR}"
  -DOPENSSL_CRYPTO_LIBRARY="${OPENSSL_LIB_DIR}/libcrypto.a"
  -DOPENSSL_SSL_LIBRARY="${OPENSSL_LIB_DIR}/libssl.a"
  -DCMAKE_C_FLAGS="-I${OPENSSL_INCLUDE_DIR}"
  -DCMAKE_CXX_FLAGS="-I${OPENSSL_INCLUDE_DIR}"
  -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
  -DVCPKG_TARGET_TRIPLET="${VCPKG_TRIPLET}"
  ..
)

# Add windows-specific linker flags
if [ "$PLATFORM" = "windows" ]; then
  # Force static linking of zstd from MSYS2 instead of vcpkg
  ZSTD_STATIC_LIB="${MINGW_PATH}/lib/libzstd.a"
  if [ -f "$ZSTD_STATIC_LIB" ]; then
    echo "Using static zstd from: $ZSTD_STATIC_LIB"
    cmake_args+=( -DZSTD_LIBRARY="$ZSTD_STATIC_LIB" -DZSTD_INCLUDE_DIR="${MINGW_PATH}/include" )
  fi
  cmake_args+=( -DCMAKE_EXE_LINKER_FLAGS="${LLD_FLAGS} -L${OPENSSL_LIB_DIR} -L${MINGW_PATH}/lib -Wl,-Bstatic -lzstd -Wl,-Bdynamic -lssl -lcrypto -lws2_32 -lcrypt32 -ladvapi32 -luser32 -lgdi32" -DCMAKE_SHARED_LINKER_FLAGS="${LLD_FLAGS}" -DCMAKE_REQUIRED_LIBRARIES="ws2_32;crypt32;advapi32;user32;gdi32" )
fi

cmake "${cmake_args[@]}"

ninja -v -j"${JOBS}" || { echo "Initial build failed—retrying single-threaded (JOBS=1) to work around memory limits"; ninja -v -j1 || { echo "Build failed even with single-threaded retry—see ninja output above."; exit 1; } }
ninja install

# Build other modules
cd "${BUILD_DIR}"
for m in "${MODULES[@]}"; do
  if [ "$m" = "qtbase" ]; then
    continue
  fi
  echo "Building module $m..."
  mkdir -p "$m/build"
  pushd "$m/build" >/dev/null
  cmake \
    -G "Ninja" \
    -DCMAKE_EXE_LINKER_FLAGS="${LLD_FLAGS}" \
    -DCMAKE_SHARED_LINKER_FLAGS="${LLD_FLAGS}" \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DCMAKE_PREFIX_PATH="${INSTALL_PREFIX}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DOPENSSL_ROOT_DIR="${OPENSSL_ROOT}" \
    -DOPENSSL_INCLUDE_DIR="${OPENSSL_INCLUDE_DIR}" \
    -DOPENSSL_CRYPTO_LIBRARY="${OPENSSL_LIB_DIR}/libcrypto.a" \
    -DOPENSSL_SSL_LIBRARY="${OPENSSL_LIB_DIR}/libssl.a" \
    -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET="${VCPKG_TRIPLET}" \
    ..
  ninja -v -j"${JOBS}" || { echo "Module build failed—retrying single-threaded (JOBS=1) to work around memory limits"; ninja -v -j1 || { echo "Module build failed again; see output above."; exit 1; } }
  ninja install
  popd >/dev/null
done

# Quick fix: Add -loleaut32 to qnetworklistmanager.prl (Windows only)
PRL_FILE="${INSTALL_PREFIX}/plugins/networkinformation/qnetworklistmanager.prl"
if [ -f "$PRL_FILE" ]; then
  echo "Updating $PRL_FILE to include -loleaut32..."
  echo "QMAKE_PRL_LIBS += -loleaut32" >> "$PRL_FILE"
else
  echo "Warning: $PRL_FILE not found. Please check build packaging." >&2
fi

# Verify lupdate
if [ -x "${INSTALL_PREFIX}/bin/lupdate" ] || [ -x "${INSTALL_PREFIX}/bin/lupdate.exe" ]; then
  echo "lupdate successfully built!"
else
  echo "Error: lupdate not found in ${INSTALL_PREFIX}/bin" >&2
  exit 1
fi

# Verify Qt configuration includes OpenSSL support
if [ -x "${INSTALL_PREFIX}/bin/qmake" ] || [ -x "${INSTALL_PREFIX}/bin/qmake.exe" ]; then
  echo "Checking for OpenSSL feature in Qt configuration..."
  if grep -q "openssl" "${INSTALL_PREFIX}/mkspecs/qconfig.pri" 2>/dev/null; then
    echo "Qt built with OpenSSL support"
  else
    echo "Warning: OpenSSL support not detected in Qt configuration" >&2
  fi
else
  echo "Error: qmake not found in ${INSTALL_PREFIX}/bin" >&2
  exit 1
fi

echo "Qt static build with OpenSSL completed successfully!"
