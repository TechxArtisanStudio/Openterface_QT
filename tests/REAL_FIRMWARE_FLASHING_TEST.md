# 真实固件烧录集成测试

## 概述

这是一个**硬件在环测试**,需要真实的Openterface设备:
- 插入真实设备
- 实际烧录固件到CH32V208芯片
- 使用GET_INFO命令(0x01)验证烧录成功
- 测试串口通信稳定性

## 测试流程

```
1. 设备进入ISP模式 (VID:PID = 1A86:55E0)
2. 使用WCHFlasher烧录固件
3. 设备重启,重新枚举 (VID:PID = 1A86:FE0C)
4. 打开串口
5. 发送GET_INFO命令 (57 AB 00 01 00)
6. 验证响应(固件版本、连接状态)
7. 多次GET_INFO测试稳定性
```

## 前置条件

### 硬件要求

1. **Openterface Mini-KVM设备**
2. **USB线缆**
3. **固件文件** (.hex格式)

### 软件要求

- Qt 6.x with SerialPort
- libusb-1.0 (Linux/Mac) 或 WCH驱动 (Windows)
- CMake 3.16+

### 固件文件

准备一个有效的固件文件:
```bash
# 方法1: 设置环境变量
export FIRMWARE_PATH=/path/to/your/firmware.hex

# 方法2: 放到默认位置
cp your_firmware.hex tests/build/test_firmware.hex
```

## 运行测试

### 步骤1: 准备设备

**将设备进入ISP模式:**

1. 拔掉设备USB
2. 按住BOOT按钮
3. 插入USB(保持按住BOOT)
4. 松开BOOT按钮

设备现在应该以VID:PID = 1A86:55E0枚举

### 步骤2: 构建测试

```bash
cd tests
mkdir -p build && cd build
cmake ..
make test_real_firmware_flashing
```

### 步骤3: 运行测试

```bash
# 使用环境变量指定固件路径
FIRMWARE_PATH=/path/to/firmware.hex ./test_real_firmware_flashing

# 或者放到默认位置
./test_real_firmware_flashing

# 详细输出
FIRMWARE_PATH=/path/to/firmware.hex ./test_real_firmware_flashing -v2
```

### 步骤4: 指定串口(可选)

如果自动检测失败,可以手动指定串口:

```bash
# Linux
export SERIAL_PORT=/dev/ttyUSB0
export FIRMWARE_PATH=/path/to/firmware.hex
./test_real_firmware_flashing

# 或一次性设置
SERIAL_PORT=/dev/ttyUSB0 FIRMWARE_PATH=/path/to/firmware.hex ./test_real_firmware_flashing
```

## 测试用例详解

### Test 1: 检测ISP模式设备

**验证:** 设备是否正确进入ISP模式
**期望:** 检测到 VID:PID = 1A86:55E0 或 4348:55E0

**如果失败:**
- 设备未进入ISP模式
- 重新执行"步骤1:准备设备"
- 检查USB连接

### Test 2: 烧录固件

**验证:** 实际烧录固件到芯片
**期望:** 烧录成功,无错误

**烧录过程:**
```
检查Flash保护 → 解除保护 → 擦除 → 编程 → 验证 → 重置
```

**如果失败:**
- 固件文件无效或损坏
- USB连接不稳定
- 设备未正确进入ISP模式

### Test 3: 等待设备重新枚举

**验证:** 设备烧录后正常启动
**期望:** 检测到 VID:PID = 1A86:FE0C

**如果失败:**
- 固件不兼容
- 设备启动失败
- 检查dmesg日志: `dmesg | tail -20`

### Test 4: 打开串口

**验证:** 串口可以正常打开
**期望:** 串口打开成功

**如果失败:**
- 权限问题: `sudo usermod -a -G dialout $USER`
- udev规则未配置
- 串口被其他程序占用

### Test 5: GET_INFO验证 ⭐核心测试

