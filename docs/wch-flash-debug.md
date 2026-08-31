# WCH ISP Flash Tool Debug Log

## Problem Description

After flashing CH32V208GB firmware using a custom WCH ISP flash tool (Linux/libusb):
- ✅ Flash process completed (erase → program → verify all passed)
- ✅ Device re-enumerated as PID 0xFE0C (normal mode)
- ✅ `/dev/ttyACM0` appeared
- ❌ **CDC ACM serial port completely unresponsive** — GET_INFO and all other commands time out

However, user feedback indicates: **the same code flashed on Windows works correctly** (GET_INFO and other functional commands all work normally).

## Device Information

| Item | Value |
|---|---|
| Chip | CH32V208GB (CH32V20x BLE variant) |
| chipID | 0x83 |
| deviceType | 0x19 |
| BTVER | 02.80 |
| UID | 72-6F-1C-7A-4E-E0-DC-C9 |
| Code Flash | 192 KiB |
| EEPROM | 32 KiB |
| Firmware | `06(1).hex` (195832 bytes, Intel HEX) |
| Firmware after padding | 196608 bytes (192 KiB, exactly fills) |

## Troubleshooting Process

### Round 1: Comparing Original Code vs. Current Code

Compared the initial commit and current code via `git diff d7ce3593 -- wch/WCHFlasher.cpp`:

| Difference | Original Code | Modified Code | Impact |
|---|---|---|---|
| **EEPROM erase** | ❌ Not present | ✅ Added `dataErase()` | 🔴 May erase BLE config/calibration data the firmware depends on |
| **protect() call** | ✅ Called after flashing | ❌ Removed (comment says it triggers flash erase) | 🔴 **Critical: protect() may trigger automatic flash erase** |
| **unprotect() reset** | No reset after writing config | Reset + reconnect after writing config | 🟡 Added unnecessary complexity |
| **Firmware padding** | Flashed at original size | Padded to sector boundary before flashing | 🟡 Flashed an extra 776 bytes of 0xFF |
| **erase parameters** | `erase(firmwareSize)` converts to sectors internally | `erase(sectors)` signature changed | 🟡 Caller must convert correctly |
| **reset() behavior** | Waits for ispEnd response, sleeps 3s | Catches exception (device disconnects) | 🟡 Changed timing |
| **verify extra check** | Only checks `resp.ok` | Added payload[0] check | 🟡 May cause false positives |
| **protect nRDPR** | `config[1] = 0x00` | `config[1] = 0xFF` | 🟡 Different chips may behave differently |

#### Key Finding: protect() Causes Firmware to Be Erased

**Evidence:**
- After calling `protect()` (changing RDPR from 0xA5 to 0x00), the device **did not re-enumerate at all**
- After unplugging USB and plugging back in, the device still did not appear
- This indicates the firmware on the chip had been erased

**Root Cause Analysis:**
WCH CH32V series chip security mechanism: When RDPR (Read Protection) switches from unprotected state (0xA5) to protected state (0x00), the chip automatically performs a full Code Flash erase as a security side effect. This is to prevent attackers from reading flash contents after setting protection.

**Reference:** wchisp (ch32-rs/wchisp) also **does not call protect()** after flashing.

#### Why Does the Original Code "Work" on Windows?

Possible explanations:
1. The user was using the **official WCH flash tool** on Windows, not custom code
2. Or the Windows version's `protect()` call did not actually take effect due to timing issues
3. Or different chip batches have inconsistent erase behavior triggered by protect

#### Round 1 Fix

Final flash flow (after round 1 fix):

```
unprotect (if needed, with reset + reconnect) → erase → program → verify → reset
```

Key changes:
1. ❌ **Removed `dataErase()`** — Do not erase EEPROM, preserve firmware configuration data
2. ❌ **Removed `protect()`** — Avoid triggering automatic flash erase
3. ✅ **Added reset + reconnect to unprotect()** — Configuration changes on Linux/libusb require a reset to take effect
4. ✅ **Made reset() fault-tolerant** — Device may disconnect USB before responding, should not be treated as an error

**Result:** Flashing completed, device enumerated as ttyACM0, but GET_INFO still times out.

---

### Round 2: Cross-referencing with wchisp Reference Implementation

