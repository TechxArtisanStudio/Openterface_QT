# Soak Test Enhancement Proposals

## Current Coverage ✅

The existing soak test already monitors:
- ✅ Memory leaks (RSS growth tracking)
- ✅ File descriptor leaks
- ✅ Thread leaks
- ✅ CPU usage monitoring
- ✅ Focus event loops (X11 grab/ungrab)
- ✅ Log file explosion
- ✅ GStreamer pipeline health
- ✅ Device connection stability
- ✅ Error tracking in logs
- ✅ Crash detection
- ✅ Visual verification (screenshots)

---

## Proposed Enhancements

### 🎥 1. Video Capture Quality & Performance

**Priority: HIGH** - Core functionality

#### What to Monitor
- **Frame rate consistency** - Detect dropped frames or stuttering
- **Frame latency** - Time between capture and display
- **Video artifact detection** - Corruption, tearing, color issues
- **Resolution changes** - Unexpected format switches
- **Buffer underruns** - Video stalls or freezes

#### Implementation Ideas
```bash
# Monitor frame rate from logs
check_video_performance() {
    local log_file="${LOG_DIR}/app_output.log"
    
    # Count frame updates in recent logs
    local frame_count=$(tail -100 "$log_file" | grep -c "frameReady\|FrameAvailable")
    
    # Calculate FPS over time window
    local fps=$(echo "scale=2; $frame_count / $time_window" | bc)
    
    # Alert if FPS drops below threshold
    if [ "$(echo "$fps < 25" | bc)" -eq 1 ]; then
        print_warning "VIDEO PERFORMANCE: FPS dropped to $fps"
    fi
}
```

#### Why It Matters
- Catches video pipeline degradation before it becomes unusable
- Detects backend-specific performance issues
- Validates real-time performance requirements

---

### 🔌 2. USB/Device Communication Stability

**Priority: HIGH** - Hardware interaction

#### What to Monitor
- **USB disconnect/reconnect cycles** - Device flapping
- **HID communication errors** - Failed reads/writes
- **Serial port timeouts** - Communication delays
- **Device enumeration loops** - Repeated discovery attempts
- **USB bandwidth saturation** - Transfer rate degradation

#### Implementation Ideas
```bash
check_usb_stability() {
    local log_file="${LOG_DIR}/app_output.log"
    
    # Check for USB errors
    local usb_errors=$(tail -100 "$log_file" | grep -c "USB error\|usb_submit_urb failed\|LIBUSB_ERROR")
    
    # Check for device reconnects
    local reconnects=$(tail -100 "$log_file" | grep -c "Device disconnected\|Device reconnected")
    
    # Check for enumeration loops
    local enum_loops=$(tail -100 "$log_file" | grep -c "Device discovered\|Found.*device")
    
    if [ "$usb_errors" -gt 5 ] || [ "$reconnects" -gt 3 ]; then
        print_warning "USB INSTABILITY: $usb_errors errors, $reconnects reconnects"
    fi
}
```

#### Why It Matters
- Catches hardware communication issues early
- Detects USB bandwidth or power problems
- Validates device driver stability

---

### 🖥️ 3. X11/Wayland Resource Leaks

**Priority: MEDIUM** - Platform-specific

#### What to Monitor
- **X11 window/pixmap leaks** - Using `xrestop` or `xwininfo`
- **X11 connection count** - Multiple connections indicate leaks
- **Wayland resource usage** - If running on Wayland
- **Graphics context leaks** - OpenGL/Vulkan contexts
- **Event queue buildup** - Unprocessed events

#### Implementation Ideas
```bash
check_x11_resources() {
    local pid=$1
    
    # Check X11 connection count
    local x11_connections=$(ls -1 /proc/$pid/fd 2>/dev/null | xargs ls -l 2>/dev/null | grep -c "X11-unix")
    
    # Check for X11 resource usage (if xrestop available)
    if command -v xrestop &>/dev/null; then
        local x11_mem=$(xrestop -m 1 | grep -A1 "$pid" | tail -1 | awk '{print $2}')
        # Alert if X11 memory usage is excessive
    fi
    
    if [ "$x11_connections" -gt 5 ]; then
        print_warning "X11 LEAK: $x11_connections X11 connections open"
    fi
}
```

