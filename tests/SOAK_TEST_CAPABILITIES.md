# Enhanced Soak Test Capabilities

## Overview

The soak test has been enhanced to detect a comprehensive range of issues that can occur during long-running operation of the Openterface QT application.

## Detection Capabilities

### 1. **Memory Leaks** ✅
- **What:** Tracks RSS memory usage over time
- **How:** Compares average of first 5 samples vs last 5 samples
- **Threshold:** Alerts when memory grows by >50MB
- **Why:** Catches memory allocation without proper cleanup

### 2. **Focus Event Loops** ✅
- **What:** Detects X11 keyboard grab/ungrab infinite loops
- **How:** Monitors logs for repeated "grabbing keyboard" / "ungrabbing keyboard" patterns
- **Threshold:** Alerts when >10 cycles in last 100 lines
- **Why:** Catches regression of the X11 focus bug we fixed

### 3. **File Descriptor Leaks** ✅ (NEW)
- **What:** Tracks open file descriptor count
- **How:** Reads `/proc/PID/fd` count over time
- **Threshold:** Alerts when FD count grows by >20
- **Why:** Catches unclosed files, sockets, pipes, devices
- **Example issues:**
  - HID device opened but not closed
  - Log files kept open
  - Network sockets not released
  - X11 connections not freed

### 4. **Thread Leaks** ✅ (NEW)
- **What:** Tracks thread count over time
- **How:** Reads `/proc/PID/status` Threads field
- **Threshold:** Alerts when thread count grows by >10
- **Why:** Catches threads created but not properly joined/destroyed
- **Example issues:**
  - QTimer creating threads without cleanup
  - QThread instances not properly terminated
  - Worker threads accumulating

### 5. **Log File Explosion** ✅ (NEW)
- **What:** Detects rapid log file growth
- **How:** Monitors log file size between checks
- **Threshold:** Alerts when log grows by >10MB between checks
- **Why:** Catches infinite logging loops or excessive debug output
- **Example issues:**
  - Debug logging left enabled
  - Error messages in tight loop
  - Polling without throttling

### 6. **GStreamer Pipeline Issues** ✅ (NEW)
- **What:** Detects GStreamer errors and warnings
- **How:** Scans logs for GStreamer-related error messages
- **Threshold:** Alerts when >5 GST errors in last 100 lines
- **Why:** Catches video pipeline problems
- **Example issues:**
  - Pipeline state changes failing
  - Element creation errors
  - Buffer allocation failures
  - xvimagesink warnings

### 7. **Device Connection Stability** ✅ (NEW)
- **What:** Detects repeated device open/close cycles
- **How:** Monitors logs for "Opening device" / "Device opened" messages
- **Threshold:** Alerts when >10 device opens in last 100 lines
- **Why:** Catches device connection instability
- **Example issues:**
  - HID device disconnecting/reconnecting
  - Serial port flapping
  - USB device enumeration loops

### 8. **Crash Detection** ✅
- **What:** Detects application crashes and unexpected exits
- **How:** Checks if process is still running
- **Action:** Captures last 100 log lines and screenshot on crash
- **Why:** Catches segfaults, aborts, unhandled exceptions

### 9. **CPU Usage Monitoring** ✅ (NEW)
- **What:** Tracks CPU time consumption
- **How:** Reads `/proc/PID/stat` CPU times
- **Why:** Baseline for detecting CPU spikes
- **Note:** Full CPU % calculation requires time-delta sampling

### 10. **Error Tracking** ✅
- **What:** Counts new errors in logs
- **How:** Tracks line count and scans for error keywords
- **Keywords:** error, Error, ERROR, fatal, Fatal, FATAL, crash, segfault, SIGSEGV, SIGABRT
- **Why:** Catches runtime errors and exceptions

## Test Configuration

### Parameters
```bash
./tests/gui_soak_test.sh [OPTIONS] [DURATION_MINUTES] [CHECK_INTERVAL_SECONDS]

# Examples:
./tests/gui_soak_test.sh --native 60 30    # 60 minutes, check every 30s
./tests/gui_soak_test.sh --native 5 10     # Quick 5-minute test
./tests/gui_soak_test.sh --native 180 60   # 3-hour soak test
```

### Check Interval Recommendations
- **Quick test (5-10 min):** 10-15s interval
- **Standard test (30-60 min):** 20-30s interval
- **Long soak (2-24 hours):** 60-120s interval

## Test Results Interpretation

### Status Codes
- **PASS:** All checks passed, no issues detected
- **WARNING:** Minor issues detected (warnings > 10)
- **FAIL:** Critical issues detected (crashes, leaks, loops)

### Report File
Generated at: `tests/soak_test_logs/soak_test_report_YYYYMMDD_HHMMSS.md`

