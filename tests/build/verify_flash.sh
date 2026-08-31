#!/bin/bash

echo "========================================="
echo "固件烧录验证脚本"
echo "========================================="
echo ""

# 检查设备是否已重新枚举
echo "步骤1: 检查设备状态..."
if lsusb | grep -q "1a86:fe0c"; then
    echo "✓ 设备已在正常模式 (1a86:fe0c)"
else
    echo "✗ 设备不在正常模式"
    echo "当前USB设备:"
    lsusb | grep -iE "1a86|4348"
    exit 1
fi

# 等待设备初始化
echo ""
echo "步骤2: 等待设备初始化 (5秒)..."
sleep 5

# 检查串口设备
echo ""
echo "步骤3: 检查串口设备..."
if [ -e /dev/ttyACM0 ]; then
    echo "✓ 串口设备: /dev/ttyACM0"
elif [ -e /dev/ttyUSB0 ]; then
    echo "✓ 串口设备: /dev/ttyUSB0"
    SERIAL_PORT="/dev/ttyUSB0"
else
    echo "✗ 未找到串口设备"
    exit 1
fi

SERIAL_PORT="${SERIAL_PORT:-/dev/ttyACM0}"

# 尝试不同波特率
echo ""
echo "步骤4: 尝试GET_INFO命令..."
echo ""

for BAUD in 115200 9600 57600 38400; do
    echo "尝试波特率: $BAUD"
    stty -F $SERIAL_PORT $BAUD raw -echo 2>/dev/null

    # 发送GET_INFO命令
    echo -ne "\x57\xAB\x00\x01\x00" > $SERIAL_PORT

    # 等待响应
    timeout 2 cat $SERIAL_PORT > /tmp/response.bin 2>/dev/null

    if [ -s /tmp/response.bin ]; then
        echo "✓ 收到响应!"
        echo "  响应数据: $(xxd -p /tmp/response.bin | tr -d '\n')"

        # 解析响应
        RESPONSE=$(xxd -p /tmp/response.bin | tr -d '\n')
        if [[ ${RESPONSE:0:4} == "57ab" ]]; then
            VERSION=$(echo ${RESPONSE:10:2} | xxd -r -p | od -An -tu1 | tr -d ' ')
            TARGET=$(echo ${RESPONSE:12:2} | xxd -r -p | od -An -tu1 | tr -d ' ')
            echo "  ✓ 固件版本: $VERSION"
            echo "  ✓ 目标连接: $([ $TARGET -ne 0 ] && echo '是' || echo '否')"
            echo ""
            echo "========================================="
            echo "✓✓✓ 烧录成功! 固件正常工作! ✓✓✓"
            echo "========================================="
            exit 0
        fi
    else
        echo "  无响应"
    fi
    echo ""
done

echo "========================================="
echo "✗ 所有波特率都无响应"
echo "可能的原因:"
echo "  1. 固件未正确烧录"
echo "  2. 固件协议不同"
echo "  3. 设备需要更长时间初始化"
echo "========================================="
exit 1
