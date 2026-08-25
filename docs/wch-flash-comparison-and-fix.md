# WCH ISP Flash Flow Comparison & Fix Proposal

## Flowchart: wchisp vs Our Implementation

```mermaid
flowchart TB
    subgraph wchisp["wchisp Flash Flow (Rust Reference)"]
        W1[get_flashing] --> W2[identify + read_config ×2]
        W2 --> W3[dump_info - logging only]
        W3 --> W4[read_firmware + extend to sector boundary]
        W4 --> W5[erase_code - sectors = size/1024 + 1]
        W5 --> W6[sleep 1s]
        W6 --> W7["flash() - send ISP key (30 zeros)"]
        W7 --> W8[flash_chunk loop - 56 byte chunks]
        W8 --> W9[final empty chunk - terminator]
        W9 --> W10[sleep 500ms]
        W10 --> W11["verify() - send ISP key again"]
        W11 --> W12[verify_chunk loop]
        W12 --> W13["check payload[0] == 0x00 ⚠️"]
        W13 --> W14[reset - ispEnd]
    end

    subgraph ours["Our Flash Flow (C++)"]
        O1[Constructor: identify + readConfig + deriveXorKey] --> O2[extendFirmwareToSectorBoundary]
        O2 --> O3{codeFlashProtected?}
        O3 -->|Yes| O4[unprotect - writeConfig + reset + reconnect]
        O3 -->|No| O5[erase - sectors = size/1024 + 1]
        O4 --> O5
        O5 --> O6[sleep 1s]
        O6 --> O7["program() - send ISP key (30 zeros)"]
        O7 --> O8[program chunk loop - 56 byte chunks]
        O8 --> O9[final empty chunk - terminator]
        O9 --> O10["sleep 500ms (in program())"]
        O10 --> O11["sleep 500ms (in flash()) - DOUBLE!"]
        O11 --> O12["verify() - send ISP key again"]
        O12 --> O13[verify chunk loop]
        O13 --> O14["check resp.ok only ⚠️ MISSING payload[0] check!"]
        O14 --> O15[reset - ispEnd + sleep 2s]
    end

    style W13 fill:#ff6b6b
    style O14 fill:#ff6b6b
    style O11 fill:#ffd93d
```

## Critical Differences Found

### 1. 🔴 Response Status Byte Handling — FUNDAMENTAL DIFFERENCE

**wchisp** (`protocol.rs:316-329`):
```rust
pub(crate) fn from_raw(raw: &[u8]) -> Result<Self> {
    // FIXME: should raw[1] == 0x00 || raw[1] == 0x82?
    if true {  // ← ALWAYS TRUE! Status byte IGNORED!
        let len = raw.pread_with::<u16>(2, scroll::LE)? as usize;
        let remain = &raw[4..];
        if remain.len() == len {
            Ok(Response::Ok(remain.to_vec()))  // ← Always returns Ok
        } else {
            Err(anyhow::anyhow!("Invalid response"))
        }
    } else {
        Ok(Response::Err(raw[1], raw[2..].to_vec()))
    }
}
```

**Our code** (`WCHProtocol.cpp:163`):
```cpp
out.ok = (out.status == 0x00);  // ← We CHECK the status byte
```

**Impact**: 
- wchisp: `resp.is_ok()` is ALWAYS true → actual error detection relies on payload content
- Our code: `resp.ok` checks status byte → may miss payload-based errors

### 2. 🔴 Verify Mismatch Detection — MISSING CHECK

**wchisp** (`flashing.rs:452`):
```rust
fn verify_chunk(&mut self, address: u32, raw: &[u8], key: [u8; 8]) -> Result<()> {
    let xored = raw.iter().enumerate().map(|(i, x)| x ^ key[i % 8]);
    let cmd = Command::verify(address, padding, xored.collect());
    let resp = self.transport.transfer(cmd)?;
    anyhow::ensure!(resp.is_ok(), "verify response failed");  // ← Always true
    anyhow::ensure!(resp.payload()[0] == 0x00, "Verify failed, mismatch");  // ← ACTUAL CHECK
    Ok(())
}
```