#### Why It Matters
- Catches platform-specific resource leaks
- Prevents X server instability
- Validates proper cleanup on window close

---

### 📡 4. Network/MCP Server Stability

**Priority: MEDIUM** - For remote operation

#### What to Monitor
- **TCP connection count** - Socket leaks
- **MCP request/response times** - Latency degradation
- **WebSocket connection stability** - If used
- **HTTP server responsiveness** - Health check endpoints
- **Network buffer accumulation** - Unread data

#### Implementation Ideas
```bash
check_network_health() {
    local pid=$1
    
    # Count open network connections
    local tcp_connections=$(ss -tnp 2>/dev/null | grep "pid=$pid" | wc -l)
    
    # Check for connection states
    local established=$(ss -tnp 2>/dev/null | grep "pid=$pid" | grep -c "ESTAB")
    local time_wait=$(ss -tnp 2>/dev/null | grep "pid=$pid" | grep -c "TIME-WAIT")
    
    # Alert if too many connections in TIME-WAIT (indicates leak)
    if [ "$time_wait" -gt 20 ]; then
        print_warning "NETWORK LEAK: $time_wait connections in TIME-WAIT"
    fi
    
    # Test MCP endpoint if running
    if curl -s --max-time 2 http://localhost:8080/health >/dev/null 2>&1; then
        print_success "MCP server responsive"
    else
        print_warning "MCP server not responding"
    fi
}
```

#### Why It Matters
- Catches network socket leaks
- Validates remote operation capability
- Detects MCP server degradation

---

### ⚡ 5. Qt Event Loop Responsiveness

**Priority: HIGH** - UI responsiveness

#### What to Monitor
- **Event queue depth** - Backlog of unprocessed events
- **Main thread blocking** - Long operations on UI thread
- **Timer accuracy** - QTimer drift or missed ticks
- **Signal/slot connection count** - Accumulation indicates leaks
- **Paint event frequency** - UI update rate

#### Implementation Ideas
```bash
check_qt_responsiveness() {
    local log_file="${LOG_DIR}/app_output.log"
    
    # Check for event loop warnings
    local event_warnings=$(tail -100 "$log_file" | grep -c "Event loop\|QEventLoop\|Blocking")
    
    # Check for timer drift (if logged)
    local timer_issues=$(tail -100 "$log_file" | grep -c "Timer.*delayed\|Timer.*missed")
    
    # Check for signal/slot connection accumulation
    local connection_count=$(tail -100 "$log_file" | grep -c "connect(")
    
    if [ "$event_warnings" -gt 5 ]; then
        print_warning "QT EVENT LOOP: $event_warnings warnings detected"
    fi
    
    # Alert if connection count is growing rapidly
    if [ "$connection_count" -gt 50 ]; then
        print_warning "QT CONNECTIONS: $connection_count connections in recent logs"
    fi
}
```

#### Why It Matters
- Catches UI freeze issues
- Detects main thread blocking
- Validates Qt best practices

---

### 💾 6. Disk I/O Monitoring

**Priority: MEDIUM** - Performance impact

#### What to Monitor
- **Write rate** - Excessive logging or file writes
- **Read rate** - Cache misses or repeated reads
- **I/O wait time** - Blocking on disk operations
- **Open file count** - File handle leaks
- **Disk space usage** - Log file growth

#### Implementation Ideas
```bash
check_disk_io() {
    local pid=$1
    
    # Read I/O stats from /proc
    local io_stats=$(cat /proc/$pid/io 2>/dev/null)
    local read_bytes=$(echo "$io_stats" | grep "read_bytes" | awk '{print $2}')
    local write_bytes=$(echo "$io_stats" | grep "write_bytes" | awk '{print $2}')
    
    # Calculate rates (requires previous sample)
    # Alert if write rate is excessive (>10MB/s sustained)
    
    # Check open file count
    local open_files=$(ls -1 /proc/$pid/fd 2>/dev/null | wc -l)
    
    if [ "$open_files" -gt 200 ]; then
        print_warning "DISK I/O: $open_files files open"
    fi
}
```

