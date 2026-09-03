/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#include "mcpToolHandler.h"
#include "mcpProtocol.h"
#include "screenAnalyzer.h"
#include "ai/SharedToolExecutor.h"
#include "host/HostManager.h"
#include "ui/globalsetting.h"
#include "host/cameramanager.h"
#include "target/MouseManager.h"
#include "target/KeyboardManager.h"
#include "scripts/KeyboardMouse.h"
#include "scripts/scriptRunner.h"
#include "scripts/scriptExecutor.h"
#include "scripts/Lexer.h"
#include "scripts/Parser.h"
#include "scripts/AST.h"
#include "serial/SerialPortManager.h"
#include "video/videohid.h"
#include "video/firmwareoperationmanager.h"
#include "ui/advance/wchflash/WCHFlashWorker.h"

#include <QBuffer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QImage>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <QCoreApplication>
#include "log/opflogging.h"

OPF_LOGGING_CATEGORY(log_server_mcp_tool, "opf.server.mcp.tool")

McpToolHandler::McpToolHandler(QObject *parent)
    : QObject(parent)
    , m_screenAnalyzer(new ScreenAnalyzer())
{
}

void McpToolHandler::setCameraManager(CameraManager* cam) {
    m_cameraManager = cam;
}

void McpToolHandler::setScriptRunner(ScriptRunner* runner) {
    m_scriptRunner = runner;
}

void McpToolHandler::setScriptExecutor(ScriptExecutor* executor) {
    m_scriptExecutor = executor;
}

