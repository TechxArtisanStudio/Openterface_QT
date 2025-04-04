# Keyboard Layout Configuration Guide

This document provides detailed instructions on how to add and configure new keyboard layouts in the openterfaceQt.

## File Structure

Keyboard layout configuration consists of two main components:
1. Resource configuration file
2. Keyboard layout JSON file

### Resource Configuration File

All keyboard layout files must be registered in the name: "keyboard_layouts.qrc" path: "Openterface_QT\config\keyboards\keyboard_layouts.qrc" file:

```xml
<!DOCTYPE RCC>
<RCC version="1.0">
    <qresource prefix="/config/keyboards">
        <file>your_keyboard_layout.json</file>
    </qresource>
</RCC>
```

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
Defines the mapping between characters and Qt keys:
```json
"char_mapping": {
    "a": "Key_A",
    "A": "Key_A"
}
```

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

## Example File

Refer to name: "qwertz_de.json" path: "Openterface_QT\config\keyboards\qwertz_de.json" for a complete implementation of the German QWERTZ keyboard layout.

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