**Our code** (`WCHFlasher.cpp:486-492`):
```cpp
auto packet = WCHPacketBuilder::verify(static_cast<uint32_t>(offset), padding, encrypted);
auto raw = doTransfer(packet, "verify");
WCHResponse resp;
if (!WCHResponse::parse(raw, resp) || !resp.ok)  // ← Only checks status byte
    throw WCHFlashError("Verify failed at offset 0x" + std::to_string(offset));
// ❌ MISSING: if (resp.payload.empty() || resp.payload[0] != 0x00) throw ...
```

**Impact**: 
- If device returns `status=0x00, payload[0]=0x01` (mismatch), wchisp detects it, we DON'T
- Verify could be silently passing when flash content is actually wrong
- **This could be the ROOT CAUSE of GET_INFO timeout!**

### 3. 🟡 Double Sleep After Program

**wchisp**: `flash() → sleep(500ms) → verify()`  
**Our code**: `program() → sleep(500ms) → flash() → sleep(500ms) → verify()` = **1000ms total**

Not critical but different from reference.

### 4. 🟡 Program Chunk Timeout

**wchisp**: `transfer_with_wait(cmd, Duration::from_millis(300))` — 300ms per chunk  
**Our code**: Uses global 5000ms USB timeout

Not a correctness issue, just different error detection speed.

### 5. 🟢 Things That Match Perfectly ✓

| Aspect | Status |
|---|---|
| USB transport (no reset, no detach, 1µs delay) | ✓ Match |
| HEX parsing (0x00 gap fill) | ✓ Match |
| Sector boundary fill (0x00) | ✓ Match |
| Erase sector count (size/1024 + 1, min 8) | ✓ Match |
| Program chunk size (56 bytes) | ✓ Match |
| XOR key derivation | ✓ Match |
| XOR encryption (rotating key) | ✓ Match |
| Address starts at 0x0 | ✓ Match |
| ISP key (30 zeros) | ✓ Match |
| Final empty chunk (terminator) | ✓ Match |
| Packet format (all commands) | ✓ Match |
| Post-erase sleep (1s) | ✓ Match |
| Unprotect byte sequence | ✓ Match |
| Reset byte sequence | ✓ Match |
| Full pipeline order | ✓ Match |

## Root Cause Analysis

**Why does GET_INFO timeout after successful flash?**

The most likely explanation:

1. **Verify is silently passing** when it should fail
2. The flash content is actually **corrupted or incomplete**
3. The firmware doesn't execute correctly → no response to GET_INFO

**Evidence**:
- Flash "succeeds" (no errors reported)
- Device re-enumerates (firmware runs at USB level)
- But firmware doesn't respond to commands (firmware logic broken)

**Mechanism**:
- Device returns `status=0x00` (command received OK) but `payload[0]=0x01` (verify mismatch)
- Our code only checks `status=0x00` → thinks verify passed
- wchisp checks `payload[0]==0x00` → would detect the mismatch
- Flash content is wrong → firmware crashes or hangs → GET_INFO times out

**Why would verify fail?**
- Possible flash erase issue (not all sectors erased)
- Possible XOR encryption bug (but we verified it matches wchisp)
- Possible timing issue (flash not ready for verify)
- Most likely: **the data was never written correctly in the first place**, but program command returned OK

## Fix Proposal

### Fix 1: Add Verify Payload Check — CRITICAL

**File**: `wch/WCHFlasher.cpp`  
**Location**: Line ~490 in `verify()` method