User pointed out: "It's still a flashing issue, probably because unprotect wasn't done correctly or something, you can refer to the wchisp in the project directory"

#### wchisp Flash Complete Flow Analysis (`src/main.rs` lines 226-270)

```rust
// Flash subcommand flow:
// dump_info → extend_firmware → erase_code → sleep(1s) → flash → sleep(500ms) → verify → reset

let mut binary = read_firmware_from_file(path)?;
extend_firmware_to_sector_boundary(&mut binary);  // Pad to 1024-byte boundary
let sectors = binary.len() / SECTOR_SIZE + 1;     // Number of sectors to erase
flashing.erase_code(sectors as u32)?;
sleep(Duration::from_secs(1));                     // Wait 1 second after erase
flashing.flash(&binary)?;                          // Program
sleep(Duration::from_millis(500));                 // Wait 500ms after program
flashing.verify(&binary)?;                         // Verify
let _ = flashing.reset();                          // Reset (ignore errors)
```

**Key Finding: wchisp Flash subcommand does NOT call unprotect()!**
unprotect is only available in the `config unprotect` subcommand.

#### Item-by-item Comparison: wchisp vs. Our Code

##### 1. USB Transport Layer (`src/transport/usb.rs` vs. `WCHUSBTransport.cpp`)

| Operation | wchisp | Our Code | Impact |
|---|---|---|---|
| `libusb_open` / `device.open()` | ✅ | ✅ | - |
| `detach_kernel_driver` | ❌ Not called | ✅ Called on Linux | 🟡 May interfere with ISP bootloader state |
| `libusb_reset_device` | ❌ **Not called** | ✅ **Called** | 🔴 **Critical difference!** May put ISP bootloader in undefined state |
| `set_active_configuration(1)` | ✅ | ✅ (`libusb_set_configuration`) | - |
| `claim_interface(0)` | ✅ | ✅ (`libusb_claim_interface`) | - |

##### 2. Transfer Timing (`src/transport/mod.rs` vs. `WCHUSBTransport.cpp`)

```rust
// wchisp transfer_with_wait():
fn transfer_with_wait(&mut self, cmd: Command, wait: Duration) -> Result<Response> {
    let req = &cmd.into_raw()?;
    self.send_raw(&req)?;
    sleep(Duration::from_micros(1)); // required for some Linux platform  ← Key!
    let resp = self.recv_raw(wait)?;
    anyhow::ensure!(req[0] == resp[0], "response command type mismatch");
    Response::from_raw(&resp)
}
```

| Difference | wchisp | Our Code |
|---|---|---|
| Write-then-read delay | ✅ `sleep(1µs)` — comment says "required for some Linux platforms" | ❌ **No delay** |
| Response command byte check | ✅ `req[0] == resp[0]` | ❌ Not checked |

##### 3. Intel HEX Parsing (`src/format.rs` vs. `WCHHexParser.cpp`)

```rust
// wchisp merge_sections():
fn merge_sections(mut sections: Vec<(u32, Cow<[u8]>)>) -> Result<Vec<u8>> {
    let start_address = sections.first().unwrap().0;
    let end_address = sections.last().unwrap().0 + sections.last().unwrap().1.len() as u32;
    let total_size = end_address - start_address;
    let mut binary = vec![0u8; total_size as usize];  // ← 0x00 padding!
    for (addr, sect) in sections {
        let sect_start = (addr - start_address) as usize;
        binary[sect_start..sect_start + sect.len()].copy_from_slice(&sect);
    }
    Ok(binary)
}
```

| Difference | wchisp | Our Code | Impact |
|---|---|---|---|
| HEX inter-segment padding byte | `0x00` | `0xFF` | 🔴 **Critical difference!** When firmware has gap segments, the data written to flash is completely different |

> **Note:** The firmware's Intel HEX file may have multiple segments (e.g., 0x00010000 and 0x00020000), and the gaps between them are filled. If the compiler expects `0x00` in the gaps (e.g., `.bss`, `COMMON` sections) but we fill with `0xFF`, the firmware will read incorrect data from those locations at runtime, causing crashes or functional anomalies.

##### 4. Firmware Padding to Sector Boundary (`src/main.rs` vs. `WCHFlasher.cpp`)

