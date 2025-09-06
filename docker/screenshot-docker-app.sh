#!/bin/bash

# Simple Docker App Screenshot Script
# Starts Docker container, waits 10 seconds, takes scecho -e "${BLUE}📷 使用 ImageMagick 截图 (JPG)...${NC}"eenshot

set -e

# Configuration
DOCKER_IMAGE="openterface-test-shared"
DOCKER_TAG="screenshot-test"
DOCKERFILE_PATH="docker/Dockerfile.openterface-test-shared"
SCREENSHOTS_DIR="app-screenshots"
CONTAINER_NAME="openterface-screenshot-test"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${BLUE}🚀 启动 Openterface Docker 应用截图测试${NC}"
echo "================================================"

# Cleanup function
cleanup() {
    echo -e "${YELLOW}🧹 清理资源...${NC}"
    docker stop $CONTAINER_NAME 2>/dev/null || true
    docker rm $CONTAINER_NAME 2>/dev/null || true
    if [ ! -z "$XVFB_PID" ]; then
        kill $XVFB_PID 2>/dev/null || true
    fi
}
trap cleanup EXIT

# Check if Docker image exists, if not build it
if ! docker images | grep -q "$DOCKER_IMAGE.*$DOCKER_TAG"; then
    echo -e "${BLUE}🔨 构建 Docker 镜像...${NC}"
    if [ ! -f "$DOCKERFILE_PATH" ]; then
        echo -e "${RED}❌ Dockerfile 不存在: $DOCKERFILE_PATH${NC}"
        exit 1
    fi
    
    # Force rebuild to include latest fixes
    echo -e "${BLUE}💡 重新构建镜像以包含最新修复...${NC}"
    docker build --no-cache -f "$DOCKERFILE_PATH" -t "$DOCKER_IMAGE:$DOCKER_TAG" docker/
    echo -e "${GREEN}✅ 镜像构建完成${NC}"
else
    echo -e "${BLUE}📦 使用现有 Docker 镜像${NC}"
    # Check if we should rebuild (optional)
    echo -e "${YELLOW}💡 提示: 如果遇到问题，可以删除镜像重新构建: docker rmi $DOCKER_IMAGE:$DOCKER_TAG${NC}"
fi

# Install virtual display and ImageMagick dependencies if needed
if ! command -v Xvfb >/dev/null 2>&1 || ! command -v import >/dev/null 2>&1; then
    echo -e "${BLUE}📦 安装虚拟显示和图像处理依赖...${NC}"
    sudo apt-get update -y >/dev/null
    sudo apt-get install -y xvfb imagemagick x11-utils >/dev/null
    echo -e "${GREEN}✅ 依赖安装完成${NC}"
fi

# Setup virtual display
echo -e "${BLUE}🖥️  设置虚拟显示...${NC}"
export DISPLAY=:98
Xvfb :98 -screen 0 1920x1080x24 -ac +extension GLX +render -noreset >/dev/null 2>&1 &
XVFB_PID=$!
sleep 3

# Verify X server
if ! DISPLAY=:98 xdpyinfo >/dev/null 2>&1; then
    echo -e "${RED}❌ 虚拟显示启动失败${NC}"
    exit 1
fi
echo -e "${GREEN}✅ 虚拟显示启动成功 ($DISPLAY)${NC}"

# Create screenshots directory
mkdir -p $SCREENSHOTS_DIR

# Start Docker container with the app
echo -e "${BLUE}🐳 启动 Docker 容器和应用...${NC}"
CONTAINER_ID=$(docker run -d \
    --name $CONTAINER_NAME \
    -e DISPLAY=$DISPLAY \
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
    --network host \
    $DOCKER_IMAGE:$DOCKER_TAG)

echo -e "${GREEN}✅ 容器已启动${NC}"
echo -e "${BLUE}📱 应用正在初始化...${NC}"

# Wait exactly 10 seconds with countdown
echo -e "${YELLOW}⏱️  等待 10 秒后截图:${NC}"
for i in {10..1}; do
    printf "\r${YELLOW}倒计时: %2d 秒${NC}" $i
    sleep 1
done
echo ""

# Take the main screenshot
echo -e "${BLUE}📸 正在截取屏幕截图...${NC}"
timestamp=$(date +"%Y%m%d_%H%M%S")
screenshot_jpg="$SCREENSHOTS_DIR/openterface_app_${timestamp}.jpg"

# Use ImageMagick import for reliable JPG screenshot capture
echo -e "${BLUE}� 使用 ImageMagick 截图 (PNG/JPG)...${NC}"
screenshot_success=false

# ImageMagick import is the most reliable method for this virtual display setup
if command -v import >/dev/null 2>&1; then
    if DISPLAY=:98 import -window root -quality 90 "$screenshot_jpg" 2>/dev/null; then
        echo -e "${GREEN}✅ JPG截图已保存: $screenshot_jpg${NC}"
        screenshot_success=true
    else
        echo -e "${RED}❌ ImageMagick 截图失败${NC}"
    fi
else
    echo -e "${RED}❌ ImageMagick import 命令不可用${NC}"
    echo -e "${YELLOW}� 请安装 ImageMagick: sudo apt-get install imagemagick${NC}"
fi

