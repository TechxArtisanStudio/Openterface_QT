# Send 命令按键语法指南

## 概述
Send 命令用于模拟键盘输入。它支持 AutoHotkey (AHK) 风格的语法，包括修饰符前缀和特殊按键。

---

## 修饰符前缀

### 基本修饰符
| 符号 | 含义 | 示例 | 效果 |
|------|------|------|------|
| `^` | Ctrl | `Send "^c"` | Ctrl+C (复制) |
| `!` | Alt | `Send "!{F4}"` | Alt+F4 (关闭窗口) |
| `+` | Shift | `Send "+{Tab}"` | Shift+Tab |
| `#` | Win | `Send "#d"` | Win+D (显示桌面) |

### 组合多个修饰符
可以组合多个修饰符前缀：

```ahk
Send "^+s"          ; Ctrl+Shift+S (另存为)
Send "^!{Delete}"   ; Ctrl+Alt+Delete
Send "#+m"          ; Win+Shift+M
```

**重要**: 修饰符前缀只影响紧跟其后的一个字符或花括号按键。

---

## 特殊按键（花括号格式）

使用花括号 `{}` 包裹特殊按键名称：

### 常用特殊按键
```ahk
Send "{Enter}"      ; 回车键
Send "{Tab}"        ; Tab 键
Send "{Space}"      ; 空格键
Send "{Backspace}"  ; 退格键 (也可以用 {BS})
Send "{Delete}"     ; Delete 键 (也可以用 {Del})
Send "{Escape}"     ; Esc 键 (也可以用 {Esc})

; 方向键
Send "{Up}"         ; 上箭头
Send "{Down}"       ; 下箭头
Send "{Left}"       ; 左箭头
Send "{Right}"      ; 右箭头

; 功能键
Send "{F1}"         ; F1
Send "{F12}"        ; F12

; 其他
Send "{Home}"       ; Home 键
Send "{End}"        ; End 键
Send "{PgUp}"       ; Page Up
Send "{PgDn}"       ; Page Down
Send "{Insert}"     ; Insert 键 (也可以用 {Ins})
```

### 特殊按键 + 修饰符
```ahk
Send "^{Enter}"     ; Ctrl+Enter
Send "+{Tab}"       ; Shift+Tab
Send "!{F4}"        ; Alt+F4
Send "^+{Home}"     ; Ctrl+Shift+Home
```

---

## 普通文本输入

### 小写字母和数字
直接输入即可：
```ahk
Send "hello"        ; 输入 hello
Send "test123"      ; 输入 test123
```

### 大写字母
有两种方式：

**方式 1: 直接使用大写字母**（推荐）
```ahk
Send "Hello"        ; 自动添加 Shift: 输入 Hello
Send "TEST"         ; 自动添加 Shift: 输入 TEST
```

**方式 2: 显式使用 Shift 前缀**
```ahk
Send "+h+e+l+l+o"   ; 等效于 "Hello"
```

---

## 输入字面的修饰符符号

要输入修饰符符号本身（`^`、`!`、`+`、`#`），使用**反引号转义**：

### 反引号转义语法
| 想输入的字符 | 命令 | 说明 |
|-------------|------|------|
| `^` | `Send "``^"` | 输入 ^ 字符 (Shift+6) |
| `!` | `Send "``!"` | 输入 ! 字符 (Shift+1) |
| `+` | `Send "``+"` | 输入 + 字符 (Shift+=) |
| `#` | `Send "``#"` | 输入 # 字符 (Shift+3) |
| `` ` `` | `Send "``````"` | 输入反引号字符本身 |

**注意**: 在双引号字符串中，反引号需要用两个 `` ` `` 表示一个反引号。

### 其他需要转义的字符
```ahk
Send "``n"          ; 输入换行符 (Enter)
Send "``t"          ; 输入 Tab 字符
Send "``r"          ; 输入回车符 (Enter)
```

---

## 完整示例