// ---------------------------------------------------------------------------
// tools/list — return all tool definitions
// ---------------------------------------------------------------------------
QJsonArray McpToolHandler::listTools() const
{
    QJsonArray tools;

    // ---- Mouse Tools ----
    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_MOUSE_MOVE_ABSOLUTE;
        tool["description"] = "Move the mouse cursor to an absolute position on the target computer. Coordinates are in the range 0-4096, where (0,0) is the top-left corner and (4096,4096) is the bottom-right corner. Convert from pixel coordinates: MCP_x = pixel_x / screen_width * 4096, MCP_y = pixel_y / screen_height * 4096. Example: on a 1920x1080 screen, pixel (960, 540) = MCP (2048, 2048).";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["x"] = QJsonObject{{"type", "integer"}, {"description", "X coordinate (0-4096). Formula: pixel_x / screen_width * 4096"}, {"minimum", 0}, {"maximum", 4096}};
        props["y"] = QJsonObject{{"type", "integer"}, {"description", "Y coordinate (0-4096). Formula: pixel_y / screen_height * 4096"}, {"minimum", 0}, {"maximum", 4096}};
        schema["properties"] = props;
        schema["required"] = QJsonArray{"x", "y"};
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_MOUSE_CLICK;
        tool["description"] = "Click the mouse at a specific position on the target computer. Coordinate range: 0-4096. Convert from pixel coordinates: MCP_x = pixel_x / screen_width * 4096, MCP_y = pixel_y / screen_height * 4096. Supports left, right, and middle clicks, as well as double-click.";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["x"] = QJsonObject{{"type", "integer"}, {"description", "X coordinate (0-4096). Formula: pixel_x / screen_width * 4096"}, {"minimum", 0}, {"maximum", 4096}};
        props["y"] = QJsonObject{{"type", "integer"}, {"description", "Y coordinate (0-4096). Formula: pixel_y / screen_height * 4096"}, {"minimum", 0}, {"maximum", 4096}};
        props["button"] = QJsonObject{{"type", "string"}, {"description", "Mouse button to click"}, {"enum", QJsonArray{"left", "right", "middle"}}, {"default", "left"}};
        props["count"] = QJsonObject{{"type", "integer"}, {"description", "Number of clicks (1 = single, 2 = double-click)"}, {"default", 1}, {"minimum", 1}, {"maximum", 10}};
        schema["properties"] = props;
        schema["required"] = QJsonArray{"x", "y"};
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_MOUSE_MOVE_RELATIVE;
        tool["description"] = "Move the mouse cursor relative to its current position on the target computer. Positive dx moves right, positive dy moves down.";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["dx"] = QJsonObject{{"type", "integer"}, {"description", "Horizontal delta (positive = right)"}};
        props["dy"] = QJsonObject{{"type", "integer"}, {"description", "Vertical delta (positive = down)"}};
        schema["properties"] = props;
        schema["required"] = QJsonArray{"dx", "dy"};
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_MOUSE_SCROLL;
        tool["description"] = "Scroll the mouse wheel on the target computer.";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["direction"] = QJsonObject{{"type", "string"}, {"description", "Scroll direction"}, {"enum", QJsonArray{"up", "down"}}};
        props["lines"] = QJsonObject{{"type", "integer"}, {"description", "Number of scroll lines"}, {"default", 1}, {"minimum", 1}};
        schema["properties"] = props;
        schema["required"] = QJsonArray{"direction"};
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    // ---- Keyboard Tools ----
    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_KEYBOARD_PRESS_KEY;
        tool["description"] = "Press or release a keyboard key on the target computer. Accepts a Qt key code (integer) or a key name (string). Common names: A-Z, 0-9, Enter/Return, Escape, Tab, Backspace, Delete, Space, Up, Down, Left, Right, Home, End, PageUp, PageDown, Insert, F1-F12, Shift, Control, Alt, Meta/Super. Modifiers are a bitmask: 1=Shift, 2=Ctrl, 4=Alt, 8=Meta/Win.";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["key"] = QJsonObject{{"type", "string"}, {"description", "Qt key code (integer) or key name (string, e.g. 'A', 'Enter', 'Escape', 'Tab', 'F1', 'Control', 'Shift', 'Alt')"}};
        props["modifiers"] = QJsonObject{{"type", "integer"}, {"description", "Qt::KeyboardModifiers bitfield, as KeyboardManager expects: Shift=33554432 (0x02000000), Ctrl=67108864 (0x04000000), Alt=134217728 (0x08000000), Meta/Win=268435456 (0x10000000); OR them together. (Not 1/2/4/8.)"}, {"default", 0}};
        props["isKeyDown"] = QJsonObject{{"type", "boolean"}, {"description", "true = press, false = release"}, {"default", true}};
        props["side"] = QJsonObject{{"type", "string"}, {"description", "Which side of the keyboard for modifier keys: 'left' or 'right'. Only applies to Shift, Ctrl, Alt keys."}, {"enum", QJsonArray{"left", "right"}}};
        props["autoRelease"] = QJsonObject{{"type", "boolean"}, {"description", "If true (default), auto-release the key after press. Set false to hold the key (useful for modifier combos)."}, {"default", true}};
        schema["properties"] = props;
        schema["required"] = QJsonArray{"key"};
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_KEYBOARD_TYPE_TEXT;
        tool["description"] = "Type a string of text on the target computer. The text will be pasted using keyboard emulation.";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["text"] = QJsonObject{{"type", "string"}, {"description", "Text to type on the target computer"}};
        schema["properties"] = props;
        schema["required"] = QJsonArray{"text"};
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_KEYBOARD_SEND_KEYS;
        tool["description"] = "Send keystrokes using AutoHotkey-like syntax. Examples: \"Hello World\", \"^c\" (Ctrl+C), \"!{Tab}\" (Alt+Tab), \"{Enter}\", \"#r\" (Win+R). See AutoHotkey Send documentation for full syntax.";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["keys"] = QJsonObject{{"type", "string"}, {"description", "Keystroke sequence in AutoHotkey-like syntax"}};
        schema["properties"] = props;
        schema["required"] = QJsonArray{"keys"};
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_KEYBOARD_FUNCTION_KEY;
        tool["description"] = "Send a function key (F1-F12) to the target computer.";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["key"] = QJsonObject{{"type", "string"}, {"description", "Function key to send"}, {"enum", QJsonArray{"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"}}};
        schema["properties"] = props;
        schema["required"] = QJsonArray{"key"};
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_KEYBOARD_CTRL_ALT_DEL;
        tool["description"] = "Send the Ctrl+Alt+Del key combination to the target computer (equivalent to pressing Ctrl+Alt+Delete).";

        QJsonObject schema;
        schema["type"] = "object";
        schema["properties"] = QJsonObject();
        schema["required"] = QJsonArray();
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_KEYBOARD_SET_LAYOUT;
        tool["description"] = "Switch the keyboard layout used by Openterface for the target computer. Layout names depend on the loaded keyboard layout files (e.g., \"us\", \"de\", \"fr\", \"jp\").";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["layout"] = QJsonObject{{"type", "string"}, {"description", "Keyboard layout name (e.g., \"us\", \"de\", \"fr\", \"jp\")"}};
        schema["properties"] = props;
        schema["required"] = QJsonArray{"layout"};
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    // ---- Screen Capture Tools ----
    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_CAPTURE_SCREEN;
        tool["description"] = "Capture the current screen from the target computer via the video input. Returns a JPEG image encoded in base64.";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["quality"] = QJsonObject{{"type", "integer"}, {"description", "JPEG quality (1-100)"}, {"default", 90}, {"minimum", 1}, {"maximum", 100}};
        schema["properties"] = props;
        schema["required"] = QJsonArray();
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_CAPTURE_LAST_IMAGE;
        tool["description"] = "Retrieve the last captured/saved image file from the local pictures folder (Openterface screenshots). Returns a JPEG/PNG image encoded in base64.";

        QJsonObject schema;
        schema["type"] = "object";
        schema["properties"] = QJsonObject();
        schema["required"] = QJsonArray();
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    // ---- USB Control Tools ----
    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_USB_SWITCH;
        tool["description"] = "Switch the USB connection between host (control computer) and target (controlled computer). Use this to ensure keyboard/mouse data is sent to the correct destination.";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["target"] = QJsonObject{{"type", "string"}, {"description", "USB target: 'host' for control computer, 'target' for controlled computer"}, {"enum", QJsonArray{"host", "target"}}};
        schema["properties"] = props;
        schema["required"] = QJsonArray{"target"};
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    // ---- Screen Analysis Tools ----
    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_SCREEN_TO_MARKDOWN;
        tool["description"] = "Capture the target screen and convert it to a structured Markdown representation with OCR-detected text and UI element locations. Returns coordinates (0-4096 range) for all detected elements, making it easy for AI to find buttons, menus, and interactive elements without needing vision. For a text console (shell, boot log, BIOS text screen) pass mode='terminal', which reads the screen as lines of text and OCRs only the region that changed since the previous call. Requires Tesseract OCR to be installed and the feature to be enabled in Preferences -> MCP Server.";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["detail_level"] = QJsonObject{{"type", "string"}, {"description", "Level of detail in output: 'basic' (only interactive elements) or 'detailed' (full breakdown with all text)"}, {"enum", QJsonArray{"basic", "detailed"}}, {"default", "detailed"}};
        props["mode"] = QJsonObject{{"type", "string"}, {"description", "Analysis mode: 'general' for a graphical UI (text and UI elements with coordinates), or 'terminal' for a text console (line-oriented text, differential OCR of the changed region only). Ignored by detail_level."}, {"enum", QJsonArray{"general", "terminal"}}, {"default", "general"}};
        schema["properties"] = props;
        schema["required"] = QJsonArray();
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    // ---- Cursor Detection ----
    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_DETECT_CURSOR;
        tool["description"] = "Detect whether the target terminal is waiting for user input by combining three independent signals: (1) cursor blink detection via temporal frame differencing — captures 5 frames at 350ms intervals and looks for a small region toggling on/off at a fixed position, (2) screen stability — measures total change ratio across frames to distinguish idle from active output, and (3) shell prompt detection via OCR on the bottom line. Returns cursor position (pixel and MCP coordinates), confidence, individual signal details, and a status: 'idle' (terminal waiting for input), 'likely_idle' (screen stable but cursor blink or prompt not confirmed), 'outputting' (content still flowing), or 'unknown'. Best used after running a command to check if it has finished and the shell is ready for the next input.";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["samples"] = QJsonObject{{"type", "integer"}, {"description", "Number of frames to capture (minimum 3). 5 frames gives 4 diffs across 1.4s — enough to catch slow blink rates (up to ~2s period). More = higher confidence but slower."}, {"default", 5}, {"minimum", 3}, {"maximum", 8}};
        props["interval_ms"] = QJsonObject{{"type", "integer"}, {"description", "Milliseconds between frame captures. 350ms avoids synchronizing with typical blink periods (500ms, 1000ms, 1200ms)."}, {"default", 350}, {"minimum", 200}, {"maximum", 1000}};
        schema["properties"] = props;
        schema["required"] = QJsonArray();
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    // ---- Run Command and Wait ----
    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_RUN_COMMAND_AND_WAIT;
        tool["description"] = "Type a command in the terminal and wait until the terminal is idle (ready for next input). Combines keyboard_type_text + polling detect_cursor in a loop. Returns when terminal status is 'idle' or timeout is reached. Use this for long-running commands where you need to wait for completion before sending the next command.";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["command"] = QJsonObject{{"type", "string"}, {"description", "Command text to type. A newline is appended automatically if not present."}};
        props["max_wait_ms"] = QJsonObject{{"type", "integer"}, {"description", "Maximum time to wait for terminal to become idle (milliseconds)."}, {"default", 30000}, {"minimum", 5000}, {"maximum", 120000}};
        props["poll_interval_ms"] = QJsonObject{{"type", "integer"}, {"description", "Time between detect_cursor polls (milliseconds)."}, {"default", 2000}, {"minimum", 500}, {"maximum", 10000}};
        props["initial_delay_ms"] = QJsonObject{{"type", "integer"}, {"description", "Delay after typing command before first poll (lets output start)."}, {"default", 1500}, {"minimum", 0}, {"maximum", 10000}};
        props["samples"] = QJsonObject{{"type", "integer"}, {"description", "Frames per detect_cursor call."}, {"default", 5}, {"minimum", 3}, {"maximum", 8}};
        props["detect_interval_ms"] = QJsonObject{{"type", "integer"}, {"description", "Interval between frames in detect_cursor."}, {"default", 350}, {"minimum", 200}, {"maximum", 1000}};
        schema["properties"] = props;
        schema["required"] = QJsonArray{"command"};
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    // ---- Script Execution ----
    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_EXECUTE_SCRIPT;
        tool["description"] = "Execute a script using the built-in AutoHotkey-like scripting engine. Supports commands like: Click, MouseMove, Send, Sleep, Scroll, FullScreenCapture, AreaScreenCapture, SetCapsLockState, SetNumLockState, SetScrollLockState.";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["script"] = QJsonObject{{"type", "string"}, {"description", "Script text in AutoHotkey-like syntax"}};
        schema["properties"] = props;
        schema["required"] = QJsonArray{"script"};
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    // ---- Script Validation ----
    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_VALIDATE_SCRIPT;
        tool["description"] = "Validate an AutoHotkey-like script without executing it. Returns detailed information about syntax errors, invalid commands, and parameter issues.";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["script"] = QJsonObject{{"type", "string"}, {"description", "Script text to validate"}};
        schema["properties"] = props;
        schema["required"] = QJsonArray{"script"};
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    // ---- Firmware Tools ----
    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_FIRMWARE_CHECK;
        tool["description"] = "Check the video chip firmware version against the latest published release. Reports the current version, the latest version and whether an update is available. Downloads the latest firmware binary into memory as a side effect.";

        QJsonObject schema;
        schema["type"] = "object";
        schema["properties"] = QJsonObject();
        schema["required"] = QJsonArray();
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_FIRMWARE_UPDATE;
        tool["description"] = "Update the video chip firmware to the latest published release. Stops all video/HID/serial activity, writes the firmware EEPROM and blocks until the write completes (up to 5 minutes). After success the app must be restarted and the device fully power-cycled (host USB power AND target power); video and control are dead until then.";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["force"] = QJsonObject{{"type", "boolean"}, {"description", "Re-flash even if the device already reports the latest version (default false)"}};
        schema["properties"] = props;
        schema["required"] = QJsonArray();
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_KEYMOD_SCAN;
        tool["description"] = "Scan for WCH ISP bootloader devices, i.e. a KeyMod (CH32V208) control chip in ISP mode (USB ID 1a86:55e0 or 4348:55e0). The KeyMod only enters ISP mode physically: with the device fully unpowered, hold its BOOT button while reconnecting power. Returns the device list with the indices keymod_flash expects.";

        QJsonObject schema;
        schema["type"] = "object";
        schema["properties"] = QJsonObject();
        schema["required"] = QJsonArray();
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    {
        QJsonObject tool;
        tool["name"] = MCP_TOOL_KEYMOD_FLASH;
        tool["description"] = "Flash a firmware file to the KeyMod (CH32V208) control chip over WCH ISP. Requires the device in ISP/bootloader mode (visible to keymod_scan). Erases, programs, verifies and resets the chip; blocks until done. After success the device must be power-cycled WITHOUT the BOOT button held to boot the new firmware.";

        QJsonObject schema;
        schema["type"] = "object";
        QJsonObject props;
        props["file"] = QJsonObject{{"type", "string"}, {"description", "Path to the firmware file (.hex Intel HEX or .bin raw binary) on the machine running this app"}};
        props["device_index"] = QJsonObject{{"type", "integer"}, {"description", "Index into the keymod_scan device list (default 0)"}};
        schema["properties"] = props;
        schema["required"] = QJsonArray{"file"};
        tool["inputSchema"] = schema;
        tools.append(tool);
    }

    return tools;
}

