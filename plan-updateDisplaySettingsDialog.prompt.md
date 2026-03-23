## Plan: 拆分 UpdateDisplaySettingsDialog 模块

TL;DR: 将 `ui/advance/updatedisplaysettingsdialog.cpp` / `.h` 里混合编写的 UI、固件读取、EDID 解析和分辨率逻辑拆成 3~4 个明确职责的类；从 `RenameDisplayDialog` 复用已有逻辑，降低冗余并增强测试可维护性。

步骤
1. 评估现状
   - 目前该类职责过多：UI + 进度 + 固件读写 + EDID 解析 + 解析延展块 + 分辨率表维护 + 校验计算。
   - `RenameDisplayDialog` 有很多重复功能（findEDIDBlock0/updateEDIDDisplayName/calc checksum），应复用到通用工具类。

2. 提案模块拆分
   - `EDIDUtils`（新文件）
     - `findEDIDBlock0`, `parseEDIDDescriptors`, `updateEDIDDisplayName`, `updateEDIDSerialNumber`, `calculateEDIDChecksum`, `showEDIDDescriptors`, `showFirmwareHexDump`等纯逻辑函数。
     - 方便单测 `EDID` 处理。
   - `FirmwareUtils`（新文件）
     - `calculateFirmwareChecksumWithDiff`, `processEDIDDisplaySettings` (部分) ; 读取/写入固件数据的转换、校验逻辑。
   - `EDIDResolutionParser`（新文件）
     - 所有与分辨率解析相关函数：`parseStandardTimingsForResolutions`, `parseDetailedTimingDescriptorsForResolutions`, `parseExtensionBlocksForResolutions`, `parseCEA861ExtensionBlockForResolutions`, `parseVideoDataBlockForResolutions`, `getVICResolutionInfo`, `getVICResolution`。
     - `ResolutionInfo` 结构体也可移到该模块下。
   - 保留 `/ui/advance/updatedisplaysettingsdialog.{h,cpp}` 只负责 UI、信号槽、与工具类通信。

3. 重构路线（推荐按模块逐个迁移）
   - 先在 `ui/advance/` 新建 `edidutils.h/.cpp`, `firmwareutils.h/.cpp`, `edidresolutionparser.h/.cpp`。
   - 在 `.h` 里移除对应函数声明，改为 `#include` 工具头与成员/静态调用。
   - 逐步在 `updatedisplaysettingsdialog.cpp` 中将逻辑迁移：先取消重复实现，后改用工具函数。
   - 同时按照现有项目样式补充 doxygen/注释、`namespace EDID`。

4. 兼容 `RenameDisplayDialog` 复用
   - 让 `RenameDisplayDialog` 调用同一个 `EDIDUtils::...` 处理所有 EDID/校验/查找逻辑。
   - `RenameDisplayDialog` 仅保留简单新名输入+校验+UI对话。

5. 测试与验证
   - 编写单元测试：`tests/test_edidutils.cpp`, `tests/test_edidresolutionparser.cpp`。
   - 手工验证：在 UI 里加载 EDID、修改名称/序列号、开启/关闭分辨率，检查固件更新是否按预期。
   - 对比已有变更前后输出，确保 `calculateFirmwareChecksumWithDiff` 前后一致。

6. 代码审查
   - 以小 PR 提交，各阶段分别：
      a. 提取 `EDIDUtils` + 让旧类转发；
      b. 提取 `EDIDResolutionParser`；
      c. 提取 `FirmwareUtils`; 
      d. 清理 `updatedisplaysettingsdialog` 只剩 UI 驱动。

7. 项目结构与构建配置
   - 新文件夹：建议 `ui/advance/edid/`（或 `ui/advance/utils/`）
   - 文件列表：`edidutils.h/.cpp`, `firmwareutils.h/.cpp`, `edidresolutionparser.h/.cpp`
   - 在 `CMakeLists.txt` 增加 `add_library` / `target_sources`: 让新模块参与编译
   - 在 `openterfaceQT.pro` 增加 `SOURCES += ...` 和 `HEADERS += ...`
   - 保证旧模块编译依赖不变，引用路径 `#include "edid/edidutils.h"` 等。

相关文件
- `ui/advance/updatedisplaysettingsdialog.h` / `.cpp`
- `ui/advance/renamedisplaydialog.h` / `.cpp`
- 新增 `ui/advance/edidutils.h` / `.cpp`
- 新增 `ui/advance/edidresolutionparser.h` / `.cpp`
- 新增 `ui/advance/firmwareutils.h` / `.cpp`

验证
1. `qmake`/`cmake` 配置编译通过
2. 现有功能不回退：命名/序列号修改、分辨率表正常
3. 单元测试覆盖关键函数

决策
- 拆分重点：按功能而非按文件长度。
- 现阶段不重构 `VideoHid`/`FirmwareReader` 外部接口，只重构当前模块内部。