```rust
// wchisp:
fn extend_firmware_to_sector_boundary(buf: &mut Vec<u8>) {
    if buf.len() % 1024 != 0 {
        let remain = 1024 - (buf.len() % 1024);
        buf.extend_from_slice(&vec![0; remain]);  // ← 0x00 padding
    }
}
```

| Difference | wchisp | Our Code | Impact |
|---|---|---|---|
| Sector boundary padding | `0x00` | `0xFF` | 🟡 Flash erase results in 0xFF, so both have the same effect (the value written after XOR ends up identical) |

##### 5. Erase Sector Calculation

```rust
// wchisp: sectors = binary.len() / SECTOR_SIZE + 1 (erases 1 extra sector)
// And has a minimum erase sector count:
pub const fn min_erase_sector_number(&self) -> u32 {
    if self.device_type() == 0x10 { 4 } else { 8 }
}
```

| Difference | wchisp | Our Code | Impact |
|---|---|---|---|
| Number of erase sectors | `size / 1024 + 1` | `(size + 1023) / 1024` | 🟡 wchisp erases 1 extra sector |
| Minimum erase count | 8 (for 0x19 devices) | No minimum limit | 🟡 May cause occasional erase failures |

##### 6. Flash Timing

| Step | wchisp | Our Code | Impact |
|---|---|---|---|
| Wait after erase | ✅ `sleep(1s)` | ❌ No wait | 🟡 Device may not have completed erase |
| Wait after program | ✅ `sleep(500ms)` | ❌ No wait | 🟡 Device may not have completed programming |

##### 7. Data Packet Format Comparison

Compared all command formats one by one, **all are identical**:

| Command | wchisp | Our Code | Match? |
|---|---|---|---|
| Identify | `[A1][12 00][devid][devtype]["MCU ISP & WCH.CN"]` | Same | ✅ |
| IspEnd | `[A2][01][00][reason]` | Same | ✅ |
| IspKey | `[A3][len_lo 00][key...]` | Same | ✅ |
| Erase | `[A4][04 00][sectors:u32LE]` | Same | ✅ |
| Program | `[A5][len][addr:u32LE][padding][data]` | Same | ✅ |
| Verify | `[A6][len][addr:u32LE][padding][data]` | Same | ✅ |
| ReadConfig | `[A7][02 00][mask][00]` | Same | ✅ |
| WriteConfig | `[A8][len][mask][00][data...]` | Same | ✅ |

##### 8. XOR Encryption Algorithm

```rust
// wchisp flash_chunk:
let xored = raw.iter().enumerate().map(|(i, x)| x ^ key[i % 8]);
```

```cpp
// Our xorChunk:
out[i] = data[i] ^ m_xorKey[(startOffset + i) % 8];
```

**Identical** ✅ — Both XOR key rotation methods are equivalent.

##### 9. XOR Key Generation

```rust
// wchisp:
fn xor_key(&self) -> [u8; 8] {
    let checksum = self.chip_uid().iter().fold(0u8, |acc, &x| acc.overflowing_add(x).0);
    let mut key = [checksum; 8];
    key.last_mut().map(|x| *x = x.overflowing_add(self.chip.chip_id).0);
    key
}
```

```cpp
// Our deriveXorKey:
uint8_t uidSum = 0;
for (size_t i = 0; i < uidSize && i < m_uid.size(); ++i)
    uidSum = static_cast<uint8_t>(uidSum + m_uid[i]);
m_xorKey.assign(8, uidSum);
m_xorKey[7] = static_cast<uint8_t>(m_xorKey[7] + m_chip.chipID);
```

**Identical** ✅ — Same algorithm.

---

### Round 2 Fixes (Applied)

#### Fix 1: `WCHUSBTransport.cpp` — Removed `libusb_reset_device` and `detach_kernel_driver`

```cpp
// Before fix:
#if defined(__linux__)
    if (libusb_kernel_driver_active(m_handle, k_iface) == 1) {
        rc = libusb_detach_kernel_driver(m_handle, k_iface);
        ...
    }
#endif
    libusb_reset_device(m_handle);
    rc = libusb_set_configuration(m_handle, 1);

// After fix:
// NOTE: Do not call libusb_reset_device or detach_kernel_driver.
// The wchisp reference implementation calls neither.
// A bus reset may put the ISP bootloader in an undefined state.
rc = libusb_set_configuration(m_handle, 1);
```