// ---------------------------------------------------------------------------
// tools/call — dispatch to the right handler
// ---------------------------------------------------------------------------
QJsonObject McpToolHandler::callTool(const QString& name, const QJsonObject& arguments)
{
    if (name == MCP_TOOL_MOUSE_MOVE_ABSOLUTE)      return toolMouseMoveAbsolute(arguments);
    if (name == MCP_TOOL_MOUSE_CLICK)               return toolMouseClick(arguments);
    if (name == MCP_TOOL_MOUSE_MOVE_RELATIVE)        return toolMouseMoveRelative(arguments);
    if (name == MCP_TOOL_MOUSE_SCROLL)               return toolMouseScroll(arguments);
    if (name == MCP_TOOL_KEYBOARD_PRESS_KEY)         return toolKeyboardPressKey(arguments);
    if (name == MCP_TOOL_KEYBOARD_TYPE_TEXT)         return toolKeyboardTypeText(arguments);
    if (name == MCP_TOOL_KEYBOARD_SEND_KEYS)         return toolKeyboardSendKeys(arguments);
    if (name == MCP_TOOL_KEYBOARD_FUNCTION_KEY)      return toolKeyboardFunctionKey(arguments);
    if (name == MCP_TOOL_KEYBOARD_CTRL_ALT_DEL)      return toolKeyboardCtrlAltDel(arguments);
    if (name == MCP_TOOL_KEYBOARD_SET_LAYOUT)        return toolKeyboardSetLayout(arguments);
    if (name == MCP_TOOL_CAPTURE_SCREEN)             return toolCaptureScreen(arguments);
    if (name == MCP_TOOL_CAPTURE_LAST_IMAGE)         return toolCaptureLastImage(arguments);
    if (name == MCP_TOOL_EXECUTE_SCRIPT)             return toolExecuteScript(arguments);
    if (name == MCP_TOOL_VALIDATE_SCRIPT)             return toolValidateScript(arguments);
    if (name == MCP_TOOL_SYSTEM_STATUS)              return toolSystemStatus(arguments);
    if (name == MCP_TOOL_USB_SWITCH)                 return toolUsbSwitch(arguments);
    if (name == MCP_TOOL_SCREEN_TO_MARKDOWN)         return toolScreenToMarkdown(arguments);
    if (name == MCP_TOOL_DETECT_CURSOR)              return toolDetectCursor(arguments);
    if (name == MCP_TOOL_RUN_COMMAND_AND_WAIT)       return toolRunCommandAndWait(arguments);
    if (name == MCP_TOOL_FIRMWARE_CHECK)             return toolFirmwareCheck(arguments);
    if (name == MCP_TOOL_FIRMWARE_UPDATE)            return toolFirmwareUpdate(arguments);
    if (name == MCP_TOOL_KEYMOD_SCAN)                return toolKeymodScan(arguments);
    if (name == MCP_TOOL_KEYMOD_FLASH)               return toolKeymodFlash(arguments);

    return errorResult("Unknown tool: " + name);
}

