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

#ifndef MCP_CONSTANTS_H
#define MCP_CONSTANTS_H

// MCP Protocol Version
#define MCP_PROTOCOL_VERSION "2024-11-05"

// MCP Server Info
#define MCP_SERVER_NAME "openterface-kvm"
#define MCP_SERVER_VERSION "1.0.0"

// JSON-RPC 2.0 Constants
#define JSONRPC_VERSION "2.0"

// MCP Method Names
#define MCP_METHOD_INITIALIZE              "initialize"
#define MCP_METHOD_INITIALIZED             "notifications/initialized"
#define MCP_METHOD_TOOLS_LIST              "tools/list"
#define MCP_METHOD_TOOLS_CALL              "tools/call"
#define MCP_METHOD_TOOLS_LIST_CHANGED      "notifications/tools/list_changed"
#define MCP_METHOD_PING                    "ping"

// JSON-RPC Error Codes
#define JSONRPC_ERROR_PARSE_ERROR          -32700
#define JSONRPC_ERROR_INVALID_REQUEST      -32600
#define JSONRPC_ERROR_METHOD_NOT_FOUND     -32601
#define JSONRPC_ERROR_INVALID_PARAMS       -32602
#define JSONRPC_ERROR_INTERNAL_ERROR       -32603

// MCP Tool Names
#define MCP_TOOL_MOUSE_MOVE_ABSOLUTE       "mouse_move_absolute"
#define MCP_TOOL_MOUSE_CLICK               "mouse_click"
#define MCP_TOOL_MOUSE_MOVE_RELATIVE       "mouse_move_relative"
#define MCP_TOOL_MOUSE_SCROLL              "mouse_scroll"
#define MCP_TOOL_KEYBOARD_PRESS_KEY        "keyboard_press_key"
#define MCP_TOOL_KEYBOARD_TYPE_TEXT        "keyboard_type_text"
#define MCP_TOOL_KEYBOARD_SEND_KEYS        "keyboard_send_keys"
#define MCP_TOOL_KEYBOARD_FUNCTION_KEY     "keyboard_function_key"
#define MCP_TOOL_KEYBOARD_CTRL_ALT_DEL     "keyboard_ctrl_alt_del"
#define MCP_TOOL_KEYBOARD_SET_LAYOUT       "keyboard_set_layout"
#define MCP_TOOL_CAPTURE_SCREEN            "capture_screen"
#define MCP_TOOL_CAPTURE_LAST_IMAGE        "capture_last_image"
#define MCP_TOOL_EXECUTE_SCRIPT            "execute_script"
#define MCP_TOOL_VALIDATE_SCRIPT           "validate_script"
#define MCP_TOOL_SYSTEM_STATUS             "system_status"
#define MCP_TOOL_USB_SWITCH                "usb_switch"

// Default Named Pipe Name
#define MCP_DEFAULT_PIPE_NAME "openterface-mcp"

// SSE Transport
#define MCP_SSE_DEFAULT_PORT        8080
#define MCP_SSE_PATH_SSE            "/sse"
#define MCP_SSE_PATH_MESSAGES       "/messages"
#define MCP_SSE_CONTENT_TYPE        "text/event-stream"
#define MCP_SSE_KEEPALIVE_INTERVAL  15000       // 15 seconds
#define MCP_SSE_SESSION_TIMEOUT_MS  1800000     // 30 minutes
#define MCP_SSE_CLEANUP_INTERVAL    60000       // 60 seconds
#define MCP_SSE_MAX_SESSIONS        16

#endif // MCP_CONSTANTS_H
