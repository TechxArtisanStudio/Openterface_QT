# MSYS2 路径问题修复说明

## 问题描述

构建过程中出现以下错误:
```
mingw32-make[2]: *** No rule to make target '/lib/libbz2.a', needed by 'openterfaceQT.exe'.  Stop.
```

同时 g++ 编译器路径出现重复:
```
C:\msys64\msys64\mingw64\bin\g++.exe
```

## 根本原因

1. **路径重复**: 在某些步骤中,MSYS2 路径被错误地拼接,导致 `msys64` 出现两次
2. **无效的绝对路径**: Qt CMake 配置文件中包含 `/lib/libbz2.a` 这样的无效路径
3. **MINGW_ROOT 未设置**: CMake 配置中 `MINGW_ROOT` 变量未正确传递

## 修复方案

### 1. 统一 MSYS2 路径处理 (windows-portable-build.yaml)

在 MSYS2 shell 中:
- 始终使用 `/mingw64` (POSIX 风格路径)
- 传递给 CMake 时使用 `cygpath -w` 转换为 `C:\msys64\mingw64` (Windows 风格)

```yaml
# Use consistent MSYS2 path - always /mingw64 within MSYS2 shell
MSYS_MINGW_PATH="/mingw64"

# Convert to Windows path for CMake
CMAKE_MINGW_PATH=$(cygpath -w "$MSYS_MINGW_PATH")
```

### 2. 移除冗余的环境变量设置

删除了所有可能导致路径重复的 `MINGW_ROOT` 环境变量设置步骤,避免路径被错误拼接。

### 3. 明确传递 MINGW_ROOT 给 CMake

在 CMake 配置中添加:
```yaml
-DMINGW_ROOT="${CMAKE_MINGW_PATH}"
```

### 4. 修复 Qt CMake 配置文件 (新增步骤)

添加 PowerShell 脚本扫描并修复 Qt6 中的 CMake 配置文件:
- 将 `/lib/lib*.a` 修复为 `C:/msys64/mingw64/lib/lib*.a`
- 将 `C:/msys64/msys64/mingw64` 修复为 `C:/msys64/mingw64`

### 5. 增强 FFmpeg.cmake (cmake/FFmpeg.cmake)

添加 `MINGW_ROOT` 默认值和验证:
```cmake
# Ensure MINGW_ROOT is set for Windows builds
if(WIN32 AND NOT DEFINED MINGW_ROOT)
    if(DEFINED ENV{MINGW_ROOT})
        set(MINGW_ROOT "$ENV{MINGW_ROOT}" CACHE PATH "MinGW root directory")
    else()
        set(MINGW_ROOT "C:/msys64/mingw64" CACHE PATH "MinGW root directory")
    endif()
endif()
```

添加库文件存在性检查,早期发现路径问题:
```cmake
set(_REQUIRED_LIBS
    "${MINGW_ROOT}/lib/libbz2.a"
    "${MINGW_ROOT}/lib/liblzma.a"
    "${MINGW_ROOT}/lib/libwinpthread.a"
)
foreach(_lib ${_REQUIRED_LIBS})
    if(NOT EXISTS "${_lib}")
        message(WARNING "Required library not found: ${_lib}")
    endif()
endforeach()
```

### 6. 添加诊断输出

在 CMake 配置后显示链接库路径,便于调试:
```bash
find CMakeFiles -name "link.txt" -o -name "linkLibs.rsp" | while read f; do
    echo "--- $f ---"
    cat "$f" | head -100
done
```

## 验证步骤

1. 检查 CMakeCache.txt 中没有 `/lib/lib` 或 `/msys64/msys64` 模式
2. 检查 link.txt 文件中所有库路径都是有效的 Windows 路径
3. 确认所有 `${MINGW_ROOT}` 引用都被正确替换为 `C:\msys64\mingw64`

## 关键要点

- **统一路径格式**: MSYS2 内部用 POSIX,传给 CMake 用 Windows 格式
- **避免环境变量污染**: 不依赖 `$MINGW_ROOT` 环境变量,使用显式 CMake 参数
- **早期验证**: 在 CMake 配置时验证库文件存在,而不是等到链接阶段才失败
- **修复历史遗留问题**: Qt 构建可能已经包含错误路径,需要事后修复