// ==========================================================================
// Mouse Tool Implementations
// ==========================================================================

QJsonObject McpToolHandler::toolMouseMoveAbsolute(const QJsonObject& args)
{
    QJsonObject result = SharedToolExecutor::instance().mouseMoveAbsolute(args);
    if (result.contains("error")) {
        return errorResult(result["error"].toString());
    }
    int x = result["x"].toInt();
    int y = result["y"].toInt();
    return textResult(QString("Mouse moved to absolute position (%1, %2)").arg(x).arg(y));
}

QJsonObject McpToolHandler::toolMouseClick(const QJsonObject& args)
{
    QJsonObject result = SharedToolExecutor::instance().mouseClick(args);
    if (result.contains("error")) {
        return errorResult(result["error"].toString());
    }
    int count = result["count"].toInt();
    int x = result["x"].toInt();
    int y = result["y"].toInt();
    return textResult(QString("Mouse %1-click at (%2, %3)").arg(count).arg(x).arg(y));
}

QJsonObject McpToolHandler::toolMouseMoveRelative(const QJsonObject& args)
{
    int dx = args.value("dx").toInt();
    int dy = args.value("dy").toInt();

    MouseManager& mm = HostManager::getInstance().getMouseManager();
    mm.handleRelativeMouseAction(dx, dy, 0, 0);

    // Add delay to allow CH32V208 to process the command
    QThread::msleep(30);

    return textResult(QString("Mouse moved relative by (%1, %2)").arg(dx).arg(dy));
}

QJsonObject McpToolHandler::toolMouseScroll(const QJsonObject& args)
{
    QString direction = args.value("direction").toString();
    int lines = args.value("lines").toInt(1);

    int dir = (direction == "up") ? 1 : -1;

    MouseManager& mm = HostManager::getInstance().getMouseManager();
    mm.scrollWheel(dir, lines);

    return textResult(QString("Scrolled %1 %2 lines").arg(direction).arg(lines));
}

// ==========================================================================
// Keyboard Tool Implementations
// ==========================================================================

static int resolveKey(const QJsonValue& keyArg)
{
    if (keyArg.isDouble()) {
        return keyArg.toInt();
    }
    QString name = keyArg.toString().toUpper();
    if (name.isEmpty()) {
        return 0;
    }
    static const QMap<QString, int> keyNameMap = {
        {"ENTER", Qt::Key_Return}, {"RETURN", Qt::Key_Return},
        {"ESCAPE", Qt::Key_Escape}, {"ESC", Qt::Key_Escape},
        {"TAB", Qt::Key_Tab},
        {"BACKSPACE", Qt::Key_Backspace},
        {"DELETE", Qt::Key_Delete},
        {"SPACE", Qt::Key_Space},
        {"UP", Qt::Key_Up}, {"DOWN", Qt::Key_Down},
        {"LEFT", Qt::Key_Left}, {"RIGHT", Qt::Key_Right},
        {"HOME", Qt::Key_Home}, {"END", Qt::Key_End},
        {"PAGEUP", Qt::Key_PageUp}, {"PAGEDOWN", Qt::Key_PageDown},
        {"INSERT", Qt::Key_Insert},
        {"PRINT", Qt::Key_Print}, {"PRINTSCREEN", Qt::Key_Print},
        {"PAUSE", Qt::Key_Pause}, {"BREAK", Qt::Key_Pause},
        {"MENU", Qt::Key_Menu}, {"CONTEXTMENU", Qt::Key_Menu},
        {"CAPSLOCK", Qt::Key_CapsLock}, {"CAPS", Qt::Key_CapsLock},
        {"NUMLOCK", Qt::Key_NumLock},
        {"SCROLLLOCK", Qt::Key_ScrollLock},
        {"F1", Qt::Key_F1}, {"F2", Qt::Key_F2}, {"F3", Qt::Key_F3},
        {"F4", Qt::Key_F4}, {"F5", Qt::Key_F5}, {"F6", Qt::Key_F6},
        {"F7", Qt::Key_F7}, {"F8", Qt::Key_F8}, {"F9", Qt::Key_F9},
        {"F10", Qt::Key_F10}, {"F11", Qt::Key_F11}, {"F12", Qt::Key_F12},
        {"F13", Qt::Key_F13}, {"F14", Qt::Key_F14}, {"F15", Qt::Key_F15},
        {"SHIFT", Qt::Key_Shift},
        {"CONTROL", Qt::Key_Control}, {"CTRL", Qt::Key_Control},
        {"ALT", Qt::Key_Alt}, {"ALTGR", Qt::Key_AltGr},
        {"META", Qt::Key_Meta}, {"SUPER", Qt::Key_Meta}, {"WIN", Qt::Key_Meta},
        {"COMMAND", Qt::Key_Meta},
    };
    if (keyNameMap.contains(name)) {
        return keyNameMap[name];
    }
    if (name.length() == 1) {
        QChar c = name[0];
        if (c.isLetter()) {
            return Qt::Key_A + (c.toUpper().unicode() - 'A');
        }
        if (c.isDigit()) {
            return Qt::Key_0 + (c.unicode() - '0');
        }
    }
    return 0;
}