### 示例 1: 复制粘贴操作
```ahk
; 全选、复制、移动到行尾、粘贴
Send "^a"           ; Ctrl+A (全选)
Sleep 100
Send "^c"           ; Ctrl+C (复制)
Sleep 100
Send "{End}"        ; 移动到行尾
Send "^v"           ; Ctrl+V (粘贴)
```

### 示例 2: 输入复杂文本
```ahk
; 输入: email@example.com
Send "email"
Send "`!"           ; 输入 ! (使用反引号转义)
Send "example.com"
```

### 示例 3: 快捷键序列
```ahk
; 打开"另存为"对话框并输入文件名
Send "^+s"          ; Ctrl+Shift+S (另存为)
Sleep 500
Send "myfile.txt"   ; 输入文件名
Send "{Enter}"      ; 确认
```

### 示例 4: 文本格式化
```ahk
; 输入 "Hello World!" 并加粗
Send "Hello World"
Send "`!"           ; 输入 ! 符号
Sleep 100
Send "^a"           ; 全选
Send "^b"           ; Ctrl+B (加粗)
```

---

## 常见错误和解决方法

### ❌ 错误 1: 孤立的修饰符
```ahk
Send "^"            ; ❌ 错误: 修饰符后没有按键
```
**效果**: 不发送任何按键，并在日志中显示错误

**正确做法**:
```ahk
Send "``^"          ; ✅ 输入字面的 ^ 字符
; 或
Send "^c"           ; ✅ 发送 Ctrl+C
```

### ❌ 错误 2: 混淆修饰符前缀和字面符号
```ahk
Send "^+c"          ; 这是 Ctrl+Shift+C，不是输入 "^+c" 三个字符
```

**正确做法**: 使用反引号转义
```ahk
Send "``^``+c"      ; ✅ 输入字面的 "^+c"
```

### ❌ 错误 3: 修饰符只影响一个字符
```ahk
Send "^ab"          ; 这会发送: Ctrl+A, 然后 b (不是 Ctrl+A+B)
```

**正确做法**: 每个组合键都需要单独的修饰符
```ahk
Send "^a^b"         ; ✅ Ctrl+A, 然后 Ctrl+B
```

### ❌ 错误 4: 花括号内不能嵌套修饰符
```ahk
Send "{^c}"         ; ❌ 错误: 不支持这种格式
```

**正确做法**: 修饰符在花括号外
```ahk
Send "^{c}"         ; ✅ Ctrl+C (虽然这个例子中 c 不需要花括号)
Send "^{Delete}"    ; ✅ Ctrl+Delete
```

---

## 调试技巧

### 启用调试日志
在代码中启用 `log_script` 日志类别，可以看到详细的按键解析过程：

```
[opf.scripts] Processing keys: "^+s"
[opf.scripts] Added modifier prefix at pos 0: ^ (control now: 0x01)
[opf.scripts] Added modifier prefix at pos 1: + (control now: 0x03)
[opf.scripts] Added char press: s (HID: 0x16, modifiers: 0x03)
[opf.scripts] Send: sending 2 packets
```

### 日志中的控制字节含义
- `0x01` = Ctrl
- `0x02` = Shift
- `0x04` = Alt
- `0x08` = Win
- `0x03` = Ctrl + Shift (0x01 | 0x02)
- `0x07` = Ctrl + Shift + Alt (0x01 | 0x02 | 0x04)

---

## 参考

### 修饰符控制字节
- Ctrl: 0x01
- Shift: 0x02
- Alt: 0x04
- Win: 0x08

### HID Usage ID (常用按键)
- a-z: 0x04-0x1D
- 0-9: 0x27, 0x1E-0x26
- Enter: 0x28
- Escape: 0x29
- Backspace: 0x2A
- Tab: 0x2B
- Space: 0x2C
- Delete: 0x4C

完整的 HID Usage ID 表可以在 USB HID Usage Tables 规范中找到。

---

## 更新历史

- **2026-06-02**: 
  - 修复了 keydata 中的冲突定义
  - 添加了孤立修饰符检测和错误提示
  - 改进了调试日志输出
  - 完善了用户文档
