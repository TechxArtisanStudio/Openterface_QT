# 固件烧录问题诊断报告

## 问题描述

用户使用自定义烧录工具烧录固件后,GET_INFO命令无响应,但官方烧录工具可以正常烧录。

## 根本原因

**固件大小超过Flash容量!**

### 芯片规格
- **芯片型号:** CH32V208GB
- **Flash大小:** 128 KiB = 131,072 字节
- **EEPROM大小:** 32 KiB
- **Device ID:** chipID=0x83, deviceType=0x19

### 固件文件
- **文件:** `/home/bot/project/06(1).hex`
- **实际数据大小:** 195,832 字节 ≈ 191 KiB
- **超出:** 64,760 字节 (约50%)

### 为什么烧录"成功"但固件不工作

1. **烧录过程:** 
   - 擦除: 成功(但只擦除了128 KiB)
   - 编程: 写入了195,832字节,但超出Flash边界的数据被丢弃
   - 验证: 可能只验证了前128 KiB,或验证逻辑有bug

2. **结果:**
   - 固件被截断,只有前128 KiB被写入
   - 固件不完整,无法正常运行
   - GET_INFO命令无响应

## 解决方案

### 立即解决
使用正确大小的固件文件(≤ 128 KiB)

### 长期修复
在烧录工具中添加固件大小检查:

```cpp
if (firmwareData.size() > flashSize) {
    fprintf(stderr, "错误: 固件太大!\n");
    fprintf(stderr, "固件大小 (%zu 字节) 超过 Flash 大小 (%u 字节)\n",
            firmwareData.size(), flashSize);
    return 1;
}
```

## 测试验证

已将大小检查添加到 `tests/build/flash_with_log.cpp`

请重新进入ISP模式后运行:
```bash
./flash_with_log "/home/bot/project/06(1).hex"
```

应该会看到:
```
✗✗✗ 错误: 固件太大! ✗✗✗
固件大小 (195832 字节) 超过 Flash 大小 (131072 字节)
超出 64760 字节
```

## 后续步骤

1. **获取正确的固件:**
   - 联系固件开发者,获取适用于CH32V208GB (128 KiB Flash) 的版本
   - 或使用官方工具烧录,看是否有大小警告

2. **验证固件大小:**
   ```bash
   # 解析HEX文件,查看实际数据大小
   python3 << 'EOF'
   import intelhex
   ih = intelhex.IntelHex("/home/bot/project/06(1).hex")
   print(f"固件大小: {ih.maxaddr() - ih.minaddr() + 1} 字节")
   EOF
   ```

3. **测试正确固件:**
   - 使用大小合适的固件重新测试
   - 验证GET_INFO命令是否正常响应

## 相关代码

- **芯片数据库:** `wch/WCHDevice.cpp` (第55行)
- **烧录工具:** `tests/build/flash_with_log.cpp`
- **GET_INFO测试:** `tests/build/test_getinfo.cpp`