QJsonObject McpToolHandler::toolKeyboardPressKey(const QJsonObject& args)
{
    QJsonValue keyArg = args.value("key");
    int keyCode = resolveKey(keyArg);

    if (keyArg.isString() && keyCode == 0) {
        return errorResult("Unknown key name: " + keyArg.toString());
    }

    int modifiers = args.value("modifiers").toInt(0);
    bool isKeyDown = args.value("isKeyDown").toBool(true);
    bool autoRelease = args.value("autoRelease").toBool(true);

    // Extract the "side" parameter if present
    QString side = args.value("side").toString(""); // empty string by default
    unsigned int nativeVirtualKey = 0;

    // Map side parameter to nativeVirtualKey values
    // Windows virtual key codes for left/right modifiers
    if (!side.isEmpty()) {
        if (keyCode == Qt::Key_Shift) {
            if (side == "left") {
                nativeVirtualKey = 0xA0; // VK_LSHIFT
            } else if (side == "right") {
                nativeVirtualKey = 0xA1; // VK_RSHIFT
            }
        } else if (keyCode == Qt::Key_Control) {
            if (side == "left") {
                nativeVirtualKey = 0xA2; // VK_LCONTROL
            } else if (side == "right") {
                nativeVirtualKey = 0xA3; // VK_RCONTROL
            }
        } else if (keyCode == Qt::Key_Alt || keyCode == Qt::Key_AltGr) {
            if (side == "left") {
                nativeVirtualKey = 0xA4; // VK_LMENU
            } else if (side == "right") {
                nativeVirtualKey = 0xA5; // VK_RMENU
            }
        }
    }

    // DIAGNOSTIC: Check serial port state
    SerialPortManager& spm = SerialPortManager::getInstance();
    fprintf(stderr, "[MCP-DIAG] toolKeyboardPressKey: keyCode=%d (0x%x) modifiers=%d (0x%x) side='%s' nativeVirtualKey=0x%x\n",
            keyCode, keyCode, modifiers, modifiers, side.toUtf8().constData(), nativeVirtualKey);
    fflush(stderr);
    qWarning() << "[MCP] SerialPortManager ready:" << spm.isPortReady()
               << "isOpen:" << spm.isPortOpen()
               << "portPath:" << spm.getCurrentSerialPortPath();

    HostManager::getInstance().handleKeyboardAction(keyCode, modifiers, isKeyDown, nativeVirtualKey);

    // Add delay to allow CH32V208 to process the command
    QThread::msleep(30);

    // Auto-release: if key was pressed and autoRelease is true, send release after short delay
    if (isKeyDown && autoRelease) {
        QThread::msleep(50); // 50ms hold time for target to register
        HostManager::getInstance().handleKeyboardAction(keyCode, modifiers, false, nativeVirtualKey);
        QThread::msleep(30);
    }

    QString keyName = keyArg.isString() ? keyArg.toString().toUpper() : QString::number(keyCode);
    QString sideStr = !side.isEmpty() ? QString(", side=%1").arg(side) : "";
    QString actionStr = isKeyDown ? (autoRelease ? "pressed+released" : "pressed") : "released";
    return textResult(QString("Key %1 (name=%2, code=%3, mods=%4%5)")
                     .arg(actionStr)
                     .arg(keyName).arg(keyCode).arg(modifiers).arg(sideStr));
}

// Convenience method: send Enter key as press+release atomically
QJsonObject McpToolHandler::toolKeyboardEnterKey()
{
    HostManager& hm = HostManager::getInstance();
    // Press Enter (Qt::Key_Return = 16777220)
    hm.handleKeyboardAction(Qt::Key_Return, 0, true);
    QThread::msleep(10);
    // Release Enter
    hm.handleKeyboardAction(Qt::Key_Return, 0, false);
    return textResult("Enter key pressed and released");
}

QJsonObject McpToolHandler::toolKeyboardTypeText(const QJsonObject& args)
{
    QString text = args.value("text").toString();

    // DEBUG: Log to file
    QFile debugLog("/tmp/mcp-keyboard-debug.log");
    if (debugLog.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&debugLog);
        out << "=== toolKeyboardTypeText called ===\n";
        out << "Text: " << text << "\n";
        out << "Thread ID: " << QThread::currentThreadId() << "\n";

        // Check serial port ready state
        SerialPortManager& spm = SerialPortManager::getInstance();
        out << "SerialPortManager::ready: " << spm.isPortReady() << "\n";
        out << "SerialPortManager::isPortOpen: " << spm.isPortOpen() << "\n";
        out << "SerialPortManager::portPath: " << spm.getCurrentSerialPortPath() << "\n";
        debugLog.close();
    }

    QJsonObject result = SharedToolExecutor::instance().typeTextCommand(args);
    if (result.contains("error")) {
        return errorResult(result["error"].toString());
    }
    int chars = result["chars_typed"].toInt();
    return textResult(QString("Typed text (%1 chars): %2").arg(chars).arg(text));
}

QJsonObject McpToolHandler::toolKeyboardSendKeys(const QJsonObject& args)
{
    QString keys = args.value("keys").toString();
    if (keys.isEmpty()) {
        return errorResult("Empty keystroke sequence");
    }

    // Use the script engine: create a "Send" command and route through Lexer/Parser/ScriptRunner
    QString script = QString("Send \"%1\"").arg(keys);

    // Lex & Parse
    Lexer lexer;
    lexer.setSource(script.toStdString());
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(tokens);
    std::unique_ptr<ASTNode> tree = parser.parse();

    if (!tree) {
        return errorResult("Failed to parse keystroke sequence");
    }

    // Execute via ScriptRunner
    if (m_scriptRunner) {
        // Use event loop to wait for completion
        QEventLoop loop;
        bool success = false;
        QObject::connect(m_scriptRunner, &ScriptRunner::analysisFinished,
                         &loop, [&](QObject* originSender, bool result) {
            Q_UNUSED(originSender);
            success = result;
            loop.quit();
        });
        // Timeout safeguard
        QTimer::singleShot(30000, &loop, &QEventLoop::quit);

        m_scriptRunner->runTree(std::move(tree));

        if (success) {
            return textResult("Keystroke sequence executed successfully");
        } else {
            return errorResult("Keystroke sequence execution failed");
        }
    }

    return errorResult("ScriptRunner not available");
}