if [ "$screenshot_success" = true ]; then
    echo -e "${GREEN}✅ 截图成功生成${NC}"
    
    # Analyze screenshot
    if [ -f "$screenshot_jpg" ] && command -v identify >/dev/null 2>&1; then
        filesize=$(ls -lh "$screenshot_jpg" | awk '{print $5}')
        dimensions=$(identify "$screenshot_jpg" | awk '{print $3}')
        mean_color=$(identify -ping -format "%[mean]" "$screenshot_jpg" 2>/dev/null || echo "0")
        mean_value=${mean_color%.*}
        
        echo -e "${BLUE}📊 JPG截图分析:${NC}"
        echo "   文件大小: $filesize"
        echo "   图像尺寸: $dimensions"
        echo "   平均颜色值: $mean_value"
        
        if [ "$mean_value" -gt 1000 ]; then
            echo -e "${GREEN}   状态: ✅ 检测到丰富的应用内容${NC}"
        elif [ "$mean_value" -gt 100 ]; then
            echo -e "${YELLOW}   状态: ⚠️  检测到基本内容${NC}"
        else
            echo -e "${RED}   状态: ❌ 截图可能为空白${NC}"
        fi
    fi
else
    echo -e "${RED}❌ 所有截图方法都失败了${NC}"
fi

# Analyze all screenshots
if [ -d "$SCREENSHOTS_DIR" ] && [ "$(ls -A $SCREENSHOTS_DIR/*.jpg 2>/dev/null)" ]; then
    echo -e "${BLUE}📊 所有截图分析:${NC}"
    for img in $SCREENSHOTS_DIR/*.jpg; do
        if [ -f "$img" ]; then
            filename=$(basename "$img")
            if command -v identify >/dev/null 2>&1; then
                filesize=$(ls -lh "$img" | awk '{print $5}')
                dimensions=$(identify "$img" | awk '{print $3}' | head -1)
                mean_color=$(identify -ping -format "%[mean]" "$img" 2>/dev/null || echo "0")
                mean_value=${mean_color%.*}
                
                echo -e "${BLUE}   📸 $filename:${NC}"
                echo "      大小: $filesize | 尺寸: $dimensions | 平均值: $mean_value"
                
                if [ "$mean_value" -gt 1000 ]; then
                    echo -e "${GREEN}      状态: ✅ 检测到丰富内容${NC}"
                elif [ "$mean_value" -gt 100 ]; then
                    echo -e "${YELLOW}      状态: ⚠️  检测到基本内容${NC}"
                else
                    echo -e "${RED}      状态: ❌ 截图可能为空白${NC}"
                fi
            fi
        fi
    done
fi

# Show container status and logs
echo -e "${BLUE}🔍 容器状态检查:${NC}"
if docker ps | grep -q $CONTAINER_ID; then
    echo -e "${GREEN}✅ 容器正在运行${NC}"
    
    # Show recent logs
    echo -e "${BLUE}📋 容器日志 (最后 10 行):${NC}"
    docker logs --tail 10 $CONTAINER_ID 2>&1 | sed 's/^/   /'
    
    # Check processes
    echo -e "${BLUE}🔍 应用进程检查:${NC}"
    process_count=$(docker exec $CONTAINER_NAME ps aux | grep -E "openterface|Qt|qt" | grep -v grep | wc -l)
    if [ $process_count -gt 0 ]; then
        echo -e "${GREEN}   ✅ 发现 $process_count 个相关进程${NC}"
        docker exec $CONTAINER_NAME ps aux | grep -E "openterface|Qt|qt" | grep -v grep | sed 's/^/   /'
    else
        echo -e "${YELLOW}   ⚠️  未发现明显的应用进程${NC}"
    fi
else
    echo -e "${RED}❌ 容器已退出${NC}"
    echo -e "${BLUE}📋 容器退出日志:${NC}"
    docker logs $CONTAINER_ID 2>&1 | sed 's/^/   /'
fi

# Show window information
echo -e "${BLUE}🪟 X11 窗口信息:${NC}"
window_info=$(DISPLAY=:98 xwininfo -tree -root 2>/dev/null | head -10 || echo "无法获取窗口信息")
if echo "$window_info" | grep -q "child"; then
    echo -e "${GREEN}   ✅ 检测到活动窗口${NC}"
    echo "$window_info" | grep "child" | head -3 | sed 's/^/   /'
else
    echo -e "${YELLOW}   ⚠️  未检测到活动窗口${NC}"
fi

# Summary
echo ""
echo -e "${BLUE}📋 测试总结:${NC}"
echo "================================================"
echo "容器镜像: $DOCKER_IMAGE:$DOCKER_TAG"
echo "容器名称: $CONTAINER_NAME"
echo "显示环境: $DISPLAY"
echo "截图目录: $SCREENSHOTS_DIR/"
echo ""

# List all screenshots
if [ -d "$SCREENSHOTS_DIR" ] && [ "$(ls -A $SCREENSHOTS_DIR 2>/dev/null)" ]; then
    echo -e "${GREEN}📸 已生成的截图文件:${NC}"
    ls -lh $SCREENSHOTS_DIR/ | grep -v "^total" | sed 's/^/   /'
    echo ""
    
    # Show specific viewing commands for main screenshots
    main_jpg=$(ls $SCREENSHOTS_DIR/openterface_app_*[0-9].jpg 2>/dev/null | head -1)
    
    if [ -n "$main_jpg" ]; then
        echo -e "${BLUE}💡 查看截图命令:${NC}"
        echo "   display $main_jpg"
        echo "   eog $main_jpg"
        echo "   firefox $main_jpg"
    fi
    
    # Count JPG files
    jpg_count=$(ls $SCREENSHOTS_DIR/*.jpg 2>/dev/null | wc -l)
    
    echo ""
    echo -e "${GREEN}📈 截图统计:${NC}"
    echo "   JPG 文件: $jpg_count 个"
else
    echo -e "${RED}❌ 没有生成截图文件${NC}"
fi

echo ""
echo -e "${GREEN}🎉 测试完成！${NC}"
