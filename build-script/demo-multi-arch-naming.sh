#!/bin/bash
# Example script demonstrating architecture-specific Docker image naming

set -e

# Configuration
REGISTRY="ghcr.io/kevinzjpeng"
UBUNTU_VERSION="24.04"

echo "🚀 Multi-Architecture Docker Build Example"
echo "=========================================="
echo ""

echo "This example shows how to build Docker images with architecture-specific names"
echo "to prevent different architectures from overriding each other in registries."
echo ""

# Function to show image naming convention
show_naming_convention() {
    echo "📝 Image Naming Convention:"
    echo "   Single Architecture Images:"
    echo "   - AMD64: ${REGISTRY}/openterface-qt-base:latest-amd64"
    echo "   - ARM64: ${REGISTRY}/openterface-qt-base:latest-arm64"
    echo "   - AMD64: ${REGISTRY}/openterface-qt-base-ubuntu-${UBUNTU_VERSION}-amd64"
    echo "   - ARM64: ${REGISTRY}/openterface-qt-base-ubuntu-${UBUNTU_VERSION}-arm64"
    echo ""
    echo "   Multi-Architecture Manifests:"
    echo "   - ${REGISTRY}/openterface-qt-base:latest"
    echo "   - ${REGISTRY}/openterface-qt-base-ubuntu-${UBUNTU_VERSION}"
    echo ""
}

# Function to demonstrate building for AMD64
build_amd64_example() {
    echo "🏗️  Building for AMD64:"
    echo "docker buildx build \\"
    echo "  --platform linux/amd64 \\"
    echo "  --file docker/Dockerfile.qt-base \\"
    echo "  --build-arg UBUNTU_VERSION=${UBUNTU_VERSION} \\"
    echo "  --tag ${REGISTRY}/openterface-qt-base:latest-amd64 \\"
    echo "  --tag ${REGISTRY}/openterface-qt-base-ubuntu-${UBUNTU_VERSION}-amd64 \\"
    echo "  --push ."
    echo ""
}

# Function to demonstrate building for ARM64
build_arm64_example() {
    echo "🏗️  Building for ARM64:"
    echo "docker buildx build \\"
    echo "  --platform linux/arm64 \\"
    echo "  --file docker/Dockerfile.qt-base \\"
    echo "  --build-arg UBUNTU_VERSION=${UBUNTU_VERSION} \\"
    echo "  --tag ${REGISTRY}/openterface-qt-base:latest-arm64 \\"
    echo "  --tag ${REGISTRY}/openterface-qt-base-ubuntu-${UBUNTU_VERSION}-arm64 \\"
    echo "  --push ."
    echo ""
}

# Function to demonstrate multi-arch manifest creation
build_multi_arch_example() {
    echo "🔗 Creating Multi-Architecture Manifest:"
    echo "docker buildx build \\"
    echo "  --platform linux/amd64,linux/arm64 \\"
    echo "  --file docker/Dockerfile.qt-base \\"
    echo "  --build-arg UBUNTU_VERSION=${UBUNTU_VERSION} \\"
    echo "  --tag ${REGISTRY}/openterface-qt-base:latest \\"
    echo "  --tag ${REGISTRY}/openterface-qt-base-ubuntu-${UBUNTU_VERSION} \\"
    echo "  --push ."
    echo ""
    echo "Note: This creates a manifest that points to both architecture-specific images"
    echo ""
}

# Function to show usage examples
show_usage_examples() {
    echo "💡 Usage Examples:"
    echo ""
    echo "1. Use our build script for AMD64:"
    echo "   ./build-script/build-multi-arch.sh --architecture linux/amd64 --type base"
    echo ""
    echo "2. Use our build script for ARM64:"
    echo "   ./build-script/build-multi-arch.sh --architecture linux/arm64 --type base"
    echo ""
    echo "3. Use our build script for both (creates manifest):"
    echo "   ./build-script/build-multi-arch.sh --architecture linux/amd64,linux/arm64 --type base --push"
    echo ""
    echo "4. Pull specific architecture:"
    echo "   docker pull ${REGISTRY}/openterface-qt-base:latest-amd64"
    echo "   docker pull ${REGISTRY}/openterface-qt-base:latest-arm64"
    echo ""
    echo "5. Pull using manifest (Docker chooses correct arch):"
    echo "   docker pull ${REGISTRY}/openterface-qt-base:latest"
    echo ""
}

# Function to show benefits
show_benefits() {
    echo "✅ Benefits of Architecture-Specific Naming:"
    echo ""
    echo "1. 🔒 Prevents Overwrites: AMD64 and ARM64 images won't override each other"
    echo "2. 🎯 Explicit Architecture: Clear which architecture you're using"
    echo "3. 🔄 Parallel Builds: Can build both architectures simultaneously"
    echo "4. 🧪 Testing: Can test specific architectures independently"
    echo "5. 📋 Debugging: Easier to identify architecture-specific issues"
    echo "6. 🌐 Compatibility: Works with multi-arch manifests for seamless pulls"
    echo ""
}

# Function to show GitHub Actions integration
show_github_actions() {
    echo "🚀 GitHub Actions Integration:"
    echo ""
    echo "The workflow now supports architecture-specific builds:"
    echo ""
    echo "1. Go to Actions → 'Build Qt Environments'"
    echo "2. Click 'Run workflow'"
    echo "3. Select:"
    echo "   - Architecture: linux/amd64, linux/arm64, or both"
    echo "   - Ubuntu Version: 22.04 or 24.04"
    echo "   - Environment Type: static, dynamic, or both"
    echo ""
    echo "Examples of generated image names:"
    echo "- ghcr.io/kevinzjpeng/openterface-qt-base:latest-amd64"
    echo "- ghcr.io/kevinzjpeng/openterface-qt-dynamic-ubuntu-24.04-arm64"
    echo "- ghcr.io/kevinzjpeng/openterface-qt-complete:latest (multi-arch manifest)"
    echo ""
}

# Main execution
show_naming_convention
build_amd64_example
build_arm64_example
build_multi_arch_example
show_usage_examples
show_benefits
show_github_actions

echo "🎉 For more information, see docker/README-multi-arch.md"