QJsonObject McpToolHandler::toolKeyboardFunctionKey(const QJsonObject& args)
{
    QString key = args.value("key").toString().toUpper();

    static const QMap<QString, int> functionKeyMap = {
        {"F1", Qt::Key_F1}, {"F2", Qt::Key_F2}, {"F3", Qt::Key_F3},
        {"F4", Qt::Key_F4}, {"F5", Qt::Key_F5}, {"F6", Qt::Key_F6},
        {"F7", Qt::Key_F7}, {"F8", Qt::Key_F8}, {"F9", Qt::Key_F9},
        {"F10", Qt::Key_F10}, {"F11", Qt::Key_F11}, {"F12", Qt::Key_F12},
    };

    if (!functionKeyMap.contains(key)) {
        return errorResult("Invalid function key: " + key);
    }

    int keyCode = functionKeyMap[key];
    HostManager::getInstance().handleFunctionKey(keyCode, 0);

    return textResult("Sent " + key);
}

QJsonObject McpToolHandler::toolKeyboardCtrlAltDel(const QJsonObject& args)
{
    Q_UNUSED(args);
    HostManager::getInstance().sendCtrlAltDel();
    return textResult("Sent Ctrl+Alt+Del");
}

QJsonObject McpToolHandler::toolKeyboardSetLayout(const QJsonObject& args)
{
    QString layout = args.value("layout").toString();
    if (layout.isEmpty()) {
        return errorResult("Layout name is empty");
    }

    HostManager::getInstance().setKeyboardLayout(layout);
    return textResult("Keyboard layout set to: " + layout);
}

// ==========================================================================
// Screen Capture Tool Implementations
// ==========================================================================

QJsonObject McpToolHandler::toolCaptureScreen(const QJsonObject& args)
{
    int quality = args.value("quality").toInt(90);
    quality = qBound(1, quality, 100);

    // Get the current frame via shared executor
    QImage frame = SharedToolExecutor::instance().captureFrame();
    if (frame.isNull()) {
        return errorResult("No frame available from camera");
    }

    // Encode as JPEG
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    if (!frame.save(&buffer, "JPEG", quality)) {
        return errorResult("Failed to encode frame as JPEG");
    }

    QByteArray jpegData = buffer.data();
    buffer.close();

    QByteArray base64Data = jpegData.toBase64();

    QJsonObject content = McpProtocol::imageContent(base64Data, "image/jpeg");
    QJsonArray contents{ content };

    return McpProtocol::toolResult(contents);
}

QJsonObject McpToolHandler::toolCaptureLastImage(const QJsonObject& args)
{
    Q_UNUSED(args);

    // Find the newest image in the Openterface pictures folder
    QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (picturesPath.isEmpty())
        picturesPath = QDir::homePath() + "/Pictures";
    QString folderPath = picturesPath + "/openterface";

    QDir dir(folderPath);
    if (!dir.exists()) {
        return errorResult("No captured images found. Use capture_screen first or take a screenshot.");
    }

    QFileInfoList files = dir.entryInfoList(
        QStringList() << "*.jpg" << "*.jpeg" << "*.png",
        QDir::Files, QDir::Time
    );

    if (files.isEmpty()) {
        return errorResult("No image files found in " + folderPath);
    }

    QString latestPath = files.first().absoluteFilePath();
    QFile file(latestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return errorResult("Could not open image file: " + latestPath);
    }

    QByteArray imageData = file.readAll();
    file.close();

    QByteArray base64Data = imageData.toBase64();

    // Determine mime type from extension
    QString suffix = files.first().suffix().toLower();
    QString mimeType = (suffix == "png") ? "image/png" : "image/jpeg";

    QJsonObject content = McpProtocol::imageContent(base64Data, mimeType);
    QJsonArray contents{ content };

    return McpProtocol::toolResult(contents);
}

// ==========================================================================
// Script Execution Tool
// ==========================================================================

QJsonObject McpToolHandler::toolExecuteScript(const QJsonObject& args)
{
    QString scriptText = args.value("script").toString();
    if (scriptText.isEmpty()) {
        return errorResult("Script text is empty");
    }

    // Lex & Parse
    Lexer lexer;
    lexer.setSource(scriptText.toStdString());
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(tokens);
    std::unique_ptr<ASTNode> tree = parser.parse();

    if (!tree) {
        return errorResult("Failed to parse script");
    }

    // Execute via ScriptRunner
    if (m_scriptRunner) {
        QEventLoop loop;
        bool success = false;
        QObject::connect(m_scriptRunner, &ScriptRunner::analysisFinished,
                         &loop, [&](QObject* originSender, bool result) {
            Q_UNUSED(originSender);
            success = result;
            loop.quit();
        });
        // Timeout safeguard
        QTimer::singleShot(60000, &loop, &QEventLoop::quit);

        m_scriptRunner->runTree(std::move(tree));

        if (success) {
            return textResult("Script executed successfully");
        } else {
            return errorResult("Script execution failed");
        }
    }

    return errorResult("ScriptRunner not available");
}

// ==========================================================================
// Script Validation Tool
// ==========================================================================

QJsonObject McpToolHandler::toolValidateScript(const QJsonObject& args)
{
    QString scriptText = args.value("script").toString();

    // 1. Check if script is empty
    if (scriptText.isEmpty()) {
        return errorResult("Script text is empty");
    }

    // 2. Try to tokenize (catch lexer errors)
    try {
        Lexer lexer;
        lexer.setSource(scriptText.toStdString());
        std::vector<Token> tokens = lexer.tokenize();

        // 3. Try to parse (catch parser errors)
        Parser parser(tokens);
        std::unique_ptr<ASTNode> tree = parser.parse();

        if (!tree) {
            return errorResult("Failed to parse script: syntax error");
        }

        // 4. Return success with validation details
        QJsonObject result;
        result["valid"] = true;
        result["message"] = "Script validation successful";
        result["tokenCount"] = static_cast<qint64>(tokens.size());

        // Count commands in the AST
        int commandCount = 0;
        // Simple traversal to count CommandStatement nodes
        // This is a basic implementation - could be enhanced later
        if (auto statementList = dynamic_cast<StatementListNode*>(tree.get())) {
            for (const auto& stmt : statementList->getChildren()) {
                if (dynamic_cast<CommandStatementNode*>(stmt.get())) {
                    commandCount++;
                }
            }
        }
        result["commandCount"] = commandCount;

        return textResult(QJsonDocument(result).toJson(QJsonDocument::Compact));

    } catch (const std::exception& e) {
        // Catch lexer/parser errors
        QJsonObject result;
        result["valid"] = false;
        result["error"] = QString::fromStdString(e.what());
        return textResult(QJsonDocument(result).toJson(QJsonDocument::Compact));
    }
}

