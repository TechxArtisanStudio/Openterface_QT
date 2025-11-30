#!/usr/bin/env bash
set -euo pipefail

# Helper: build Docker image that cross-compiles FFmpeg for Windows and extract the output.
# Output dir (host) will contain the final installed tree (/opt/ffmpeg-win-static inside container).

FFMPEG_VER=${FFMPEG_VER:-6.1.1}
IMAGE_TAG="openterface/ffmpeg-win-static:${FFMPEG_VER}"
DOCKERFILE=docker/Dockerfile.ffmpeg-windows-static
OUT_DIR="$(pwd)/../build/ffmpeg-win-static"

echo "Building docker image ${IMAGE_TAG} from ${DOCKERFILE}..."
docker build --progress=plain -f ${DOCKERFILE} -t ${IMAGE_TAG} --build-arg FFMPEG_VERSION=${FFMPEG_VER} .

echo "Creating temporary container to export artifacts..."
CID=$(docker create ${IMAGE_TAG})
mkdir -p "${OUT_DIR}"

echo "Copying /opt/ffmpeg-win-static from container ${CID} to ${OUT_DIR}"
docker cp ${CID}:/opt/ffmpeg-win-static "${OUT_DIR}"

echo "Removing temporary container ${CID}"
docker rm ${CID} > /dev/null

echo "FFmpeg (Windows static) exported to: ${OUT_DIR}"

echo "Done. You can now use -DFFMPEG_PREFIX=<path> to point your CMake to the static tree."
