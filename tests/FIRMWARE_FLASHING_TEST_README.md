# Firmware Flashing Test Suite

## Overview

This test suite provides systematic testing for firmware flashing and serial port reconnection scenarios, specifically designed to diagnose and prevent issues on Linux where:
1. Errors occur during firmware flashing of the CH32V208 control chip
2. Serial port becomes unusable after flashing completes

## Test Suite: test_firmware_flashing

### What It Tests

The test suite simulates the complete firmware flashing workflow:

```
1. Device in ISP mode (flashing mode) → VID:PID = 1A86:55E0
2. Flash process starts → USB disconnect (device reset)
3. Device re-enumerates → VID:PID = 1A86:FE0C
4. Serial port auto-reconnects
5. Firmware verification using GET_INFO command (0x01)
```

### Test Cases

#### Test 1: Complete Flash Workflow
Simulates the normal flash process from ISP mode to normal operation, including USB disconnect/reconnect and serial port recovery.

**What it validates:**
- Device detection in ISP mode
- USB disconnect handling during flash reset
- Device re-enumeration with new VID/PID
- Serial port auto-reconnection
- No crashes during the complete cycle

#### Test 2: Unstable USB During Flash
Tests how the system handles unstable USB connections during the flashing process.

**What it validates:**
- Rapid plug/unplug cycles (10 iterations)
- System stability under poor connection conditions
- Graceful error handling

#### Test 3: Delayed Re-enumeration
Tests various USB re-enumeration delays after flash (100ms to 3000ms).

**What it validates:**
- System tolerance for slow device boot times
- Proper timeout handling
- Successful reconnection after extended delays

#### Test 4: GET_INFO Command Structure
Validates the GET_INFO command (0x01) building and response parsing.

**What it validates:**
- Command packet structure (57 AB 00 01 00)
- Response parsing (version, target connection status, indicators)
- Error handling for malformed responses
- Checksum validation

**Command format:**
```
Request:  57 AB 00 01 00
Response: 57 AB 00 81 06 [version] [target_connected] [indicators] [reserved...] [checksum]
```

#### Test 5: Checksum Validation
Tests protocol packet checksum calculation and validation.

**What it validates:**
- Correct checksum calculation
- Detection of corrupted packets

#### Test 6: Multiple Flash Cycles (Stress Test)
Performs 50 consecutive flash cycles to test long-term stability.

**What it validates:**
- No memory leaks
- No resource exhaustion
- Consistent behavior across many cycles

#### Test 7: Port Chain Tracking
Verifies that the serial port chain is correctly maintained through flash cycles.

**What it validates:**
- Port chain consistency before and after flash
- Correct device identification

#### Test 8: Multiple Devices During Flash
Tests flashing one device while another remains connected.

**What it validates:**
- Correct device identification during multi-device scenarios
- Isolation between devices
- No cross-device interference

#### Test 9: VID/PID Transition
Validates correct handling of the VID/PID change from ISP mode to normal mode.

**What it validates:**
- ISP mode detection (1A86:55E0)
- Normal mode detection (1A86:FE0C)
- Smooth transition between modes

#### Test 10: Integration Test
Complete integration test including serial communication simulation after flash.

**What it validates:**
- End-to-end workflow
- GET_INFO command building
- Response parsing and validation
- Firmware version extraction

## Building the Tests

### Prerequisites

- Qt 6.x with SerialPort module
- CMake 3.16 or later
- C++17 compatible compiler

### Build Commands

```bash
cd tests
mkdir -p build
cd build
cmake ..
make test_firmware_flashing
```

## Running the Tests

### Run All Tests

```bash
./test_firmware_flashing
```

### Run Specific Test

```bash
./test_firmware_flashing testCompleteFlashWorkflow
```

### Run with Verbose Output

```bash
./test_firmware_flashing -v2
```

### Run with CTest

```bash
cd build
ctest -R FirmwareFlashing -V
```

## Expected Output

A successful test run should show:

```
********* Start testing of TestFirmwareFlashing *********
Config: Using QtTest library 6.x.x
PASS   : TestFirmwareFlashing::initTestCase()
PASS   : TestFirmwareFlashing::testCompleteFlashWorkflow()
PASS   : TestFirmwareFlashing::testUnstableUSBDuringFlash()
PASS   : TestFirmwareFlashing::testDelayedReenumeration()
PASS   : TestFirmwareFlashing::testGetInfoCommandStructure()
PASS   : TestFirmwareFlashing::testChecksumValidation()
PASS   : TestFirmwareFlashing::testMultipleFlashCycles()
PASS   : TestFirmwareFlashing::testPortChainTracking()
PASS   : TestFirmwareFlashing::testMultipleDevicesDuringFlash()
PASS   : TestFirmwareFlashing::testVIDPIDTransition()
PASS   : TestFirmwareFlashing::testFlashWithSerialCommunication()
PASS   : TestFirmwareFlashing::cleanupTestCase()
Totals: 12 passed, 0 failed, 0 skipped, 0 blacklisted, 0ms
********* Finished testing of TestFirmwareFlashing *********
```

## Diagnosing Real-World Issues

### If Tests Pass But Real Flash Fails

The tests simulate the flashing process at the hotplug monitoring level. If tests pass but real flashing fails, check:

1. **udev Rules** (Linux-specific)
   ```bash
   # Verify WCH flash rules are installed
   ls -l /etc/udev/rules.d/51-opf-wchflash.rules
   
   # Install if missing
   sudo tee /etc/udev/rules.d/51-opf-wchflash.rules <<'EOF'
   SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55e0", TAG+="uaccess", MODE="0666"
   SUBSYSTEM=="usb", ATTRS{idVendor}=="4348", ATTRS{idProduct}=="55e0", TAG+="uaccess", MODE="0666"
   SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="fe0c", TAG+="uaccess", MODE="0666"
   EOF
   
   sudo udevadm control --reload-rules
   sudo udevadm trigger
   ```

2. **Serial Port Permissions**
   ```bash
   # Check if user is in dialout group
   groups | grep dialout
   
   # Add user to dialout group if needed
   sudo usermod -a -G dialout $USER
   # Then log out and log back in
   ```

3. **CH340 Driver (for CH9329 chips)**
   ```bash
   # Check if CH340 module is loaded
   lsmod | grep ch341
   
   # Load if needed
   sudo modprobe ch341
   ```

4. **USB Power Management**
   ```bash
   # Disable USB autosuspend
   echo -1 | sudo tee /sys/module/usbcore/parameters/autosuspend
   ```

### If Tests Fail

Check these common issues:

1. **Qt SerialPort Module Not Installed**
   ```bash
   # Ubuntu/Debian
   sudo apt install libqt6serialport6-dev
   
   # Fedora
   sudo dnf install qt6-qtserialport-devel
   ```

2. **Build Errors**
   - Ensure you're using Qt 6.x (not Qt 5.x)
   - Check CMake version is 3.16+

3. **Test Crashes**
   - Run with `-v2` for detailed output
   - Check for memory leaks with valgrind:
     ```bash
     valgrind --leak-check=full ./test_firmware_flashing
     ```

## Protocol Reference

### GET_INFO Command (0x01)

**Request packet:**
```
Byte 0: 0x57  (Header byte 1)
Byte 1: 0xAB  (Header byte 2)
Byte 2: 0x00  (Address)
Byte 3: 0x01  (Command code: GET_INFO)
Byte 4: 0x00  (Length: 0 bytes)
```

**Response packet:**
```
Byte 0:  0x57  (Header byte 1)
Byte 1:  0xAB  (Header byte 2)
Byte 2:  0x00  (Address)
Byte 3:  0x81  (Response code: GET_INFO | 0x80)
Byte 4:  0x06  (Length: 6 bytes)
Byte 5:  [Version number]
Byte 6:  [Target connected: 0x00 or 0x01]
Byte 7:  [Indicators (NumLock, CapsLock, ScrollLock)]
Byte 8:  0x00  (Reserved)
Byte 9:  0x00  (Reserved)
Byte 10: 0x00  (Reserved)
Byte 11: [Checksum]
```

**Checksum calculation:**
```
Checksum = (sum of all bytes from byte 0 to byte 10) & 0xFF
```

## Troubleshooting Linux Serial Port Issues

### Symptom: Serial Port Not Detected After Flash

**Check 1: Is the device enumerated?**
```bash
lsusb | grep -i "1a86"
```
Expected: Should see `1a86:fe0c` (normal mode)

**Check 2: Is the serial port created?**
```bash
ls -l /dev/ttyUSB* /dev/ttyACM*
```
Expected: Should see a device file

**Check 3: Are permissions correct?**
```bash
ls -l /dev/ttyUSB0
```
Expected: Should show your user or `dialout` group with rw permissions

**Check 4: Check kernel messages**
```bash
dmesg | tail -20
```
Look for USB device connection messages and any errors.

### Symptom: Flash Tool Reports Errors

**Check 1: Device in ISP mode?**
```bash
lsusb | grep -E "55e0|55E0"
```
Expected: Should see `1a86:55e0` or `4348:55e0` when in ISP mode

**Check 2: USB permissions for ISP mode?**
```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

**Check 3: No other application using the device?**
```bash
lsof /dev/ttyUSB*
```
Expected: Should be empty (no processes using the port)

## Integration with CI/CD

Add to your CI pipeline:

```yaml
test:
  script:
    - cd tests/build
    - cmake ..
    - make test_firmware_flashing
    - ./test_firmware_flashing
  artifacts:
    reports:
      junit: tests/build/test_firmware_flashing.xml
```

## Future Enhancements

Planned test additions:
- [ ] Real serial port communication test (requires hardware)
- [ ] Flash timeout scenarios
- [ ] Power loss during flash recovery
- [ ] Multi-platform compatibility (Windows, macOS)
- [ ] Performance benchmarks for reconnection timing

## Related Documentation

- [CH32 Firmware Flashing Guide](../../docs/ch32_firmware_flashing.md)
- [Serial Factory Reset Refactor](../../docs/serial_factory_reset_refactor.md)
- [Hotplug Test Framework](./README.md)

## Support

If you encounter issues:
1. Run tests with verbose output: `./test_firmware_flashing -v2`
2. Check application logs in `~/.local/share/openterface/logs/`
3. Open an issue on GitHub with test output and system information

## License

Part of the Openterface Mini-KVM QT project. See main project license for details.