#### Fix 2: `WCHUSBTransport.cpp` — Added write-then-read delay

```cpp
// After fix in transfer():
libusb_bulk_write(...);
// wchisp: sleep(1µs) "required for some Linux platform"
std::this_thread::sleep_for(std::chrono::microseconds(1));
libusb_bulk_read(...);
```

#### Fix 3: `WCHHexParser.cpp` — 🔴 Critical Fix: Padding byte changed to 0x00

```cpp
// Before fix:
std::vector<uint8_t> result(totalSize, 0xFF);  // 0xFF padding

// After fix:
std::vector<uint8_t> result(totalSize, 0x00);  // 0x00 padding, matching wchisp
```

#### Fix 4: `WCHFlasher.cpp` — Sector boundary padding changed to 0x00

```cpp
// Before fix:
padded.insert(padded.end(), remain, 0xFF);

// After fix:
padded.insert(padded.end(), remain, 0x00);  // Match wchisp
```

#### Fix 5: `WCHFlasher.cpp` — Erase sector calculation and minimum value

```cpp
// Before fix:
uint32_t sectors = (firmwareSize + SectorSize - 1) / SectorSize;

// After fix:
uint32_t sectors = firmwareSize / SectorSize + 1;  // Match wchisp
uint32_t minSectors = (m_chip.deviceType == 0x10) ? 4 : 8;
if (sectors < minSectors) sectors = minSectors;
```

#### Fix 6: `WCHFlasher.cpp` — Added timing delays and firmware padding to flash()

```cpp
void WCHFlasher::flash(const std::vector<uint8_t>& rawFirmware, ...) {
    // Added: Pad firmware to sector boundary
    auto firmware = extendFirmwareToSectorBoundary(rawFirmware);

    // Stage 0: unprotect if needed
    if (m_codeFlashProtected) unprotect();

    // Stage 1: erase
    erase(firmware.size());
    std::this_thread::sleep_for(std::chrono::seconds(1));     // Added: wait 1s after erase

    // Stage 2: program
    program(firmware, progress);
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Added: wait 500ms after program

    // Stage 3: verify
    verify(firmware, progress);

    // Stage 4: reset
    reset();
}
```

---

### Round 2 Test Results

**Flashing:** ✅ Completed (erase → program → verify 100%)

```
=== Flash + GET_INFO Test ===
Firmware loaded: 195832 bytes
→ After padding: 196608 bytes (192 KiB)

Chip: CH32V208GB (Code Flash: 192 KiB, EEPROM: 32 KiB)
Flash Protected: No

[5%] Erasing flash...
[10%] Erase complete
[0%-50%] Programming: 196608/196608 bytes
[50%-95%] Verifying: 196608/196608 bytes
[96%] Resetting device...
[100%] Flash complete!

✓ Device found on /dev/ttyACM0
```

**GET_INFO:** ❌ Still times out

```
Attempt 1: sending GET_INFO (57 ab 00 01 00 03)
  Timeout - no response
Attempt 2: sending GET_INFO (57 ab 00 01 00 03)
  Timeout - no response
Attempt 3: sending GET_INFO (57 ab 00 01 00 03)
  Timeout - no response
```

---

## Current Status Analysis

### Confirmed Working

| Item | Status | Notes |
|---|---|---|
| USB ISP communication | ✅ | identify / readConfig / erase / program / verify all successful |
| Flash erase | ✅ | Erase command returns OK |
| Flash programming | ✅ | All chunks programmed successfully |
| Flash verification | ✅ | All chunks verified successfully (data matches) |
| Device re-enumeration | ✅ | PID 0xFE0C, /dev/ttyACM0 appears |
| USB CDC ACM | ✅ | ttyACM0 can be opened, data can be written |
| Packet format | ✅ | Exactly matches wchisp reference implementation |
| XOR encryption | ✅ | Matches wchisp algorithm |

### Remaining Issues

| Item | Status | Notes |
|---|---|---|
| GET_INFO response | ❌ | No response after sending 57 AB 00 01 00 + checksum |

### Possible Root Causes (To Investigate)