**验证:** 烧录的固件是否正常工作
**命令:** `57 AB 00 01 00`
**期望响应:** `57 AB 00 81 06 [版本] [连接状态] [指示灯] ... [校验和]`

**响应解析:**
```
Byte 5: 固件版本 (应该 > 0)
Byte 6: 目标连接状态 (0x01 = 已连接)
Byte 7: 指示灯状态
```

**如果失败:**
- 固件损坏
- 串口通信问题
- 波特率不匹配(应该是115200)

### Test 6: 多次GET_INFO稳定性测试

**验证:** 固件是否稳定运行
**期望:** 10次GET_INFO全部成功

**如果失败:**
- 固件不稳定
- 串口通信不可靠
- USB连接问题

## 预期输出

成功的测试输出:

```
=== Real Firmware Flashing Test ===
Firmware path: /path/to/firmware.hex
Serial port: auto-detect

=== Test 1: Detect ISP Mode Device ===
Please ensure device is in ISP mode (hold BOOT while plugging in)
✓ Device found in ISP mode: ttyUSB0
  VID: 0x1a86
  PID: 0x55e0

=== Test 2: Flash Firmware ===
Loading firmware from: /path/to/firmware.hex
Firmware loaded: 28672 bytes
Chip info: Chip: CH32V208 (Code Flash: 64 KiB)
Starting flash process...
[0%] Checking flash protection...
[2%] Flash already unprotected
[6%] Erasing flash...
[10%] Erase complete
[10%] Sending ISP key...
...
[95%] Verify complete
[96%] Resetting device...
[100%] Flash complete! Reconnect the device.
✓ Firmware flashed successfully

=== Test 3: Wait for Device Re-enumeration ===
Waiting for device to boot with new firmware...
✓ Device re-enumerated in normal mode: ttyUSB0
  VID: 0x1a86
  PID: 0xfe0c

=== Test 4: Open Serial Port ===
Opening serial port: ttyUSB0
✓ Serial port opened successfully

=== Test 5: GET_INFO Verification ===
Using serial port: ttyUSB0
Sending GET_INFO command (0x01)...
GET_INFO response: 57 ab 00 81 06 02 01 00 00 00 00 2b
✓ GET_INFO response valid
  Firmware version: 2
  Target connected: YES
  Indicators: 0x00

✓✓✓ Firmware flash and verification SUCCESSFUL ✓✓✓

=== Test 6: Multiple GET_INFO Commands (Stability Test) ===
Sending 10 GET_INFO commands...
  [1/10] ✓ Version: 2
  [2/10] ✓ Version: 2
  ...
  [10/10] ✓ Version: 2

Success rate: 10/10
✓ All GET_INFO commands successful - firmware is stable

=== Test Complete ===
All tests passed successfully!
Firmware is working correctly.
```

## 故障排除

### 问题1: 设备未检测到

**症状:** "Device not found in ISP mode"

**解决:**
```bash
# 检查USB设备
lsusb | grep -E "55e0|55E0"

# 检查权限
ls -l /dev/bus/usb/*

# 重新加载udev规则
sudo udevadm control --reload-rules
sudo udevadm trigger
```

### 问题2: 烧录失败

**症状:** "Flash failed" 或传输错误

**解决:**
```bash
# 检查固件文件
file firmware.hex
hexdump -C firmware.hex | head

# 尝试不同的USB端口(优先USB 2.0)
# 避免使用USB集线器

# 检查dmesg
dmesg | tail -30
```

### 问题3: 串口打开失败

**症状:** "Failed to open serial port"

**解决:**
```bash
# 检查串口设备
ls -l /dev/ttyUSB*

# 添加用户到dialout组
sudo usermod -a -G dialout $USER
# 然后重新登录

# 检查是否有其他程序占用
lsof /dev/ttyUSB*
```

### 问题4: GET_INFO无响应

**症状:** "Timeout waiting for response"