```cpp
// Current code:
if (!WCHResponse::parse(raw, resp) || !resp.ok)
    throw WCHFlashError("Verify failed at offset 0x" + std::to_string(offset));

// Add AFTER the above check:
if (resp.payload.empty() || resp.payload[0] != 0x00)
    throw WCHFlashError("Verify MISMATCH at offset 0x" + std::to_string(offset) +
                        " (payload[0]=0x" + 
                        (resp.payload.empty() ? std::string("??") : 
                         ([](uint8_t b) {
                             char buf[3];
                             snprintf(buf, sizeof(buf), "%02X", b);
                             return std::string(buf);
                         })(resp.payload[0])) + ")");
```

**Why**: Matches wchisp's behavior and will detect if verify is actually failing.

### Fix 2: Remove Double Sleep — MINOR

**File**: `wch/WCHFlasher.cpp`  
**Location**: Line ~451 (end of `program()`) and line ~596 (in `flash()`)

```cpp
// Option A: Remove sleep from program() (line 451)
// Delete: std::this_thread::sleep_for(std::chrono::milliseconds(500));

// Option B: Remove sleep from flash() (line 596)
// Delete: std::this_thread::sleep_for(std::chrono::milliseconds(500));
```

**Recommendation**: Keep the sleep in `flash()` (line 596), remove from `program()` (line 451). This matches wchisp where the sleep is between flash and verify in the main flow, not inside flash() itself.

### Fix 3: Add Response Status Byte Logging — DEBUGGING

**File**: `wch/WCHProtocol.cpp` or `wch/WCHFlasher.cpp`

Add logging to see what the device actually returns:

```cpp
// In WCHFlasher::doTransfer(), before returning:
if (raw.size() >= 4) {
    // Log: cmd=raw[0], status=raw[1], len=raw[2..3]
    // This helps debug if status byte is meaningful
}
```

**Why**: Helps understand if the device returns non-zero status bytes that we're currently ignoring or misinterpreting.

### Fix 4: Add Program Response Validation — DEFENSIVE

**File**: `wch/WCHFlasher.cpp`  
**Location**: Line ~428 in `program()` method

```cpp
// After checking resp.ok, also check payload if present:
if (!resp.payload.empty() && resp.payload[0] != 0x00) {
    // Device reported an error in payload even though status was OK
    throw WCHFlashError("Program error at offset 0x" + std::to_string(offset) +
                        " (payload[0]=0x" + ... + ")");
}
```

**Why**: Defensive check. If device returns errors in payload for program commands too, we'll catch them.

## Testing Plan

1. **Apply Fix 1** (verify payload check)
2. **Re-flash the firmware** and observe:
   - If verify now FAILS → we've found the root cause! Flash content is wrong.
   - If verify still PASSES → the issue is elsewhere (timing, config, etc.)
3. **If verify fails**, investigate why:
   - Try increasing erase sleep (1s → 2s)
   - Try adding sleep after each program chunk
   - Check if specific offsets fail (pattern analysis)
4. **If verify passes**, investigate other causes:
   - Config registers (USER byte, SRAM_CODE_MODE)
   - EEPROM data (was it corrupted by earlier dataErase?)
   - USB CDC ACM initialization (DTR/RTS signals?)

## Expected Outcome

After applying **Fix 1**, one of two things will happen:

**Scenario A**: Verify now detects mismatches → we know the flash content is wrong → can investigate why program isn't writing correctly.

**Scenario B**: Verify still passes → the flash content is correct → the issue is in firmware initialization or USB CDC ACM layer → need to investigate DTR/RTS signals, firmware boot sequence, or config registers.

**Most likely**: Scenario A. The missing payload check is the most significant protocol difference, and silent verify failures would perfectly explain the symptoms.

## References

- wchisp source: `/home/bot/project/wchisp/`
  - `src/flashing.rs:452` — verify payload check
  - `src/protocol.rs:316-329` — response status byte handling
- Our code: `/home/bot/project/Openterface_QT/wch/`
  - `WCHFlasher.cpp:490` — missing verify payload check
  - `WCHProtocol.cpp:163` — status byte check
- Debug doc: `/home/bot/project/Openterface_QT/docs/wch-flash-debug.md`
