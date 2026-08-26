# Firmware Flashing Test - Quick Start Guide

## 立即开始测试

### 方法 1: 使用自动化脚本 (推荐)

```bash
cd tests
./run_firmware_flashing_tests.sh
```

脚本会自动:
- ✓ 检查 Qt6 和 CMake 是否已安装
- ✓ 创建构建目录
- ✓ 配置和编译测试
- ✓ 提供运行选项

### 方法 2: 手动构建和运行

```bash
cd tests
mkdir -p build && cd build
cmake ..
make test_firmware_flashing
./test_firmware_flashing
```

## 测试内容

这个测试套件包含 10 个测试用例,专门用于诊断 Linux 上的烧录问题:

### 核心测试

1. **testCompleteFlashWorkflow** - 完整的烧录流程模拟
   - ISP模式检测 → 烧录重置 → USB重新枚举 → 串口重连

2. **testGetInfoCommandStructure** - GET_INFO 命令验证
   - 验证使用 0x01 命令检查烧录是否成功
   - 解析固件版本、连接状态等信息

3. **testUnstableUSBDuringFlash** - USB连接不稳定测试
   - 模拟烧录过程中的快速插拔

4. **testDelayedReenumeration** - 延迟重新枚举测试
   - 测试设备重启后的各种延迟场景

### 压力测试

5. **testMultipleFlashCycles** - 多次烧录循环
   - 50次连续烧录循环,检测内存泄漏

### 集成测试

6. **testFlashWithSerialCommunication** - 完整集成测试
   - 烧录 + 串口通信 + GET_INFO 验证

## 运行特定测试

```bash
# 运行单个测试
./test_firmware_flashing testCompleteFlashWorkflow

# 运行所有测试
./test_firmware_flashing

# 详细输出
./test_firmware_flashing -v2
```

## 预期结果

成功的测试输出:

```
********* Start testing of TestFirmwareFlashing *********
Config: Using QtTest library 6.x.x
PASS   : TestFirmwareFlashing::initTestCase()
PASS   : TestFirmwareFlashing::testCompleteFlashWorkflow()
PASS   : TestFirmwareFlashing::testUnstableUSBDuringFlash()
...
PASS   : TestFirmwareFlashing::testFlashWithSerialCommunication()
Totals: 12 passed, 0 failed, 0 skipped
********* Finished testing of TestFirmwareFlashing *********
```

## 如果测试失败

### 1. Qt SerialPort 模块缺失

**错误信息:**
```
Project ERROR: Unknown module(s) in QT: serialport
```

**解决方案:**
```bash
# Ubuntu/Debian
sudo apt install libqt6serialport6-dev

# Fedora
sudo dnf install qt6-qtserialport-devel

# Arch
sudo pacman -S qt6-serialport
```

### 2. CMake 版本过低

**错误信息:**
```
CMake Error: CMake 3.16 or higher is required.
```

**解决方案:**
```bash
# Ubuntu 20.04+
sudo apt install cmake

# 或从官网下载最新版
wget https://github.com/Kitware/CMake/releases/download/v3.27.7/cmake-3.27.7-linux-x86_64.sh
sudo sh cmake-3.27.7-linux-x86_64.sh --prefix=/usr/local --skip-license
```

### 3. 构建失败

**检查构建日志:**
```bash
cat build/build_output.log
```

**常见问题:**
- Qt6 未完整安装
- 编译器不支持 C++17
- 缺少依赖库

## Linux 串口问题诊断

如果测试通过但实际烧录失败,检查以下几点:

### 1. udev 规则

```bash
# 检查规则是否存在
ls -l /etc/udev/rules.d/51-opf-wchflash.rules

# 如果不存在,创建规则
sudo tee /etc/udev/rules.d/51-opf-wchflash.rules <<'EOF'
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55e0", TAG+="uaccess", MODE="0666"
SUBSYSTEM=="usb", ATTRS{idVendor}=="4348", ATTRS{idProduct}=="55e0", TAG+="uaccess", MODE="0666"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="fe0c", TAG+="uaccess", MODE="0666"
EOF

# 重新加载规则
sudo udevadm control --reload-rules
sudo udevadm trigger
```

### 2. 用户权限

```bash
# 检查是否在 dialout 组
groups | grep dialout

# 添加到 dialout 组
sudo usermod -a -G dialout $USER
# 然后重新登录
```

### 3. 设备检测

```bash
# 检查 ISP 模式设备
lsusb | grep -E "55e0|55E0"
# 应该看到: 1a86:55e0

# 检查正常模式设备
lsusb | grep -E "fe0c|FE0C"
# 应该看到: 1a86:fe0c

# 检查串口设备
ls -l /dev/ttyUSB* /dev/ttyACM*
```

### 4. 内核日志

```bash
# 查看最近的 USB 设备消息
dmesg | tail -30

# 实时监控 USB 事件
sudo dmesg -w
```

## 使用 GET_INFO 命令验证烧录

烧录成功后,可以使用 GET_INFO 命令 (0x01) 验证固件:

**命令格式:**
```
发送: 57 AB 00 01 00
响应: 57 AB 00 81 06 [版本] [连接状态] [指示灯] [保留...] [校验和]
```

**Python 示例:**
```python
import serial

ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
cmd = bytes.fromhex('57 AB 00 01 00')
ser.write(cmd)
response = ser.read(12)

if response[0:2] == b'\x57\xAB' and response[3] == 0x81:
    version = response[5]
    target_connected = response[6] != 0
    print(f"固件版本: {version}")
    print(f"目标连接: {target_connected}")
```

## 下一步

1. **运行测试:** `./run_firmware_flashing_tests.sh`
2. **查看详细文档:** `cat FIRMWARE_FLASHING_TEST_README.md`
3. **如果发现问题:** 检查上面的故障排除步骤
4. **如果测试通过但实际烧录失败:** 检查 udev 规则和权限

## 技术支持

如果遇到问题:
1. 运行测试并保存输出: `./test_firmware_flashing -v2 > test_output.log 2>&1`
2. 收集系统信息: `uname -a && lsb_release -a`
3. 在 GitHub 上提交 issue,附上测试输出

## 相关文档

- [完整测试文档](./FIRMWARE_FLASHING_TEST_README.md)
- [CH32 烧录指南](../docs/ch32_firmware_flashing.md)
- [串口热插拔测试](./hotplug/)
