# Virtual Keyboard Redesign Design Spec

**Date**: 2026-08-11  
**Status**: Draft  
**Approach**: B (Redesign UI)

## Overview

Redesign the virtual keyboard configuration interface to improve usability and ensure correct key sending to the target machine. The new interface will be integrated into Advanced Settings as a "Virtual Keyboard" page.

## Core Requirements

### 1. Key Sending Correctness (Priority)

Before UI redesign, ensure all key sending mechanisms work correctly:

#### Modifier Toggle Buttons
- **Location**: Toolbar (always visible at the start)
- **Buttons**: Ctrl, Alt, Shift, Win
- **Behavior**:
  - Click to toggle on (highlighted state)
  - Click a regular key button → modifier is applied to that key press
  - Modifier auto-unchecks after key press (one-shot behavior)
  - Multiple modifiers can be toggled simultaneously (e.g., Ctrl+Alt)

#### Custom Key Buttons
- **Source**: Configured via `default.json` or user customization
- **Behavior**:
  - Single click sends the configured key combination
  - If modifier toggles are active, they are prepended to the key combination
  - Key press sequence: press modifiers → press regular keys → release regular keys → release modifiers

#### Key Combination Sending
- **Implementation**: `HostManager::handleKeyCombo(const QList<int>& keyCodes)`
- **Sequence**:
  1. Separate modifiers and regular keys from keyCodes
  2. Press modifier keys (in order)
  3. Press regular keys
  4. Wait 50ms
  5. Release regular keys
  6. Release modifier keys (in reverse order)

### 2. UI Redesign (Secondary)

#### Layout
- **Location**: Advanced Settings → "Virtual Keyboard" page
- **Left Panel**: Available keys library (categorized: Function keys, Special keys, Lock keys, etc.)
- **Right Panel**: Toolbar preview (shows current configuration)
- **Bottom**: Action buttons (Add, Remove, Move Up/Down, Import, Export, Reset)

#### Features
- **Drag and Drop**: Drag keys from left panel to right preview
- **Reordering**: Drag keys within preview to reorder
- **Modifier Lock**: Modifier buttons (Ctrl/Alt/Shift/Win) always appear first and cannot be removed
- **Live Preview**: Changes in configuration immediately reflect in toolbar preview
- **Import/Export**: JSON format for sharing configurations
- **Presets**: Save/load named configurations

#### Toolbar Behavior
- **Visibility**: Toolbar is hidden by default, can be toggled via menu or shortcut
- **Layout**: Single row with horizontal scrolling if needed (prevent wrapping)
- **Modifier Buttons**: Always visible at the start, highlighted with distinct color
- **Custom Keys**: Loaded from configuration, displayed after modifiers

### 3. Integration Points

#### Advanced Settings
- Add "Virtual Keyboard" tree item in `AdvancedSettingsDialog`
- Create `VirtualKeyboardPage` widget that embeds the configuration UI
- Connect toolbar ⚙ button to open Advanced Settings → Virtual Keyboard page

#### Data Flow
```
User clicks key button in toolbar
  ↓
ToolbarManager::onKeyButtonClicked()
  ↓
Collect active modifier toggles
  ↓
Get keyCodes from button property
  ↓
If modifiers active: prepend modifier keyCodes
  ↓
HostManager::handleKeyCombo(combinedKeyCodes)
  ↓
KeyboardManager sends to target via serial
```

## Implementation Phases

### Phase 1: Verify Key Sending (Current)
- [ ] Test modifier toggle buttons work correctly
- [ ] Test custom key buttons send correct keys
- [ ] Test modifier + key combinations work
- [ ] Fix any issues found

### Phase 2: UI Redesign
- [ ] Create `VirtualKeyboardPage` class
- [ ] Implement left panel (available keys library)
- [ ] Implement right panel (toolbar preview)
- [ ] Implement drag-and-drop functionality
- [ ] Add to Advanced Settings dialog
- [ ] Test integration with toolbar

### Phase 3: Polish
- [ ] Add import/export functionality
- [ ] Add preset management
- [ ] Add reset to default option
- [ ] Improve visual styling
- [ ] Add tooltips and help text

## Files to Modify

- `ui/toolbar/toolbarmanager.cpp` - Modifier toggle logic, key button handler
- `ui/toolbar/toolbarmanager.h` - Add ModifierInfo struct
- `config/customkeys/default.json` - Default key configuration
- `ui/preferences/advancedsettingsdialog.cpp` - Add Virtual Keyboard page
- `ui/preferences/advancedsettingsdialog.h` - Add VirtualKeyboardPage member
- `ui/customkey/virtualkeyboardpage.cpp` (new) - Configuration page UI
- `ui/customkey/virtualkeyboardpage.h` (new) - Configuration page class

## Testing Checklist

- [ ] Modifier toggle buttons highlight when clicked
- [ ] Modifier toggle buttons auto-uncheck after key press
- [ ] Single key press sends correct key code
- [ ] Modifier + key sends correct combination (e.g., Ctrl+F1)
- [ ] Multiple modifiers + key works (e.g., Ctrl+Alt+Del)
- [ ] Toolbar wraps correctly when too many keys
- [ ] Configuration changes persist across app restart
- [ ] Import/export JSON works correctly
- [ ] Preset save/load works correctly

## Notes

- The current implementation uses `handleKeyCombo` which has a 50ms delay between press and release
- Modifier keys are represented as keyCodes (Qt::Key_Control, etc.) in the keyCodes list
- The toolbar uses Qt's property system to store keyCodes on each button
- CustomKeyManager handles loading/saving configurations to JSON files