Contains:
- Test configuration
- All metrics and counters
- Resource usage statistics
- Key findings checklist
- Log file locations
- Screenshot locations

### Screenshots
Taken every 5 checks and stored in: `tests/soak_test_screenshots/`

Useful for:
- Visual verification of UI state
- Detecting rendering issues
- Documenting crashes

## Common Issues & What They Indicate

### Memory Leak Detected
**Possible causes:**
- GStreamer buffers not freed
- Qt objects created without parent
- X11 resources not released
- C++ new without delete

**Investigation:**
```bash
# Monitor memory in real-time
watch -n 1 'grep VmRSS /proc/PID/status'
```

### FD Leak Detected
**Possible causes:**
- File opened but not closed
- Socket not closed
- Pipe ends not closed
- HID device not closed

**Investigation:**
```bash
# List open files
ls -l /proc/PID/fd
lsof -p PID
```

### Thread Leak Detected
**Possible causes:**
- QThread not properly terminated
- Worker threads accumulating
- Timer threads not cleaned up

**Investigation:**
```bash
# List threads
ls /proc/PID/task
cat /proc/PID/status | grep Threads
```

### Focus Loop Detected
**Possible causes:**
- X11 grab/ungrab cycle (regression)
- Window manager interaction issues
- Focus tracking logic error

**Investigation:**
```bash
# Check logs for loop pattern
grep -E "grabbing|ungrabbing" tests/soak_test_logs/app_output.log
```

### Log Explosion Detected
**Possible causes:**
- Debug logging enabled in production
- Error in tight loop
- Polling without throttling

**Investigation:**
```bash
# Check log growth rate
wc -l tests/soak_test_logs/app_output.log
tail -100 tests/soak_test_logs/app_output.log
```

### GStreamer Issues Detected
**Possible causes:**
- Pipeline state management error
- Element creation failure
- Buffer allocation issues
- Video sink problems

**Investigation:**
```bash
# Check GStreamer logs
grep -i gstreamer tests/soak_test_logs/app_output.log
grep -i pipeline tests/soak_test_logs/app_output.log
```

### Device Instability Detected
**Possible causes:**
- USB connection issues
- HID device firmware bug
- Serial port driver problem
- Power management issues

**Investigation:**
```bash
# Check device logs
grep -i "opening device\|device opened" tests/soak_test_logs/app_output.log
dmesg | tail -50
```

## Integration with CI/CD

### Recommended CI Configuration
```yaml
# .github/workflows/soak-test.yml
name: Soak Test

on:
  schedule:
    - cron: '0 2 * * *'  # Daily at 2 AM
  workflow_dispatch:

jobs:
  soak-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Build
        run: |
          mkdir build && cd build
          cmake ..
          make -j$(nproc)
      
      - name: Run Soak Test
        run: |
          # Start virtual display
          Xvfb :99 -screen 0 1280x720x24 &
          export DISPLAY=:99
          
          # Run 30-minute soak test
          ./tests/gui_soak_test.sh --native --no-xvfb 30 30
      
      - name: Upload Results
        if: always()
        uses: actions/upload-artifact@v3
        with:
          name: soak-test-results
          path: |
            tests/soak_test_logs/
            tests/soak_test_screenshots/
```

## Performance Impact

### Resource Usage
- **CPU:** <1% (checks are lightweight)
- **Memory:** ~10MB (for tracking arrays)
- **Disk:** ~1MB per hour (logs + screenshots)

### Check Frequency Trade-offs
- **More frequent (5-10s):** Better resolution, more overhead
- **Less frequent (60-120s):** Lower overhead, may miss short-lived issues
- **Recommended:** 20-30s for most use cases

## Future Enhancements

### Potential Additions
1. **Network connection monitoring** - Track open sockets
2. **X11 resource tracking** - Windows, pixmaps, GCs
3. **Qt signal/slot leak detection** - Connection count tracking
4. **Disk I/O monitoring** - Read/write rates
5. **GPU memory tracking** - For video rendering
6. **Response time monitoring** - UI event latency
7. **Power consumption** - For mobile/laptop testing
8. **Temperature monitoring** - Hardware stress testing

### Implementation Notes
- All checks should be non-intrusive (read-only)
- Use `/proc` filesystem when possible (Linux)
- Provide hooks for custom checks
- Support plugin architecture for extensibility

## Conclusion

The enhanced soak test provides comprehensive coverage for detecting stability issues in long-running applications. It combines passive monitoring with active checks to catch a wide range of problems from memory leaks to device instability.

**Key Benefits:**
- Automated issue detection
- Comprehensive coverage
- Low overhead
- Easy to integrate into CI/CD
- Detailed reporting for debugging

**Best Practices:**
- Run soak tests regularly (daily/weekly)
- Use appropriate check intervals
- Review reports and screenshots
- Investigate all warnings and failures
- Track metrics over time for trend analysis