**解决:**
```bash
# 手动测试串口
screen /dev/ttyUSB0 115200
# 发送: 57 AB 00 01 00 (十六进制)
# 应该收到响应

# 检查波特率
stty -F /dev/ttyUSB0 115200

# 尝试重置设备
# 拔掉USB重新插入(不按BOOT)
```

### 问题5: 设备重新枚举失败

**症状:** "Device did not re-enumerate after flash"

**解决:**
- 固件可能不兼容
- 检查固件版本是否与硬件匹配
- 查看dmesg: `dmesg | tail -30`
- 手动重新插入设备

## Linux特定问题

### udev规则

确保WCH设备有正确权限:

```bash
sudo tee /etc/udev/rules.d/51-opf-wchflash.rules <<'EOF'
# ISP mode
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55e0", TAG+="uaccess", MODE="0666"
SUBSYSTEM=="usb", ATTRS{idVendor}=="4348", ATTRS{idProduct}=="55e0", TAG+="uaccess", MODE="0666"
# Normal mode
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="fe0c", TAG+="uaccess", MODE="0666"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger
```

### libusb权限

如果libusb无法访问设备:

```bash
# 临时解决(测试时)
sudo chmod 666 /dev/bus/usb/$(lsusb | grep 1a86 | awk '{print $2}')/$(lsusb | grep 1a86 | awk '{print $4}' | tr -d ':')

# 永久解决: 配置udev规则(见上)
```

### 串口权限

```bash
# 临时解决
sudo chmod 666 /dev/ttyUSB0

# 永久解决
sudo usermod -a -G dialout $USER
# 重新登录
```

## 手动验证

如果测试失败,可以手动验证:

### 1. 检查ISP模式

```bash
lsusb | grep -E "55e0|55E0"
# 应该看到: ID 1a86:55e0
```

### 2. 检查正常模式

```bash
lsusb | grep -E "fe0c|FE0C"
# 应该看到: ID 1a86:fe0c
```

### 3. 手动发送GET_INFO

```bash
# 使用Python
python3 <<'EOF'
import serial
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
cmd = bytes.fromhex('57 AB 00 01 00')
ser.write(cmd)
response = ser.read(12)
print('Response:', response.hex(' '))
if len(response) >= 12 and response[0] == 0x57 and response[1] == 0xAB:
    print(f'Version: {response[5]}')
    print(f'Target Connected: {response[6] != 0}')
ser.close()
EOF
```

### 4. 使用串口调试工具

```bash
# 使用screen
screen /dev/ttyUSB0 115200

# 使用minicom
minicom -D /dev/ttyUSB0 -b 115200

# 使用picocom
picocom -b 115200 /dev/ttyUSB0
```

## CI/CD集成

这个测试需要硬件,不适合常规CI。可以:

1. **手动运行:** 在开发环境运行
2. **专用测试机:** 连接设备的专用机器
3. **条件运行:** 检测到设备时才运行

```bash
# 检查设备是否存在
if lsusb | grep -q "1a86:55e0"; then
    echo "Device found, running flash test"
    ./test_real_firmware_flashing
else
    echo "No device found, skipping test"
fi
```

## 测试固件

如果没有固件文件,可以:

1. **从发布页面下载:**
   ```bash
   wget https://github.com/TechxArtisanStudio/Openterface_KM/releases/latest/download/firmware.hex
   ```

2. **从源码构建:**
   ```bash
   # 参考 Openterface_KM 仓库
   ```

3. **使用测试固件:**
   - 项目中可能包含测试固件
   - 联系开发团队获取

## 相关文档

- [CH32烧录指南](../docs/ch32_firmware_flashing.md)
- [WCH ISP协议](../wch/WCHProtocol.cpp)
- [GET_INFO命令](../serial/protocol/SerialProtocol.h)
- [串口协议](../serial/ch9329.h)

## 支持

遇到问题:
1. 检查上面的故障排除步骤
2. 查看dmesg日志
3. 在GitHub提交issue,附上:
   - 测试输出
   - dmesg输出
   - 系统信息 (`uname -a && lsb_release -a`)
   - 固件版本