#### Why It Matters
- Detects excessive logging
- Catches file handle leaks
- Identifies performance bottlenecks

---

### 🎨 7. GPU/Graphics Memory

**Priority: LOW** - System-dependent

#### What to Monitor
- **GPU memory usage** - Using `nvidia-smi` or similar
- **Texture count** - Accumulation indicates leaks
- **OpenGL context count** - Multiple contexts
- **VSync status** - Dropped frames
- **Shader compilation errors** - Graphics pipeline issues

#### Implementation Ideas
```bash
check_gpu_memory() {
    # NVIDIA GPUs
    if command -v nvidia-smi &>/dev/null; then
        local gpu_mem=$(nvidia-smi --query-compute-apps=pid,used_memory --format=csv,noheader | grep "$APP_PID" | awk '{print $2}')
        
        if [ -n "$gpu_mem" ] && [ "$gpu_mem" -gt 500 ]; then
            print_warning "GPU MEMORY: $gpu_mem MB used"
        fi
    fi
    
    # Intel/AMD GPUs (more complex, may need vendor tools)
}
```

#### Why It Matters
- Catches graphics memory leaks
- Validates hardware acceleration
- Prevents system-wide GPU issues

---

### 🔊 8. Audio Device Stability

**Priority: LOW** - If audio features used

#### What to Monitor
- **Audio device open/close cycles** - Repeated initialization
- **Buffer underruns** - Audio glitches
- **Sample rate changes** - Unexpected format switches
- **ALSA/PulseAudio errors** - Audio subsystem issues
- **Audio latency** - Delay in audio processing

#### Implementation Ideas
```bash
check_audio_stability() {
    local log_file="${LOG_DIR}/app_output.log"
    
    # Check for audio errors
    local audio_errors=$(tail -100 "$log_file" | grep -c "ALSA.*error\|PulseAudio.*error\|Audio.*failed")
    
    # Check for device reinitialization
    local audio_reinit=$(tail -100 "$log_file" | grep -c "Opening audio\|Audio device opened")
    
    if [ "$audio_errors" -gt 5 ] || [ "$audio_reinit" -gt 10 ]; then
        print_warning "AUDIO INSTABILITY: $audio_errors errors, $audio_reinit reinitializations"
    fi
}
```

#### Why It Matters
- Catches audio subsystem issues
- Validates audio capture/playback stability
- Detects driver compatibility problems

---

### 🎯 9. Input Event Processing

**Priority: MEDIUM** - Core KVM functionality

#### What to Monitor
- **Keyboard event latency** - Time from input to action
- **Mouse event accuracy** - Coordinate mapping
- **HID report parsing errors** - Malformed reports
- **Key state tracking** - Stuck keys or missed releases
- **Input queue buildup** - Unprocessed events

#### Implementation Ideas
```bash
check_input_processing() {
    local log_file="${LOG_DIR}/app_output.log"
    
    # Check for HID parsing errors
    local hid_errors=$(tail -100 "$log_file" | grep -c "HID.*error\|Failed to parse\|Invalid report")
    
    # Check for input latency warnings
    local latency_warnings=$(tail -100 "$log_file" | grep -c "Input latency\|Event delay")
    
    # Check for stuck key detection
    local stuck_keys=$(tail -100 "$log_file" | grep -c "Stuck key\|Key not released")
    
    if [ "$hid_errors" -gt 5 ] || [ "$stuck_keys" -gt 0 ]; then
        print_warning "INPUT ISSUES: $hid_errors HID errors, $stuck_keys stuck keys"
    fi
}
```

#### Why It Matters
- Validates core KVM functionality
- Catches input processing bugs
- Ensures low-latency operation

---

### 📊 10. System Resource Contention

