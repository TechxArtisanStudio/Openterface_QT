# Serial Command Tracking and Auto-Restart Feature

## Overview
This document describes the command tracking and automatic serial port restart feature implemented in the SerialPortManager.

## Feature Description
The system monitors serial command success rates and automatically restarts the serial port if too many commands fail to receive responses.

## Implementation Details

### Key Components

1. **Command Counters**
   - `m_commandsSent`: Tracks total commands sent
   - `m_commandsReceived`: Tracks successful responses received
   - `m_serialResetCount`: Tracks how many times the serial port has been reset

2. **Configuration Constants**
   - `COMMAND_TRACKING_INTERVAL`: 5000ms (5 seconds) - Window for tracking commands
   - `COMMAND_LOSS_THRESHOLD`: 0.30 (30%) - Loss rate that triggers restart
   - `MAX_SERIAL_RESETS`: 3 - Maximum number of restart attempts

3. **Timer**
   - `m_commandTrackingTimer`: QTimer that fires every 5 seconds to check loss rate

### How It Works

1. **Command Tracking**
   - Every time a command is sent via `sendAsyncCommand()`, the `m_commandsSent` counter is incremented
   - Every time a valid response is received in `readData()`, the `m_commandsReceived` counter is incremented

2. **Loss Rate Calculation** (every 5 seconds)
   - Loss rate = 1.0 - (received / sent)
   - Example: If 10 commands sent and 6 received, loss rate = 1.0 - (6/10) = 0.40 (40%)

3. **Auto-Restart Logic**
   - If loss rate ≥ 30%:
     - Reset count < 3: Trigger `restartPort()` and increment reset counter
     - Reset count = 3 and no data received: Log critical error and stop tracking
     - Reset count = 3 but some data received: Continue monitoring
   - If loss rate < 30%:
     - Communication is good, reset the serial reset counter to 0

4. **Counter Reset**
   - After each 5-second evaluation, command counters are reset to start fresh tracking

## Usage Example

### Normal Operation
```
[5s] Sent=10, Received=9 → Loss rate=10% → OK, continue
[10s] Sent=12, Received=11 → Loss rate=8% → OK, continue
```

### Restart Triggered
```
[5s] Sent=10, Received=6 → Loss rate=40% → RESTART (attempt 1/3)
[10s] Sent=8, Received=7 → Loss rate=12% → OK, reset counter to 0
```

### Maximum Resets Reached
```
[5s] Sent=10, Received=5 → Loss rate=50% → RESTART (attempt 1/3)
[10s] Sent=10, Received=4 → Loss rate=60% → RESTART (attempt 2/3)
[15s] Sent=10, Received=3 → Loss rate=70% → RESTART (attempt 3/3)
[20s] Sent=10, Received=0 → Loss rate=100% → CRITICAL ERROR, stop tracking
```

## Benefits

1. **Automatic Recovery**: Detects communication problems and attempts recovery without user intervention
2. **Prevents Infinite Loops**: Limited to 3 restart attempts to avoid endless restart cycles
3. **Adaptive**: Resets the restart counter when communication recovers
4. **Detailed Logging**: Provides clear debug information about command tracking and decisions

## Debug Logging

The feature logs the following information:
- Command counts every 5 seconds
- Calculated loss rates
- Restart trigger decisions
- Reset counter status
- Critical errors when max resets reached

Example log output:
```
Command tracking: Sent=10 Received=6 ResetCount=0
Command loss rate: 40.0%
Command loss rate (40.0%) exceeds threshold. Triggering serial port restart. Reset count: 1/3
```

## Integration Points

- **SerialPortManager.h**: Added member variables and method declarations
- **SerialPortManager.cpp**: 
  - Constructor: Initialize counters and timer
  - `sendAsyncCommand()`: Increment sent counter
  - `readData()`: Increment received counter on successful response
  - `checkCommandLossRate()`: Main evaluation logic and UI notification
  - `resetCommandCounters()`: Reset counters for next window
  - Destructor/stop(): Clean up timer

## UI Integration

The auto-restart feature is integrated with the status bar to provide visual feedback to users:

### Status Bar Notifications

When an auto-restart is triggered, the status bar displays:
```
⚠️ Auto-restarting serial port (attempt 1/3) - Loss rate: 40.0%
```

### Integration Flow

1. **StatusEventCallback** (`ui/statusevents.h`)
   - Added `onSerialAutoRestart(int attemptNumber, int maxAttempts, double lossRate)` callback method

2. **StatusBarManager** (`ui/statusbar/statusbarmanager.h/cpp`)
   - Added `showSerialAutoRestart()` method to display formatted messages in the status bar
   - Uses the existing `showThrottledMessage()` infrastructure to prevent message flooding
   - Displays messages in orange color with 3-second duration

3. **MainWindow** (`ui/mainwindow.h/cpp`)
   - Implements `onSerialAutoRestart()` callback from StatusEventCallback interface
   - Forwards notifications to StatusBarManager for display

4. **SerialPortManager** (`serial/SerialPortManager.cpp`)
   - Calls `eventCallback->onSerialAutoRestart()` when auto-restart is triggered
   - Provides attempt number, max attempts, and loss rate for informative display

### User Experience

Users will see real-time notifications when:
- Serial communication degrades (≥30% packet loss)
- Automatic recovery is attempted (1-3 times)
- The system is working to restore communication

This provides transparency and helps users understand when connectivity issues occur without requiring manual intervention.

## Future Enhancements

Possible improvements:
- Make thresholds configurable via settings
- Add statistics collection for analysis
- Implement exponential backoff for restart attempts
