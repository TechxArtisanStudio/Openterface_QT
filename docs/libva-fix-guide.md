# libva 符号冲突修复指南

## 问题描述

运行 openterfaceQT 时出现以下错误：
```
./openterfaceQT: symbol lookup error: /lib64/libavutil.so.59: undefined symbol: vaMapBuffer2
```

## 问题原因

1. **旧版本 RPM 打包问题**：RPM 包将旧版本的 libva（2.1400.0）打包到 `/usr/lib/openterfaceqt/`
2. **ldconfig 优先级问题**：`/etc/ld.so.conf.d/openterface-libs.conf` 让打包的 libva 优先于系统 libva 加载
3. **符号不兼容**：旧版 libva（2.1400.0）缺少 `vaMapBuffer2` 符号，但系统版 libavutil.so.59 需要这个符号

## 解决方案

### 方案 1：立即修复当前系统（推荐）

运行清理脚本：

```bash
sudo ./scripts/cleanup-old-libva.sh
```

这个脚本会：
- 备份旧的打包 libva 库
- 删除 `/usr/lib/openterfaceqt/` 下的 libva 库
- 更新 ldconfig 缓存
- 验证系统 libva 是否可用

### 方案 2：手动修复

```bash
# 1. 删除遗留的 ldconfig 配置文件
sudo rm -f /etc/ld.so.conf.d/openterface-libs.conf

# 2. 删除打包的 libva 库
sudo rm -f /usr/lib/openterfaceqt/libva*.so*

# 3. 更新 ldconfig 缓存
sudo ldconfig

# 4. 验证修复
ldconfig -p | grep "libva.so.2"
```

### 方案 3：使用启动脚本（临时方案）

启动脚本已经配置了 `LD_PRELOAD` 来预加载系统 libva：

```bash
cd build
./openterfaceQT-launcher.sh
```

## 根本修复（已应用）

以下修改已经应用到代码库，防止问题再次发生：

### 1. 构建脚本修改 (`build-script/docker-build-rpm.sh`)

不再打包 libva 库，改为使用系统库：

```bash
# Hardware acceleration libraries are NOT bundled - use system libraries instead
# Bundling libva causes symbol incompatibilities with system libavutil (e.g., vaMapBuffer2)
# Users must install system libva packages: libva, libva-drm, libva-x11
# "VA|VA-API|libva.so|WARNING||/usr/lib/x86_64-linux-gnu|/usr/lib"
# "VADRM|VA-API DRM|libva-drm.so|WARNING||/usr/lib/x86_64-linux-gnu|/usr/lib"
# "VAX11|VA-API X11|libva-x11.so|WARNING||/usr/lib/x86_64-linux-gnu|/usr/lib"
```

### 2. RPM Spec 文件修改 (`packaging/rpm/spec`)

- **添加 libva 为系统依赖**：
  ```spec
  Requires:       libva
  Requires:       libva-drm
  Requires:       libva-x11
  ```

- **移除打包 libva 的代码**：
  ```spec
  # Hardware acceleration libraries - use system libraries, do NOT bundle
  # libva is now a system dependency (see Requires: section above)
  ```

- **移除创建 libva 符号链接的代码**

### 3. 安装脚本修改 (`docker/install-openterface.sh`)

安装时自动删除遗留的 `openterface-libs.conf` 文件（第 622-636 行）

## 系统要求

修复后，系统必须安装 libva 库：

### Fedora/RHEL
```bash
sudo dnf install libva libva-drm libva-x11
```

### Ubuntu/Debian
```bash
sudo apt install libva2 libva-drm2 libva-x11-2
```

### Arch Linux
```bash
sudo pacman -S libva libva-utils
```

## 验证修复

1. **检查 libva 库位置**：
   ```bash
   ldconfig -p | grep libva.so.2
   ```
   应该显示系统库路径（如 `/usr/lib64/libva.so.2`），而不是 `/usr/lib/openterfaceqt/`

2. **检查 vaMapBuffer2 符号**：
   ```bash
   nm -D /usr/lib64/libva.so.2 | grep vaMapBuffer2
   ```
   应该显示 `vaMapBuffer2` 符号

3. **运行应用**：
   ```bash
   ./build/openterfaceQT
   ```
   应该正常启动，不再出现符号错误

## 技术细节

### 为什么不再打包 libva？

1. **系统级库**：libva 是硬件加速库，与系统 GPU 驱动紧密相关
2. **符号兼容性**：不同版本的 libva 可能有不同的符号集
3. **依赖关系**：系统 libavutil 依赖系统 libva 的特定符号
4. **维护成本**：打包 libva 需要跟踪系统更新，维护成本高

### 为什么使用系统 libva？

1. **版本一致**：系统 libva 与系统 libavutil 版本匹配
2. **驱动兼容**：系统 libva 与系统 GPU 驱动版本匹配
3. **自动更新**：系统包管理器自动处理更新
4. **减少冲突**：避免多个版本的库同时存在

## 故障排除

### 问题：应用启动后提示找不到 libva

**解决方案**：安装系统 libva 包
```bash
# Fedora
sudo dnf install libva libva-drm libva-x11

# Ubuntu
sudo apt install libva2 libva-drm2 libva-x11-2
```

### 问题：仍然出现 vaMapBuffer2 错误

**解决方案**：
1. 确认已删除所有打包的 libva 库：
   ```bash
   ls -la /usr/lib/openterfaceqt/libva*
   ```
   应该为空或不存在

2. 确认 ldconfig 缓存已更新：
   ```bash
   sudo ldconfig
   ```

3. 确认系统 libva 有 vaMapBuffer2 符号：
   ```bash
   nm -D /usr/lib64/libva.so.2 | grep vaMapBuffer2
   ```

### 问题：需要恢复旧的打包 libva

**解决方案**：从备份恢复
```bash
sudo cp /usr/lib/openterfaceqt/backup-libva-*/*.so* /usr/lib/openterfaceqt/
sudo ldconfig
```

注意：这会导致原来的问题重新出现，仅用于调试。

## 相关文件

- 构建脚本：`build-script/docker-build-rpm.sh`
- RPM Spec：`packaging/rpm/spec`
- 安装脚本：`docker/install-openterface.sh`
- 清理脚本：`scripts/cleanup-old-libva.sh`
- 启动脚本：`build/openterfaceQT-launcher.sh`

## 更新日志

- 2026-08-18：移除 libva 打包，改为系统依赖
- 2026-08-18：添加清理脚本
- 2026-08-18：更新 RPM spec 文件
- 2026-08-18：更新构建脚本
