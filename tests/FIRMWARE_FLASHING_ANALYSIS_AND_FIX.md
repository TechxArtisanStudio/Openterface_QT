# WCH烧录工具问题分析与修复

## 问题背景
用户使用自定义烧录工具烧录CH32V208GB芯片后,GET_INFO命令无响应。但官方WCH烧录工具可以正常烧录相同固件。

## 分析过程

### 1. 固件大小检查
首先检查发现:
- **固件大小:** 195,832 字节 (≈191 KiB)
- **Flash容量:** 131,072 字节 (=128 KiB)
- **问题:** 固件超出Flash容量64,760字节(约50%)

这是导致GET_INFO无响应的**主要原因**:固件被截断,只有前128KB被写入,导致固件不完整无法运行。

### 2. 与wchisp参考实现对比

详细对比了wchisp (https://github.com/ch32-rs/wchisp) 的实现,发现:

| 功能 | wchisp实现 | 我们的实现 | 结论 |
|------|-----------|-----------|------|
| unprotect() | readConfig → patch RDPR/WPR → writeConfig → reset | 相同 | ✓ 一致 |
| deriveXorKey() | uid字节和 → 8字节key → 最后字节+chipID | 相同 | ✓ 一致 |
| erase() | **最小sector数检查**(8 sectors for CH32V208) | **缺少** | ✗ **有问题** |
| program() | 30字节ISP key → 56字节chunk → 随机padding → 空chunk | 相同 | ✓ 一致 |
| verify() | 重发ISP key → 56字节chunk → 检查payload[0]==0x00 | 相同 | ✓ 一致 |

### 3. 发现的关键差异

**erase()缺少最小sector数检查**

wchisp实现:
```rust
pub fn erase_code(&mut self, mut sectors: u32) -> Result<()> {
    let min_sectors = self.chip.min_erase_sector_number();
    if sectors < min_sectors {
        sectors = min_sectors;  // 强制最小擦除数
    }
    // ...
}

pub const fn min_erase_sector_number(&self) -> u32 {
    if self.device_type() == 0x10 {
        4
    } else {
        8  // CH32V208需要至少8个sector
    }
}
```

我们的实现:
```cpp
void WCHFlasher::erase(uint32_t firmwareSize)
{
    uint32_t sectors = (firmwareSize + WCHConstants::SectorSize - 1) /
                        WCHConstants::SectorSize;
    if (sectors == 0) sectors = 1;  // 只检查是否为0,没有最小值!
    // ...
}
```

**影响:** 如果固件很小(<8KB),我们只擦除1个sector,而芯片可能需要至少擦除8个sector才能正常工作。

## 修复方案

### 修复1: 添加最小sector数检查

**文件:** `wch/WCHFlasher.cpp`

```cpp
void WCHFlasher::erase(uint32_t firmwareSize)
{
    uint32_t sectors = (firmwareSize + WCHConstants::SectorSize - 1) /
                        WCHConstants::SectorSize;

    // WCH chips require a minimum number of sectors to be erased.
    // For device_type 0x10: min 4 sectors (4KB)
    // For other device types (including 0x19/CH32V208): min 8 sectors (8KB)
    // Reference: wchisp src/device.rs min_erase_sector_number()
    uint32_t minSectors = (m_chip.deviceType == 0x10) ? 4 : 8;
    if (sectors < minSectors) {
        sectors = minSectors;
    }

    auto packet = WCHPacketBuilder::erase(sectors);
    auto raw = doTransfer(packet, "erase");

    WCHResponse resp;
    if (!WCHResponse::parse(raw, resp) || !resp.ok)
        throw WCHFlashError("Erase failed");
}
```

**状态:** ✓ 已修复

### 修复2: 添加固件大小检查

**文件:** `wch/WCHFlasher.cpp` (flash()方法)

建议在flash()开始时添加固件大小检查:

```cpp
void WCHFlasher::flash(const std::vector<uint8_t>& firmware,
                        const WCHProgressCallback& progress)
{
    if (firmware.empty())
        throw WCHFlashError("Firmware data is empty");

    // 检查固件大小
    if (firmware.size() > m_chip.flashSize) {
        throw WCHFlashError(
            "Firmware too large: " + std::to_string(firmware.size()) +
            " bytes exceeds flash capacity: " + std::to_string(m_chip.flashSize) +
            " bytes. Firmware will be truncated and won't work properly.");
    }

    // ... 继续烧录流程
}
```

**状态:** 建议添加(可选,但强烈推荐)

## 测试建议

### 测试1: 使用小固件测试
1. 获取一个小于128KB的固件文件
2. 烧录固件
3. 等待设备重新枚举
4. 发送GET_INFO命令验证

**预期结果:** GET_INFO应该正常响应

### 测试2: 验证最小sector擦除
1. 使用很小的固件(<8KB)
2. 观察erase步骤的日志
3. 应该看到实际擦除了8个sector(而不是1个)

### 测试3: 大固件应该失败
1. 使用195KB的固件
2. 如果添加了大小检查,应该在开始时失败
3. 如果没有检查,固件会被截断,GET_INFO不会响应

## 结论

1. **主要问题:** 固件太大(195KB > 128KB Flash)导致固件被截断
2. **次要问题:** erase()缺少最小sector数检查(已修复)

**解决方案:**
1. 使用正确大小的固件文件(≤128KB)
2. 已修复erase()的最小sector数检查
3. 建议添加固件大小检查以避免截断

## 参考
- wchisp参考实现: https://github.com/ch32-rs/wchisp
- CH32V208数据手册: WCH官方文档
