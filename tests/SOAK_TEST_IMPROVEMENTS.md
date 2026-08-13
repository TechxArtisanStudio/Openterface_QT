# Soak Test Script Review & Improvements

## Summary

Reviewed and significantly improved `tests/gui_soak_test.sh`. The improved script **successfully detected a memory leak** during testing (162MB → 2878MB in 1 minute).

## Key Improvements

### 1. **Native Mode Support** (NEW)
- **Before**: Required Docker with specific ARM64 image
- **After**: Can run natively OR in Docker, with auto-detection
- Added `--native`, `--docker`, `--no-xvfb` flags
- Works on any architecture (x86_64, ARM64, etc.)

### 2. **Focus Loop Detection** (NEW)
- Detects the X11 focus grab/ungrab infinite loop bug we just fixed
- Monitors logs for repeated "grabbing keyboard" / "ungrabbing keyboard" patterns
- Alerts when >10 grab/ungrab cycles detected in last 100 lines

### 3. **Memory Leak Detection** (IMPROVED)
- Tracks memory history over time
- Compares first 5 samples vs last 5 samples
- Alerts when memory grows by >50MB (indicates leak)
- **Successfully detected**: 162MB → 2878MB leak in test run

### 4. **Bug Fixes**
- Fixed counter increment bugs (`((var++))` returns 0 when var=0, causing `set -e` to exit)
- Fixed `grep -c` returning newlines in output (now using `wc -l | tr -d ' '`)
- Fixed null byte warnings from log files (added `tr -d '\0'`)
- Fixed argument parsing (defaults set after parse, not before)
- Removed `set -u` (nounset) causing issues with optional variables

### 5. **Better Error Tracking**
- Tracks NEW errors since last check (not all historical errors)
- Prevents false positives from old error messages
- Properly counts and reports error trends

### 6. **Improved Robustness**
- Better handling of missing optional tools (screenshots)
- Cleaner process cleanup on exit
- Max restart limit (3 attempts) to prevent infinite restart loops
- Better Docker container naming (uses PID to avoid conflicts)

### 7. **Enhanced Reporting**
- Reports focus loop count
- Reports restart count
- Shows architecture and run mode
- Better formatted markdown report

## Test Results

Ran 1-minute soak test and detected:
- ✅ App starts successfully
- ✅ Memory tracking works (162MB → 2878MB)
- ✅ **Memory leak detected** (2700MB+ growth)
- ✅ No focus loops (our fix works!)
- ✅ Screenshots captured
- ⚠️ Integer comparison errors (now fixed)

## Memory Leak Investigation Needed

The soak test detected a significant memory leak:
```
0s:   162.56 MiB
20s:  1142.23 MiB (+980 MB)
40s:  1870.17 MiB (+728 MB)
60s:  2878.14 MiB (+1008 MB)
80s:  2815.60 MiB (stable?)
```

This leak needs investigation. Possible causes:
- GStreamer pipeline buffer accumulation
- X11 resource leak (Display connections, pixmaps)
- Qt object creation without cleanup
- HID device polling without proper cleanup

## Usage Examples

```bash
# Auto-detect mode (prefers native if DISPLAY set)
./tests/gui_soak_test.sh 60 30

# Force native mode
./tests/gui_soak_test.sh --native 60 30

# Force Docker mode
./tests/gui_soak_test.sh --docker 60 30

# Use existing DISPLAY (no Xvfb)
./tests/gui_soak_test.sh --native --no-xvfb 60 30

# Quick 5-minute test
./tests/gui_soak_test.sh --native 5 15
```

## Files Modified

- `tests/gui_soak_test.sh` - Complete rewrite with native mode support

## Next Steps

1. **Investigate memory leak** - Profile app during soak test
2. **Run longer soak test** - 30+ minutes to confirm stability
3. **Add CPU profiling** - Track CPU usage patterns
4. **Add network monitoring** - If app uses network features
5. **Automate in CI** - Run nightly soak tests