// ==========================================================================
// System Status Tool
// ==========================================================================

QJsonObject McpToolHandler::toolSystemStatus(const QJsonObject& args)
{
    Q_UNUSED(args);
    QJsonObject status;

    // --- MCP Server ---
    status["mcp_server"] = "running";

    // --- Serial Port / HID ---
    SerialPortManager& spm = SerialPortManager::getInstance();
    QString portPath = spm.getCurrentSerialPortPath();
    bool serialConnected = !portPath.isEmpty();

    QJsonObject serial;
    serial["connected"]    = serialConnected;
    serial["port_path"]    = portPath;
    serial["port_chain"]   = spm.getCurrentSerialPortChain();

    QString chipName;
    switch (spm.getCurrentChipType()) {
        case ChipType::CH9329:   chipName = "CH9329"; break;
        case ChipType::CH32V208: chipName = "CH32V208"; break;
        default:                 chipName = "unknown"; break;
    }
    serial["chip_type"] = chipName;
    status["serial"] = serial;

    // --- Camera ---
    QJsonObject camera;
    if (m_cameraManager) {
        bool camActive = m_cameraManager->hasActiveCameraDevice();
        camera["active"]  = camActive;
        if (m_cameraManager->isFFmpegBackend()) {
            camera["backend"] = "ffmpeg";
        } else if (m_cameraManager->isGStreamerBackend()) {
            camera["backend"] = "gstreamer";
        } else {
            camera["backend"] = "qt";
        }
        QImage lastFrame = m_cameraManager->getLatestOriginalFrame();
        camera["frame_available"] = !lastFrame.isNull();
        if (!lastFrame.isNull()) {
            camera["frame_width"]  = lastFrame.width();
            camera["frame_height"] = lastFrame.height();
        }
    } else {
        camera["active"] = false;
        camera["backend"] = "not_loaded";
    }
    status["camera"] = camera;

    // --- Host Manager (mouse/keyboard) ---
    QJsonObject host;
    host["mouse_ready"]    = true;   // HostManager singleton is always ready
    host["keyboard_ready"] = true;
    status["host"] = host;

    // --- Summary ---
    bool allOk = serialConnected && camera["active"].toBool();
    status["all_ready"] = allOk;

    return textResult(QJsonDocument(status).toJson(QJsonDocument::Compact));
}

// ==========================================================================
// USB Control Tool
// ==========================================================================

QJsonObject McpToolHandler::toolUsbSwitch(const QJsonObject& args)
{
    QString target = args.value("target").toString().toLower();

    SerialPortManager& spm = SerialPortManager::getInstance();

    if (target == "target") {
        qCInfo(log_server_mcp_tool) << "Switching USB to TARGET (controlled computer)";
        spm.switchUsbToTargetViaSerial();
        QThread::msleep(100);  // Allow time for switch to take effect
        return textResult("USB switched to target (controlled computer). Keyboard/mouse data will now be sent to the target.");
    } else if (target == "host") {
        qCInfo(log_server_mcp_tool) << "Switching USB to HOST (control computer)";
        spm.switchUsbToHostViaSerial();
        QThread::msleep(100);
        return textResult("USB switched to host (control computer).");
    } else {
        return errorResult("Invalid USB target: '" + target + "'. Use 'host' or 'target'.");
    }
}

// ==========================================================================
// Screen Analysis Tool
// ==========================================================================

QJsonObject McpToolHandler::toolScreenToMarkdown(const QJsonObject& args)
{
    // Check if the feature is enabled in settings
    GlobalSetting &settings = GlobalSetting::instance();
    if (!settings.getMcpScreenToMarkdown()) {
        return errorResult("Screen to Markdown feature is disabled. Enable it in Preferences -> MCP Server.");
    }

    QJsonObject result = SharedToolExecutor::instance().screenToMarkdown(args);
    if (result.contains("error")) {
        return errorResult(result["error"].toString());
    }
    return textResult(result["markdown"].toString());
}

// ==========================================================================
// Cursor Detection Tool
// ==========================================================================