**Priority: MEDIUM** - Multi-process environments

#### What to Monitor
- **CPU throttling** - Thermal or power limits
- **Memory pressure** - System-wide swap usage
- **Disk contention** - I/O wait across system
- **Network saturation** - Bandwidth limits
- **Priority inversion** - Real-time scheduling issues

#### Implementation Ideas
```bash
check_system_resources() {
    # System-wide memory pressure
    local swap_used=$(free | grep Swap | awk '{print $3}')
    local swap_total=$(free | grep Swap | awk '{print $2}')
    local swap_pct=$(echo "scale=2; $swap_used * 100 / $swap_total" | bc)
    
    if [ "$(echo "$swap_pct > 50" | bc)" -eq 1 ]; then
        print_warning "SYSTEM MEMORY: ${swap_pct}% swap used"
    fi
    
    # CPU load average
    local load_avg=$(uptime | awk -F'load average:' '{print $2}' | awk -F',' '{print $1}' | tr -d ' ')
    local cpu_count=$(nproc)
    
    if [ "$(echo "$load_avg > $cpu_count * 2" | bc)" -eq 1 ]; then
        print_warning "SYSTEM CPU: Load average $load_avg (CPUs: $cpu_count)"
    fi
}
```

#### Why It Matters
- Detects system-wide performance issues
- Validates resource isolation
- Prevents false positives from system problems

---

## Implementation Priority

### Phase 1: Critical (Week 1-2)
1. **Video frame rate monitoring** - Core functionality validation
2. **USB/device communication stability** - Hardware interaction
3. **Qt event loop responsiveness** - UI stability

### Phase 2: Important (Week 3-4)
4. **X11/Wayland resource leaks** - Platform stability
5. **Network/MCP server stability** - Remote operation
6. **Input event processing** - KVM functionality

### Phase 3: Nice-to-Have (Week 5-6)
7. **Disk I/O monitoring** - Performance optimization
8. **System resource contention** - Environment validation
9. **Audio device stability** - Feature completeness
10. **GPU memory tracking** - Hardware acceleration

---

## Testing Strategy

### Short Soak Tests (5-15 minutes)
- Focus on critical checks (1-3)
- Quick validation of core functionality
- CI/CD integration

### Standard Soak Tests (30-60 minutes)
- Include important checks (4-6)
- Comprehensive stability validation
- Pre-release testing

### Extended Soak Tests (2-24 hours)
- All checks enabled
- Resource leak detection
- Long-term stability validation

---

## Integration with CI/CD

```yaml
# .github/workflows/soak-test.yml
name: Enhanced Soak Test

on:
  schedule:
    - cron: '0 2 * * *'  # Daily at 2 AM
  workflow_dispatch:

jobs:
  soak-test:
    strategy:
      matrix:
        backend: [ffmpeg, gstreamer]
        duration: [30, 120]  # 30 min and 2 hour tests
    
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Build
        run: |
          mkdir build && cd build
          cmake ..
          make -j$(nproc)
      
      - name: Run Enhanced Soak Test
        run: |
          Xvfb :99 -screen 0 1280x720x24 &
          export DISPLAY=:99
          ./tests/gui_soak_test.sh --native --backend ${{ matrix.backend }} ${{ matrix.duration }} 30
      
      - name: Upload Results
        if: always()
        uses: actions/upload-artifact@v3
        with:
          name: soak-test-${{ matrix.backend }}-${{ matrix.duration }}min
          path: |
            tests/soak_test_logs/
            tests/soak_test_screenshots/
```

---

## Conclusion

The proposed enhancements would transform the soak test from a basic health check into a comprehensive stability validation tool. By monitoring video performance, device communication, UI responsiveness, and system resources, we can catch issues before they reach users and ensure the highest quality for the OpenterfaceQT application.

**Key Benefits:**
- 🎯 Early detection of performance degradation
- 🔍 Comprehensive coverage of all subsystems
- 📊 Quantitative metrics for trend analysis
- 🚀 Automated CI/CD integration
- 💪 Increased confidence in releases
