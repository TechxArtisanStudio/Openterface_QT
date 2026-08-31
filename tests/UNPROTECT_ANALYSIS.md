# 烧录问题分析报告

## 问题描述
使用自定义工具烧录固件后,GET_INFO命令无响应,但官方工具可以正常烧录。

## 与wchisp参考实现的对比分析

### 1. unprotect流程 ✓ 一致
**wchisp实现:**
```rust
pub fn unprotect(&mut self, force: bool) -> Result<()> {
    if !force && !self.code_flash_protected {
        return Ok(());
    }
    let read_conf = Command::read_config(CFG_MASK_RDPR_USER_DATA_WPR);
    let resp = self.transport.transfer(read_conf)?;
    anyhow::ensure!(resp.is_ok(), "read_config failed");

    let mut config = resp.payload()[2..14].to_vec(); // 4 x u32
    config[0] = 0xa5; // code flash unprotected
    config[1] = 0x5a;

    // WPR register
    config[8..12].copy_from_slice(&[0xff; 4]);

    let write_conf = Command::write_config(CFG_MASK_RDPR_USER_DATA_WPR, config);
    let resp = self.transport.transfer(write_conf)?;
    anyhow::ensure!(resp.is_ok(), "write_config failed");

    log::info!("Code Flash unprotected");
    self.reset()?;
    Ok(())
}
```

**我们的实现:**
```cpp
void WCHFlasher::unprotect()
{
    if (!m_codeFlashProtected) return;

    // Step 1: Read current config
    auto packet = WCHPacketBuilder::readConfig(WCHConstants::CfgMaskRDPRUserDataWPR);
    auto raw = doTransfer(packet, "readConfig(unprotect)");
    // ... 验证响应 ...

    // Patch: RDPR=0xA5 (unprotected), nRDPR=0x5A, WPR=0xFFFFFFFF
    std::vector<uint8_t> config(resp.payload.begin() + 2,
                                resp.payload.begin() + 14);
    config[0] = 0xA5;
    config[1] = 0x5A;
    config[8]  = 0xFF;
    config[9]  = 0xFF;
    config[10] = 0xFF;
    config[11] = 0xFF;

    // Step 2: Write the unprotect config
    auto wpacket = WCHPacketBuilder::writeConfig(WCHConstants::CfgMaskRDPRUserDataWPR, config);
    // ... 验证响应 ...

    // Step 3: Reset the device
    m_codeFlashProtected = false;
    reset();
}
```

**结论:** ✓ 逻辑一致

### 2. deriveXorKey ✓ 一致
**wchisp实现:**
```rust
fn xor_key(&self) -> [u8; 8] {
    let checksum = self
        .chip_uid()
        .iter()
        .fold(0_u8, |acc, &x| acc.overflowing_add(x).0);
    let mut key = [checksum; 8];
    key.last_mut()
        .map(|x| *x = x.overflowing_add(self.chip.chip_id).0);
    key
}
```

**我们的实现:**
```cpp
void WCHFlasher::deriveXorKey()
{
    uint8_t uidSum = 0;
    for (size_t i = 0; i < static_cast<size_t>(m_chip.uidSize) && i < m_uid.size(); ++i)
        uidSum = static_cast<uint8_t>(uidSum + m_uid[i]);

    m_xorKey.assign(8, uidSum);
    m_xorKey[7] = static_cast<uint8_t>(m_xorKey[7] + m_chip.chipID);
}
```

**结论:** ✓ 逻辑一致

### 3. erase ✗ 存在差异
**wchisp实现:**
```rust
pub fn erase_code(&mut self, mut sectors: u32) -> Result<()> {
    let min_sectors = self.chip.min_erase_sector_number();
    if sectors < min_sectors {
        sectors = min_sectors;  // 强制最小擦除数
        log::warn!(
            "erase_code: set min number of erased sectors to {}",
            sectors
        );
    }
    let erase = Command::erase(sectors);
    let resp = self
        .transport
        .transfer_with_wait(erase, Duration::from_millis(5000))?;
    anyhow::ensure!(resp.is_ok(), "erase failed");

    log::info!("Erased {} code flash sectors", sectors);
    Ok(())
}

pub const fn min_erase_sector_number(&self) -> u32 {
    if self.device_type() == 0x10 {
        4
    } else {
        8  // CH32V208 (device_type=0x19) 最小需要擦除8个sector
    }
}
```

**我们的实现:**
```cpp
void WCHFlasher::erase(uint32_t firmwareSize)
{
    uint32_t sectors = (firmwareSize + WCHConstants::SectorSize - 1) /
                        WCHConstants::SectorSize;
    if (sectors == 0) sectors = 1;  // 只检查是否为0,没有最小值检查!

    auto packet = WCHPacketBuilder::erase(sectors);
    auto raw = doTransfer(packet, "erase");
    // ...
}
```

