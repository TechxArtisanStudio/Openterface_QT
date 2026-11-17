# Keyboard Layout Configuration Guide

This document provides detailed instructions on how to add and configure new keyboard layouts in the openterfaceQt.

## File Structure

Keyboard layout configuration consists of two main components:
1. Resource configuration file
2. Keyboard layout JSON file

### Resource Configuration File

All keyboard layout files must be registered in **both** of the following files:

1. **Qt Resource file** (`config/keyboards/keyboard_layouts.qrc`) — used by qmake builds:
```xml
<!DOCTYPE RCC>
<RCC version="1.0">
    <qresource prefix="/config/keyboards">
        <file>your_keyboard_layout.json</file>
    </qresource>
</RCC>
```

2. **CMake resource list** (`cmake/Resources.cmake`) — used by CMake builds:
```cmake
set(keyboard_layouts_resource_files
    ...
    "config/keyboards/your_keyboard_layout.json"
)
```

> **Note:** If you only register in one file, the layout will be missing in builds using the other build system.

### Keyboard Layout JSON File

Keyboard layout files (`.json`) should be stored in the `config/keyboards` directory and follow this structure:

```json
{
    "name": "Display name of the keyboard layout",
    "right_to_left": false,
    "key_map": {},
    "char_mapping": {},
    "unicode_map": {},
    "need_shift_keys": [],
    "need_altgr_keys": []
}
```

## Configuration Details

### 1. Basic Information
- `name`: The name of the keyboard layout displayed in the software interface.
- `right_to_left`: Indicates whether the layout is for a right-to-left writing system.

### 2. Key Mapping (`key_map`)
Defines the mapping between Qt keys and HID codes:
```json
"key_map": {
    "Key_A": "0x04",
    "Key_B": "0x05"
}
```

### 3. Character Mapping (`char_mapping`)
Defines the mapping between characters and Qt keys. Values can be:

- **Single key name**: `"a": "Key_A"` — sends the key with Shift if the character is uppercase or listed in `need_shift_keys`, or AltGr if listed in `need_altgr_keys`.
- **Key name with modifier prefix**: `"€": "AltGr+Key_E"` — explicitly sets modifiers (useful when the character requires AltGr but you don't want to list it in `need_altgr_keys`).
- **Array of steps (dead key sequence)**: `"Ä": ["Key_Dead_Diaeresis", "Shift+Key_A"]` — sends each step as a separate key press/release. Used for characters that require a dead key followed by a base character (e.g., uppercase umlauts on Swiss German keyboards).

```json
"char_mapping": {
    "a": "Key_A",
    "A": "Key_A",
    "€": "AltGr+Key_E",
    "Ä": ["Key_Dead_Diaeresis", "Shift+Key_A"],
    "Ö": ["Key_Dead_Diaeresis", "Shift+Key_O"],
    "^": ["Key_Dead_Circumflex", "Key_Space"]
}
```

Modifier prefixes: `Shift+`, `AltGr+` (can be combined: `Shift+AltGr+Key_X`).

### 4. Unicode Mapping (`unicode_map`)
Maps special (non-qtKey) Unicode characters to HID codes:
```json
"unicode_map": {
    "U+20AC": "0x08",  // € symbol
    "U+00B5": "0x10"   // µ symbol
}
```

### 5. Special Key Configurations
- `need_shift_keys`: A list of characters that require the Shift key to be pressed.
- `need_altgr_keys`: A list of characters that require the AltGr key to be pressed.

## Example Files

- **German QWERTZ**: `config/keyboards/qwertz_de.json` — standard PC layout with AltGr characters.
- **Swiss German QWERTZ**: `config/keyboards/qwertz_ch.json` — includes dead key sequences for uppercase umlauts (Ä, Ö, Ü) and dead key literals (^, `, ´, ~, ¨).

## Notes

1. All HID codes must be in hexadecimal format (starting with `0x`).
2. Ensure all necessary mappings are defined.
3. Unicode mapping is only for non-qtKey characters.
4. Special key configurations must include all characters requiring key combinations.

## Validation Steps

1. Ensure the JSON file format is correct.
2. Verify that all required mappings are included.
3. Test the input of special characters.
4. Confirm that key combinations work as expected.