# Tools Configuration Fix

## Issue
The cursor detection tool (`detect_cursor`) and web search tool (`web_search`) were implemented but not registered in the tools configuration system. This meant users could not see or configure these tools in the Preferences → AI Chat → Tools settings page.

## Root Cause
1. **Missing from GlobalSetting**: The `getChatAllToolsEnabled()` function only listed a subset of tools:
   - Had: capture_screen, screen_to_markdown, mouse tools, keyboard tools, recording tools, run_bash, set_target_system
   - Missing: `detect_cursor`, `web_search`

2. **Missing from Tools UI**: The `ToolsSettingsPage::populateToolsTree()` function had all tools except `detect_cursor` in the tree view.

## Solution

### 1. Updated `ui/globalsetting.cpp`
Added missing tools to the tool list:
```cpp
QStringList toolNames = {
    "capture_screen", "screen_to_markdown",
    "move_mouse", "left_click", "right_click", "double_click", "left_drag",
    "type_text", "press_key", "repeat_key",
    "start_recording", "stop_recording",
    "run_bash", "set_target_system",
    "detect_cursor", "web_search"  // Added
};
```

### 2. Updated `ui/chat/ToolsSettingsPage.cpp`
Added `detect_cursor` to the System/Host Tools group:
```cpp
QStandardItem *detectCursor = new QStandardItem(tr("Detect Cursor (Terminal Idle)"));
detectCursor->setCheckable(true);
detectCursor->setCheckState(Qt::Checked);
detectCursor->setEditable(false);
detectCursor->setData("detect_cursor", Qt::UserRole + 1);
detectCursor->setToolTip(tr("Detect whether the target terminal is idle and waiting for input"));

QStandardItem *detectCursorDesc = new QStandardItem("detect_cursor");
detectCursorDesc->setEditable(false);
detectCursorDesc->setForeground(QColor(128, 128, 128));

systemGroup->appendRow({setTargetSystem, setTargetSystemDesc});
systemGroup->appendRow({runBash, runBashDesc});
systemGroup->appendRow({webSearch, webSearchDesc});
systemGroup->appendRow({detectCursor, detectCursorDesc});  // Added
```

## Tool Aliases
The codebase supports multiple aliases for the same tool for flexibility:

### detect_cursor
- Primary: `detect_cursor`
- Aliases: `terminal_idle`, `check_terminal`, `wait_for_prompt`
- Function: Detects if terminal is idle/waiting for input using cursor blink detection

### web_search
- Primary: `web_search`
- Aliases: `search`, `internet_search`
- Function: Searches the internet using multi-provider fallback (Exa, Parallel, DuckDuckGo, Wikipedia)

## Verification

### Before Fix
- Tools not visible in Preferences → AI Chat → Tools
- Users could not enable/disable these tools
- Tools worked but were not configurable

### After Fix
- Both tools visible in System/Host Tools group
- Users can enable/disable via checkboxes
- Tool IDs displayed: `detect_cursor`, `web_search`
- Settings persist across application restarts

## Files Modified
1. `ui/globalsetting.cpp` - Added tools to getChatAllToolsEnabled()
2. `ui/chat/ToolsSettingsPage.cpp` - Added detect_cursor to UI tree

## Testing
1. Open Preferences → AI Chat → Tools
2. Verify "System/Host Tools" group shows:
   - Set Target System
   - Run Bash (Host)
   - Web Search
   - Detect Cursor (Terminal Idle) ← NEW
3. Toggle each tool and verify settings persist
4. Use AI chat to test:
   - "detect_cursor" - should check if terminal is idle
   - "web_search" - should search internet

## Commit
- Commit: `5b0c3e6`
- Branch: `feature/cursor-blink-detection`
- Date: 2026-09-03
- Changes: 46 files, 5154 insertions(+), 1011 deletions(-)

## Status
✅ **FIXED** - All tools now properly registered and configurable in UI
