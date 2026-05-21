# VpFreezeCopyPaste — AutoCAD 布局视口图层冻结状态跨文档复制/粘贴

## 功能

在 AutoCAD 的布局空间中，每个布局视口（Viewport）都可以独立设置各图层的冻结/解冻状态。
本工具提供了两个命令，让你可以**框选多个视口和多段线等实体**，跨文档复制粘贴，并且自动携带和恢复视口的图层冻结状态。

### 命令

| 命令 | 别名 | 功能 |
|------|------|------|
| `VpFreezeCopy` | `CopyVpFreeze` | **框选**源实体（视口、多段线等），复制到剪贴板，视口附带图层冻结状态 |
| `VpFreezePaste` | `PasteVpFreeze` | 在目标文档指定插入点粘贴，**自动恢复各视口的冻结状态** |

### 工作流

1. 在源文档中执行 `VpFreezeCopy`
2. **框选**需要复制的实体（支持窗选、框选、点选等多种方式）
3. 指定复制**基点**
4. 切换到目标文档
5. 执行 `VpFreezePaste`
6. 指定粘贴插入点
7. ✅ 所有实体粘贴完毕，视口冻结状态自动恢复

### 特性

- ✅ **多实体支持**：视口、多段线、及其他 AutoCAD 实体均可框选复制
- ✅ **跨文档复制**：通过 Windows 剪贴板 + XData 标记，跨文档粘贴并匹配视口
- ✅ **支持 UNDO**：粘贴操作会分组到单个 UNDO 步骤中
- ✅ **跳过锁定图层**：粘贴时自动跳过已锁定的图层
- ✅ **增量更新**：仅修改状态不一致的图层
- ✅ **智能提示**：展示成功恢复的视口数、更新图层数、跳过锁定图层数

## 系统要求

| 软件 | 版本 |
|------|------|
| AutoCAD | 2021 / 2022 / 2023 / 2024 / 2025 / 2026 (64位) |
| ObjectARX SDK | 对应 AutoCAD 版本 |
| Visual Studio | 2022 (v143 工具集) |
| Windows SDK | 10.0+ |

## 编译步骤

### 1. 安装 ObjectARX SDK

从 [Autodesk 官网](https://www.autodesk.com/developer-network/platform-technologies/objectarx) 下载对应 AutoCAD 版本的 ObjectARX SDK，解压到本地目录，例如：

```
C:\ObjectARX\2026\
```

### 2. 配置环境变量

在系统环境变量中添加 `ARXSDK`，指向 ObjectARX SDK 的根目录：

```
ARXSDK = C:\ObjectARX\2026\inc
```

> 注意：项目中的 `$(ARXSDK)` 宏会展开为 `C:\ObjectARX\2026\inc`，因此头文件路径为 `$(ARXSDK)\inc`，库路径为 `$(ARXSDK)\lib-x64`。请根据你的实际安装路径调整环境变量，或直接在 `.vcxproj` 文件中修改路径。

### 3. 打开并编译

1. 双击 `ViewportFreezeCopyPaste.sln` 用 Visual Studio 2022 打开
2. 选择 **Release | x64** 配置
3. 按 `Ctrl+Shift+B` 编译
4. 编译产物为 `Output\Release\VpFreezeCopyPaste.arx`

## 安装与使用

### 方法一：手动加载

1. 在 AutoCAD 中输入命令 `AP` (APPLOAD)
2. 选择编译生成的 `VpFreezeCopyPaste.arx` 文件
3. 点击"加载"

### 方法二：自动加载

将 `.arx` 文件放入 AutoCAD 的自动加载目录，或在 `acad.rx` 中添加一行：

```
VpFreezeCopyPaste.arx
```

## 项目结构

```
ViewportFreezeCopyPaste/
├── ViewportFreezeCopyPaste.sln              # Visual Studio 解决方案
├── ViewportFreezeCopyPaste.vcxproj          # 项目文件
├── src/
│   ├── ViewportFreezeCopyPaste.cpp          # 主源码（命令实现）
│   └── ViewportFreezeCopyPaste.def          # 模块定义文件
├── Output/                                  # 编译输出（自动生成）
├── Intermediate/                            # 中间文件（自动生成）
└── README.md                                # 本文件
```

## 技术细节

### 跨文档视口匹配机制

由于跨文档粘贴时视口的 ObjectId / handle 会改变，本工具使用 **XData（扩展实体数据）** 来标记视口：

1. **复制时**：为每个选中的视口附加 XData `(1001 . "VPFREECOPY") (1070 . index)`，同时将冻结状态存入内存
2. **粘贴后**：扫描目标文档中所有带 `VPFREECOPY` XData 的视口，按 index 匹配冻结状态并应用
3. **清理**：复制完成后立即清除源视口的 XData；粘贴应用完成后也清除目标视口的 XData

### 内存数据跨文档保持

`g_copiedViewportStates` 是全局变量，在同一 AutoCAD 会话中跨文档保持有效，
因此**无需退出 AutoCAD** 即可在多个文档间切换复制粘贴。

## 常见问题

**Q: 粘贴时出现"锁定图层"跳过提示？**
A: 这是正常行为。粘贴操作会自动跳过锁定的图层，防止写入冲突。如需修改锁定图层的冻结状态，请先解锁图层。

**Q: 为什么有些图层的冻结状态没有应用？**
A: 如果目标图形中的图层集合与源图形不一致，不存在的图层会被自动跳过。

**Q: 没有视口被选中，只有多段线，能复制吗？**
A: 可以。`VpFreezeCopy` 会使用 `acedCopybase` 将所有选中实体复制到剪贴板，视口的冻结状态是额外附加的信息。

## 许可

MIT License
