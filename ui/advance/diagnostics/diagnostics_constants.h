#ifndef UI_ADVANCE_DIAGNOSTICS_CONSTANTS_H
#define UI_ADVANCE_DIAGNOSTICS_CONSTANTS_H

#include <QString>

namespace Diagnostics {

// Window / UI strings
inline constexpr const char* WINDOW_TITLE = "Hardware Diagnostics";
inline constexpr const char* LOG_FILE_NAME = "diagnostics_log.txt";
inline constexpr const char* LOG_PLACEHOLDER = "Test logs will appear here...";
inline constexpr const char* RESTART_TITLE = "Restart Diagnostics";
inline constexpr const char* RESTART_CONFIRM = "This will reset all test results and start over. Continue?";
inline constexpr const char* TEST_LOG_HEADER = "Hardware Diagnostics Log - %1\n";
inline constexpr const char* LOG_OPEN_ERROR_TITLE = "Error";
inline constexpr const char* LOG_OPEN_ERROR = "Could not open log file: %1";

// Diagnostic completion messages
inline constexpr const char* DIAGNOSTICS_COMPLETE_SUCCESS = "All diagnostic tests completed successfully!";
inline constexpr const char* DIAGNOSTICS_COMPLETE_FAIL = "Diagnostic tests completed with some failures. Please check the results.";

// Reminders for tests
inline const char* REMINDERS[] = {
    "Verify all cable connections (Orange USB-C, Black USB-C, HDMI)",
    "Testing Black USB-C hotplug/unplug detection and reconnection (target side)",
    "Testing Orange USB-C hotplug/unplug detection and reconnection (host side)",
    "Detecting serial port baud rate and connection status",
    "Resting serial configuration to factory default",
    "Changeing serial port baudrate to 115200",
    "Testing serial port communication at low baudrate (9600)",
    "Running platform stress test for 30 seconds"
};

inline constexpr const char* FOLLOW_INSTRUCTIONS = "Follow the test instructions carefully";

} // namespace Diagnostics

#endif // UI_ADVANCE_DIAGNOSTICS_CONSTANTS_H
