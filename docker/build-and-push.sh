#!/bin/bash
set -e

# Configuration
IMAGE_NAME="ghcr.io/kevinzjpeng/openterface-alpine-qt-arm64-builder"
VERSION="6.6.3-alpine"
DOCKERFILE="Dockerfile.alpine"
CONTEXT_PATH="."

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# Check if GitHub token is set
if [ -z "$GITHUB_TOKEN" ]; then
    echo -e "${YELLOW}Warning: GITHUB_TOKEN environment variable is not set.${NC}"
    echo -e "${YELLOW}You will need to be logged in to GitHub Container Registry already.${NC}"
    echo -e "${YELLOW}If you're not logged in, the push will fail.${NC}"
    echo ""
    echo -e "${YELLOW}To log in, you can run:${NC}"
    echo -e "${YELLOW}  echo \$GITHUB_TOKEN | docker login ghcr.io -u USERNAME --password-stdin${NC}"
    echo ""
    echo -e "${YELLOW}Alternatively, set the GITHUB_TOKEN environment variable:${NC}"
    echo -e "${YELLOW}  export GITHUB_TOKEN=your_github_personal_access_token${NC}"
    echo ""
    read -p "Continue anyway? (y/n) " -n 1 -r
    echo ""
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Aborted."
        exit 1
    fi
else
    echo -e "${GREEN}Authenticating with GitHub Container Registry...${NC}"
    echo "$GITHUB_TOKEN" | docker login ghcr.io -u "$GITHUB_USERNAME" --password-stdin
fi

# Build the Docker image
echo -e "${GREEN}Building Docker image: ${IMAGE_NAME}:${VERSION}...${NC}"
docker buildx create --use --name openterface-builder --driver docker-container

docker buildx build --platform linux/arm64 \
    -t "${IMAGE_NAME}:${VERSION}" \
    -t "${IMAGE_NAME}:latest" \
    -f "${DOCKERFILE}" \
    --push \
    "${CONTEXT_PATH}"

# Check if the build was successful
if [ $? -eq 0 ]; then
    echo -e "${GREEN}Successfully built and pushed ${IMAGE_NAME}:${VERSION}${NC}"
else
    echo -e "${RED}Failed to build or push Docker image.${NC}"
    echo -e "${YELLOW}If the error was related to authentication, make sure:${NC}"
    echo -e "${YELLOW}1. You have a GitHub Personal Access Token with 'write:packages' scope${NC}"
    echo -e "${YELLOW}2. You're properly authenticated with 'docker login ghcr.io'${NC}"
    echo -e "${YELLOW}3. You have permission to push to the ${IMAGE_NAME} repository${NC}"
    exit 1
fi

# Information about using the built image
echo -e "${GREEN}=== Image Information ===${NC}"
echo -e "Image: ${IMAGE_NAME}:${VERSION}"
echo -e "To pull this image: docker pull ${IMAGE_NAME}:${VERSION}"
echo -e "${GREEN}=========================${NC}"