1. **Firmware itself issue** — The Intel HEX file may have multiple address segments; although padding bytes have been changed to 0x00, data in specific address regions may be incorrect
2. **Firmware initialization requires specific conditions** — Firmware may need DTR/RTS signals, specific baud rate handshake, or a specific initialization sequence before it begins processing commands
3. **EEPROM data lost** — The previous dataErase() may have erased BLE config/calibration data the firmware depends on (EEPROM was not restored)
4. **Flash content actually incorrect** — Although verify passed (it compares XOR-encrypted data), there may be some edge case
5. **ISP bootloader state residue** — Even after removing libusb_reset_device, previous operations may have left some state behind

## Round 3: Fix 1 Applied — Verify payload[0] Check

### Key Protocol Difference Found Between wchisp and Our Code

**wchisp `Response::from_raw()` always returns `Ok`** (code has `if true` — status byte is completely ignored):
```rust
// wchisp protocol.rs:316-329
if true {  // ← Always true! Status byte is ignored
    ...
    Ok(Response::Ok(remain.to_vec()))
}
```

Therefore wchisp's verify actually relies on `payload[0] == 0x00` to determine a match:
```rust
// wchisp flashing.rs:452
anyhow::ensure!(resp.payload()[0] == 0x00, "Verify failed, mismatch");
```

**Our code previously only checked `resp.ok` (status byte == 0x00)**, without checking `payload[0]`.

### Fix 1 Applied

Added `payload[0]` check in `WCHFlasher.cpp` verify():
```cpp
if (resp.payload.empty() || resp.payload[0] != 0x00) {
    throw WCHFlashError("Verify MISMATCH at offset 0x...");
}
```

### Fix 1 Test Results

| Item | Result |
|---|---|
| Flash process | ✅ Completed (erase → program → verify → reset) |
| Verify check | ✅ Passed (payload[0]==0x00, data matches) |
| Device re-enumeration | ✅ PID 0xFE0C, /dev/ttyACM0 appears |
| GET_INFO response | ❌ **Still times out** |
| DTR/RTS assertion | ❌ No effect |
| Different baud rates (9600/115200) | ❌ No effect |
| Waiting for auto-sent data | ❌ No data |

### Key Conclusion

**Flash content is correct** — verify passed the payload[0] check (consistent with wchisp behavior). The firmware is indeed running (USB re-enumeration works normally). But the firmware does not respond to any commands.

This means the problem is **not at the flash programming level**, but at a higher layer:

### Possible Root Causes (Updated)

1. **🔴 EEPROM data lost** — During round 1 debugging, `dataErase()` may have erased BLE config/calibration data the firmware depends on. Even though the dataErase call was later removed, the previously erased data cannot be recovered.
2. **Firmware itself issue** — The firmware may have bugs or depend on specific hardware states
3. **Firmware requires a specific initialization sequence** — Not as simple as DTR/RTS; may require a specific command sequence or configuration

### Next Troubleshooting Steps (Updated)

1. **🔴 Restore EEPROM data** — Use wchisp or the official tool to read valid EEPROM data, then write it back via ISP DataWrite
2. **Flash directly with wchisp tool** — Requires Rust environment to compile wchisp, then flash the same firmware for verification
3. **ISP flash readback** — Read back flash contents via ISP DataRead command and compare byte-by-byte with the original firmware
4. **Review firmware source code** — Check the startup conditions of the firmware's CDC ACM command processing loop
5. **Check USER configuration bytes** — Confirm whether SRAM_CODE_MODE and other settings are correct

## Status

- [x] Document findings
- [x] Remove dataErase() (do not erase EEPROM)
- [x] Remove protect() (avoid automatic flash erase)
- [x] Add reset + reconnect to unprotect()
- [x] Cross-reference wchisp to check USB transport layer differences
- [x] Remove libusb_reset_device
- [x] Remove detach_kernel_driver
- [x] Add 1µs write-then-read delay
- [x] 🔴 Intel HEX padding byte changed to 0x00
- [x] Erase sector calculation matches wchisp
- [x] Add erase/program timing delays
- [x] 🔴 🔴 🔴 Add verify payload[0] check (Fix 1)
- [x] Verify verify passes (flash content is correct)
- [ ] Verify GET_INFO command response ← **Still unresolved**
- [ ] Cross-validate with wchisp tool
- [ ] ISP flash readback to verify content correctness
- [ ] 🔴 Restore EEPROM data
