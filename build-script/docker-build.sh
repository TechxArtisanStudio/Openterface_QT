#!/bin/bash
set -e

echo "Building Openterface QT Application in docker build environment..."

# Copy configuration files
mkdir -p /workspace/build/config/languages
mkdir -p /workspace/build/config/keyboards
cp -r config/keyboards/*.json /workspace/build/config/keyboards/ 2>/dev/null || echo "No keyboard configs"
cp -r config/languages/*.qm /workspace/build/config/languages/ 2>/dev/null || echo "No language files"

# Build with CMake
cd /workspace/build
echo "Configuring with CMake..."

echo "Finding FFmpeg libraries..."
find / -name "libavformat.so*" || echo "No shared libavformat found"
find / -name "libavformat.a*" || echo "No shared libavformat found"
find / -name "avformat.h" || echo "No avformat.h found"

# Debug pkg-config before CMake
pkg-config --version
pkg-config --list-all | grep -E "avformat|avcodec" || echo "No FFmpeg packages found"
echo "========================="

# Default to dynamic linking for the shared build environment; allow override via OPENTERFACE_BUILD_STATIC env
: "${OPENTERFACE_BUILD_STATIC:=OFF}"
echo "OPENTERFACE_BUILD_STATIC set to ${OPENTERFACE_BUILD_STATIC}"
cmake \
	-DCMAKE_BUILD_TYPE=Release \
	-DOPENTERFACE_BUILD_STATIC=${OPENTERFACE_BUILD_STATIC} \
	-DCMAKE_VERBOSE_MAKEFILE=ON \
	/workspace/src

echo "Building with CMake..."
make -j4 VERBOSE=1
echo "Build complete."

SRC=/workspace/src
BUILD=/workspace/build

# Determine version from resources/version.h (APP_VERSION macro)
VERSION_H="${SRC}/resources/version.h"
if [ -f "${VERSION_H}" ]; then
	VERSION=$(grep -Po '^#define APP_VERSION\s+"\K[0-9]+(\.[0-9]+)*' "${VERSION_H}" | head -n1)
fi
if [ -z "${VERSION}" ]; then
	VERSION="0.0.1"
fi

# Determine architecture (map to Debian arch names)
ARCH=$(dpkg --print-architecture 2>/dev/null || true)
if [ -z "${ARCH}" ]; then
	UNAME_M=$(uname -m)
	case "${UNAME_M}" in
		aarch64|arm64) ARCH=arm64;;
		x86_64|amd64) ARCH=amd64;;
		*) ARCH=${UNAME_M};;
	esac
fi

# =========================
# Build AppImage (.AppImage)
# =========================
echo "Preparing AppImage package..."

APPIMAGE_DIR=/workspace/appimage
APPDIR="${APPIMAGE_DIR}/AppDir"
APPIMAGE_OUT=/workspace/build

rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin"
mkdir -p "${APPDIR}/usr/share/applications"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"

# Copy main binary
if [ -f "${BUILD}/openterfaceQT" ]; then
	install -m 0755 "${BUILD}/openterfaceQT" "${APPDIR}/usr/bin/openterfaceQT"
else
	echo "Error: built binary not found at ${BUILD}/openterfaceQT" >&2
	exit 1
fi

# Desktop file for AppImage (Exec must be bare binary name; Icon should be a basename)
DESKTOP_OUT="${APPDIR}/usr/share/applications/openterfaceQT.desktop"
if [ -f "${SRC}/com.openterface.openterfaceQT.desktop" ]; then
	sed 's|^Exec=.*$|Exec=openterfaceQT|; s|^Icon=.*$|Icon=openterfaceQT|' \
		"${SRC}/com.openterface.openterfaceQT.desktop" > "${DESKTOP_OUT}"
else
	cat > "${DESKTOP_OUT}" <<EOF
[Desktop Entry]
Type=Application
Name=OpenterfaceQT
Exec=openterfaceQT
Icon=openterfaceQT
Categories=Utility;
EOF
fi

# AppStream/metainfo (optional)
if [ -f "${SRC}/com.openterface.openterfaceQT.metainfo.xml" ]; then
	mkdir -p "${APPDIR}/usr/share/metainfo"
	cp "${SRC}/com.openterface.openterfaceQT.metainfo.xml" "${APPDIR}/usr/share/metainfo/"
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
else
	echo "No icon found; continuing without a custom icon."
fi

# Determine linuxdeploy architecture tag from Debian arch
case "${ARCH}" in
	amd64|x86_64) APPIMAGE_ARCH=x86_64;;
	arm64|aarch64) APPIMAGE_ARCH=aarch64;;
	armhf|armv7l) APPIMAGE_ARCH=armhf;;
	*) echo "Warning: unknown arch '${ARCH}', defaulting to x86_64"; APPIMAGE_ARCH=x86_64;;
esac

# Prefer preinstalled linuxdeploy and plugin in the image; fallback to download
TOOLS_DIR="${APPIMAGE_DIR}/tools"
LINUXDEPLOY_BIN=""

# Ensure AppImages run inside containers without FUSE (also set in Dockerfile)
export APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN:-1}

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
	chmod +x linuxdeploy.AppImage linuxdeploy-plugin-qt.AppImage
	# Use the downloaded linuxdeploy and make plugin available on PATH
	LINUXDEPLOY_BIN="${TOOLS_DIR}/linuxdeploy.AppImage"
	export PATH="${TOOLS_DIR}:${PATH}"
	popd >/dev/null
fi

# Build AppImage
pushd "${APPIMAGE_DIR}" >/dev/null
"${LINUXDEPLOY_BIN}" \
	--appdir "${APPDIR}" \
	--executable "${APPDIR}/usr/bin/openterfaceQT" \
	--desktop-file "${DESKTOP_OUT}" \
	${ICON_SRC:+--icon-file "${ICON_SRC}"} \
	--plugin qt \
	--output appimage

# Normalize output name
APPIMAGE_FILENAME="openterfaceQT_${VERSION}_${APPIMAGE_ARCH}.AppImage"
# Move whichever AppImage got produced
FOUND_APPIMAGE=$(ls -1 *.AppImage 2>/dev/null | grep -v -E '^linuxdeploy|^linuxdeploy-plugin-qt' | head -n1 || true)
if [ -n "${FOUND_APPIMAGE}" ]; then
	mv "${FOUND_APPIMAGE}" "${APPIMAGE_OUT}/${APPIMAGE_FILENAME}"
	echo "AppImage created at ${APPIMAGE_OUT}/${APPIMAGE_FILENAME}"
else
	echo "Error: AppImage build did not produce an output." >&2
	exit 1
fi
popd >/dev/null