QJsonObject McpToolHandler::toolDetectCursor(const QJsonObject& args)
{
    QJsonObject result = SharedToolExecutor::instance().detectCursor(args);
    if (result.contains("error")) {
        return errorResult(result["error"].toString());
    }
    return textResult(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

// ==========================================================================
// Run Command and Wait Implementation
// ==========================================================================

QJsonObject McpToolHandler::toolRunCommandAndWait(const QJsonObject& args)
{
    QJsonObject result = SharedToolExecutor::instance().runCommandAndWait(args);
    if (result.contains("error")) {
        return errorResult(result["error"].toString());
    }
    return textResult(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

// ==========================================================================
// Firmware Tool Implementations
// ==========================================================================

QJsonObject McpToolHandler::toolFirmwareCheck(const QJsonObject& args)
{
    Q_UNUSED(args);
    VideoHid& hid = VideoHid::getInstance();
    FirmwareResult r = hid.isLatestFirmware();

    QString status;
    switch (r) {
    case FirmwareResult::Latest:      status = "latest"; break;
    case FirmwareResult::Upgradable:  status = "upgradable"; break;
    case FirmwareResult::Timeout:     status = "network_timeout"; break;
    case FirmwareResult::CheckFailed: status = "check_failed"; break;
    default:                          status = "unknown"; break;
    }

    return textResult(QString("firmware status: %1 (current: %2, latest: %3)")
        .arg(status,
             QString::fromStdString(hid.getCurrentFirmwareVersion()),
             QString::fromStdString(hid.getLatestFirmwareVersion())));
}

QJsonObject McpToolHandler::toolFirmwareUpdate(const QJsonObject& args)
{
    bool force = args.value("force").toBool(false);

    VideoHid& hid = VideoHid::getInstance();
    FirmwareResult r = hid.isLatestFirmware();
    QString cur = QString::fromStdString(hid.getCurrentFirmwareVersion());
    QString latest = QString::fromStdString(hid.getLatestFirmwareVersion());

    if (r == FirmwareResult::Timeout || r == FirmwareResult::CheckFailed) {
        return errorResult(QString("Firmware check failed (%1); no flash attempted. Current version: %2")
            .arg(r == FirmwareResult::Timeout ? "network timeout" : "check failed", cur));
    }
    if (r == FirmwareResult::Latest && !force) {
        return textResult(QString("Firmware already at latest version %1; nothing flashed. "
            "Pass force=true to re-flash.").arg(cur));
    }

    qCInfo(log_server_mcp_tool) << "Firmware update starting:" << cur << "->" << latest;

    // Same shutdown sequence as FirmwarePage::onCheckForUpdatesClicked():
    // the EEPROM write must not compete with HID polling or serial traffic.
    hid.stop();
    hid.stopPollingOnly();
    QThread::msleep(300);
    SerialPortManager::getInstance().closePort();
    QThread::msleep(200);

    FirmwareOperationManager* mgr = hid.getFirmwareOperationManager();

    QEventLoop loop;
    bool done = false;
    bool ok = false;
    int lastPct = 0;
    QMetaObject::Connection cProg = connect(mgr, &FirmwareOperationManager::progress,
        &loop, [&lastPct](int pct) { lastPct = pct; });
    QMetaObject::Connection cDone = connect(mgr, &FirmwareOperationManager::writeCompleted,
        &loop, [&ok, &done, &loop](bool success) { ok = success; done = true; loop.quit(); });

    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(300000);

    hid.loadFirmwareToEeprom();
    if (!done)
        loop.exec();

    disconnect(cProg);
    disconnect(cDone);

    if (!done) {
        return errorResult(QString("Firmware write TIMED OUT after 300s at %1%. "
            "Device state unknown -- retry or investigate before power-cycling.").arg(lastPct));
    }
    if (!ok) {
        return errorResult(QString("Firmware write FAILED at %1% (%2 -> %3). "
            "Retry before power-cycling the device.").arg(lastPct).arg(cur, latest));
    }
    return textResult(QString("Firmware update SUCCESSFUL: %1 -> %2. "
        "REQUIRED next steps: quit this app, power off BOTH ends of the KVM "
        "(host USB power and target power), then re-energize the KVM before booting the target.")
        .arg(cur, latest));
}

// ==========================================================================
// KeyMod (CH32V208) Firmware Tool Implementations
// ==========================================================================
// Both tools wrap WCHFlashWorker, the same object the Control Chip Firmware
// preference page drives. Its slots run synchronously and emit their signals
// inline, so on this thread plain lambda connections capture the results
// without an event loop.

QJsonObject McpToolHandler::toolKeymodScan(const QJsonObject& args)
{
    Q_UNUSED(args);

    WCHFlashWorker worker;
    QStringList devices;
    QStringList log;
    connect(&worker, &WCHFlashWorker::devicesFound,
            [&devices](const QStringList& d) { devices = d; });
    connect(&worker, &WCHFlashWorker::logMessage,
            [&log](const QString& m) { log << m; });

    worker.scanDevices();

    if (devices.isEmpty()) {
        return textResult(
            "No WCH ISP devices found. The KeyMod must be in ISP/bootloader mode: "
            "with the device fully unpowered (host USB AND target side), hold the "
            "BOOT button while reconnecting power, then release. In ISP mode it "
            "enumerates as 1a86:55e0 or 4348:55e0.\n" + log.join("\n"));
    }

    QString out = QString("Found %1 WCH ISP device(s):\n").arg(devices.size());
    for (int i = 0; i < devices.size(); ++i)
        out += QString("  [%1] %2\n").arg(i).arg(devices.at(i));
    return textResult(out);
}

QJsonObject McpToolHandler::toolKeymodFlash(const QJsonObject& args)
{
    const QString file = args.value("file").toString();
    const int index = args.value("device_index").toInt(0);

    if (file.isEmpty())
        return errorResult("Missing required argument 'file' (path to a .hex or .bin firmware file).");
    QFileInfo fi(file);
    if (!fi.isFile())
        return errorResult("Firmware file not found: " + file);
    const QString suffix = fi.suffix().toLower();
    if (suffix != "hex" && suffix != "bin")
        return errorResult(QString("Unsupported firmware file type '.%1' (expected .hex or .bin).").arg(suffix));

    WCHFlashWorker worker;
    QStringList devices;
    QStringList log;
    QString chipInfo;
    bool done = false;
    bool ok = false;
    QString resultMsg;

    connect(&worker, &WCHFlashWorker::devicesFound,
            [&devices](const QStringList& d) { devices = d; });
    connect(&worker, &WCHFlashWorker::deviceConnected,
            [&chipInfo](const QString& info) { chipInfo = info; });
    connect(&worker, &WCHFlashWorker::logMessage,
            [&log](const QString& m) { log << m; });
    connect(&worker, &WCHFlashWorker::finished,
            [&done, &ok, &resultMsg](bool success, const QString& msg) {
                done = true; ok = success; resultMsg = msg;
            });

    worker.scanDevices();
    if (devices.isEmpty()) {
        return errorResult(
            "No WCH ISP devices found; nothing flashed. Put the KeyMod in "
            "ISP/bootloader mode (hold BOOT while powering on) and confirm with "
            "keymod_scan first.\n" + log.join("\n"));
    }
    if (index < 0 || index >= devices.size()) {
        return errorResult(QString("device_index %1 out of range: %2 device(s) found. "
            "Run keymod_scan for the list.").arg(index).arg(devices.size()));
    }

    // connectDevice emits finished(false, ...) only on failure
    worker.connectDevice(index);
    if (done)
        return errorResult(resultMsg + "\n" + log.join("\n"));

    qCInfo(log_server_mcp_tool) << "KeyMod flash starting:" << file << "chip:" << chipInfo;
    worker.flashFirmware(file);

    if (!done)
        return errorResult("Flash ended without a result (internal error).\n" + log.join("\n"));
    if (!ok)
        return errorResult(resultMsg + "\n" + log.join("\n"));

    return textResult(QString(
        "%1\nChip: %2\n"
        "REQUIRED next step: power-cycle the device WITHOUT the BOOT button held; "
        "it then boots the new firmware.\n\nLog:\n%3")
        .arg(resultMsg, chipInfo, log.join("\n")));
}

// ==========================================================================
// Helpers
// ==========================================================================

QJsonObject McpToolHandler::textResult(const QString& text)
{
    QJsonObject content = McpProtocol::textContent(text);
    QJsonArray contents{ content };
    return McpProtocol::toolResult(contents);
}

QJsonObject McpToolHandler::errorResult(const QString& message)
{
    return McpProtocol::toolError(message);
}

int McpToolHandler::parseMouseButton(const QString& button)
{
    if (button == "right") return Qt::RightButton;
    if (button == "middle") return Qt::MiddleButton;
    return Qt::LeftButton;  // default
}
