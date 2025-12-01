#!/usr/bin/env bash
set -euo pipefail

# Build the Qt cross-build image (based on ffmpeg-win image) and optionally attempt the build
# It will export the resulting Qt prefix directory (if created) to build/qt-win-static

QT_VER=${QT_VER:-6.6.3}
FFMPEG_TAG=${FFMPEG_TAG:-6.1.1}
IMAGE_TAG="openterface/qt-win-static:${QT_VER}-ffmpeg${FFMPEG_TAG}"
DOCKERFILE=docker/Dockerfile.qt-windows-static
OUT_DIR="$(pwd)/../build/qt-win-static"

echo "Building docker image ${IMAGE_TAG} from ${DOCKERFILE} (this will NOT run a full Qt make by default)..."
docker build --progress=plain -f ${DOCKERFILE} -t ${IMAGE_TAG} --build-arg QT_VERSION=${QT_VER} --build-arg BASE_IMAGE=openterface/ffmpeg-win-static:${FFMPEG_TAG} .

echo "Created image ${IMAGE_TAG}. To run a full build inside the container, run (example):"
echo "  docker run --rm -it -v \\$(pwd)/../build:/workspace/build ${IMAGE_TAG} /bin/bash"
echo "  # then inside container: cd /workspace/qt-win-build && make -j\"

# Try to extract any already-installed prefix
CID=$(docker create ${IMAGE_TAG}) || { echo "Failed to create container from image"; exit 1; }
mkdir -p "${OUT_DIR}"
docker cp ${CID}:${QT_INSTALL_PREFIX:-/opt/qt-windows-static} "${OUT_DIR}" || echo "No pre-built Qt install found inside image; run build inside container to build Qt"
docker rm ${CID} > /dev/null || true

echo "Attempt to export complete. Check ${OUT_DIR}"