**问题:** 缺少最小sector数检查!对于CH32V208(device_type=0x19),最小应该擦除8个sector(8KB)。如果固件很小,我们可能只擦除1-2个sector,导致擦除不完整。

**修复方案:**
```cpp
void WCHFlasher::erase(uint32_t firmwareSize)
{
    uint32_t sectors = (firmwareSize + WCHConstants::SectorSize - 1) /
                        WCHConstants::SectorSize;
    
    // 添加最小sector数检查
    uint32_t minSectors = (m_chip.deviceType == 0x10) ? 4 : 8;
    if (sectors < minSectors) {
        sectors = minSectors;
    }

    auto packet = WCHPacketBuilder::erase(sectors);
    // ...
}
```

### 4. program流程 ✓ 一致
**wchisp实现:**
```rust
pub fn flash(&mut self, raw: &[u8]) -> Result<()> {
    let key = self.xor_key();
    let key_checksum = key.iter().fold(0_u8, |acc, &x| acc.overflowing_add(x).0);

    // NOTE: use all-zero key seed for now.
    let isp_key = Command::isp_key(vec![0; 0x1e]);  // 30个0x00
    let resp = self.transport.transfer(isp_key)?;
    anyhow::ensure!(resp.is_ok(), "isp_key failed");
    anyhow::ensure!(resp.payload()[0] == key_checksum, "isp_key checksum failed");

    const CHUNK: usize = 56;
    let mut address = 0x0;

    for ch in raw.chunks(CHUNK) {
        self.flash_chunk(address, ch, key)?;
        address += ch.len() as u32;
    }
    // NOTE: require a write action of empty data for success flashing
    self.flash_chunk(address, &[], key)?;  // 最后的空chunk

    log::info!("Code flash {} bytes written", address);
    Ok(())
}

fn flash_chunk(&mut self, address: u32, raw: &[u8], key: [u8; 8]) -> Result<()> {
    let xored = raw.iter().enumerate().map(|(i, x)| x ^ key[i % 8]);
    let padding = rand::random();  // 随机填充
    let cmd = Command::program(address, padding, xored.collect());
    let resp = self
        .transport
        .transfer_with_wait(cmd, Duration::from_millis(300))?;
    anyhow::ensure!(resp.is_ok(), "program 0x{:08x} failed", address);
    Ok(())
}
```

**我们的实现:** 基本一致,都使用:
- 30字节0x00的ISP key
- 56字节chunk
- 随机padding
- 最后的空chunk

**结论:** ✓ 逻辑一致

### 5. flash()主流程 ✓ 一致
**wchisp:** 不直接调用unprotect/erase/verify,这些由调用者分别调用
**我们:** flash()内部调用 unprotect → reset → reconnect → reidentify → erase → program → verify → reset

**关键差异:** 我们的代码在flash()结束时**不调用protect()**,因为注释说:
```cpp
// NOTE: we intentionally do NOT re-protect flash here.
// On WCH CH32V chips (including CH32V208), changing RDPR from 0xA5
// (unprotected) to 0x00 (protected) triggers an automatic code flash
// erase as a security side-effect. This would erase the firmware we
// just wrote. The reference tool (ch32-rs/wchisp) also does NOT call
// protect after flashing — see src/flashing.rs.
```

这个设计是正确的。

## 发现的主要问题

### 问题1: 固件大小超过Flash容量
- **固件大小:** 195,832 字节 ≈ 191 KiB
- **Flash容量:** 131,072 字节 = 128 KiB
- **超出:** 64,760 字节 (约50%)

**影响:** 固件被截断,只有前128KB被写入,导致固件不完整无法运行。

**解决方案:** 使用正确大小的固件(≤128KB)

### 问题2: erase缺少最小sector数检查
- **wchisp:** 对于device_type != 0x10的芯片,最小擦除8个sector
- **我们:** 没有最小值检查

**影响:** 如果固件很小(<8KB),可能导致擦除不完整

**修复:** 添加最小sector数检查

## 结论

1. **主要问题:** 固件太大(195KB > 128KB Flash),导致固件被截断
2. **次要问题:** erase()缺少最小sector数检查

**建议:**
1. 首先获取正确大小的固件文件(≤128KB)
2. 修复erase()函数添加最小sector数检查
3. 使用小固件重新测试,验证GET_INFO命令是否响应

如果小固件可以正常工作,说明问题确实是固件大小导致的。
如果小固件也不能工作,需要进一步调查unprotect/reconnect流程。
