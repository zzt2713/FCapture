# F-Capture — 项目配置

本文件供 Claude Code 每次会话自动加载。**动手前先读 `docs/00-全局约定.md`**

## 项目是什么

用 Qt 6 Widgets (C++20) 复刻 **Snipaste Pro 2.x** 的全部功能：截图、标注、贴图三大模块 + OCR、二维码、命令行、白板等辅助工具。

> 项目原名 SnipasteClone，已改名 **F-Capture**。旧文档/旧脚本里出现的 `SnipasteClone` 路径都是过时的。

## 硬约束（不可协商）

| 项 | 值 |
|---|---|
| Qt | 6.8.3 **msvc2022_64**（`D:/Qt/6.8.3/msvc2022_64`） |
| 编译器 | **MSVC 2022 x64**，禁止 MinGW（`Windows.Media.Ocr` 依赖 C++/WinRT） |
| C++ 标准 | C++20 |
| UI | **Qt Widgets**，禁止 QML |
| OCR | `Windows.Media.Ocr`（本地，不联网，不接第三方 API） |
| 平台 | 仅 Windows |
| 构建 | CMake ≥ 3.21 + **Visual Studio 17 2022 生成器**（不用 Ninja） |
| 控件外观库 | **ElaWidgetTools**（预编译 `D:/cppsoft/MsvcEla`，源码 `D:/study/code/ElaWidgetTools`） |
| 界面风格 | 截图层复刻 Snipaste 极简暗色；应用界面用 Ela 暗色主题（见 `docs/00-全局约定.md` §6） |

## 编译、打包、交付由我完成（AI 负责）

**AI 必须自己编译、自己打包，交付可运行的程序给用户测试。** 用户只负责运行和反馈。

### 环境路径（已实测验证）

| 项 | 路径 | 说明 |
|---|---|---|
| Qt | `D:/Qt/6.8.3/msvc2022_64` | `D:/QT` 是同一目录（Windows 不区分大小写） |
| 编译器 | VS 2022 自带 `cl.exe`（MSVC 19.44） | **不在 Qt 目录里**。`D:/Qt/.../bin` 只有 qmake/moc/windeployqt 等 Qt 工具 |
| CMake | `D:/CMake/bin/cmake` | |
| windeployqt | `D:/Qt/6.8.3/msvc2022_64/bin/windeployqt.exe` | |
| **ElaWidgetTools 源码** | `D:/study/code/ElaWidgetTools` | **完整源码 + 官方示例**，见下节 |

### 标准构建流程

**一律跑 `./scripts/build.sh`**（含手册/zip/SHA256/安装包），不要手动拼命令。

只做快速验证时（改完代码看效果）：

```bash
D:/CMake/bin/cmake --build build --config Release
cp -f build/Release/FCapture.exe dist/F-Capture/
```

本机 `NoDefaultCurrentDirectoryInExePath=1`，在 bash 里 `cmd //c "call vcvars64.bat && ..."` 会失败，**已实测**。用 Visual Studio 生成器，CMake 自动定位 MSVC，无需 vcvars 环境。

### 打包产物

- 输出目录：`dist/F-Capture/`
- 主程序：`dist/F-Capture/FCapture.exe`
- 日志：`%LOCALAPPDATA%/F-Capture/log/`
- 报告时给出 **`D:\study\QT\Snipaste\dist\F-Capture\FCapture.exe` 完整路径**

### 编译失败时

- 自己读错误、自己修、自己重试，不要一报错就丢给用户
- 连续 3 次修不好才向用户报告，并附完整错误日志

## 交付方式

用户**自己测试**软件，AI 只负责编译 + 打包 + 启动。

编译完成后用后台方式启动，让用户直接看效果：

```bash
taskkill //F //IM FCapture.exe 2>/dev/null
start "" "D:\study\QT\Snipaste\dist\F-Capture\FCapture.exe"
```

报告里写一句「程序已启动，可以直接截图验证」。**不要跑冒烟测试、不要读日志找 ERROR** —— 用户会自己反馈。

## 文档地图

| 文件 | 什么时候读 |
|---|---|
| `docs/00-全局约定.md` | **每次会话必读**。硬约束、编码规范、**Ela 接入规则**、构建流程、视觉规格、交付标准 |

## 界面控件库：ElaWidgetTools

- 预编译库：`D:/cppsoft/MsvcEla`（2.0.0，ABI 已核对兼容 Qt 6.8.3）
- **完整源码 + 官方示例**：`D:/study/code/ElaWidgetTools`
  - 组件实现：`ElaWidgetTools/ElaWidgetTools/*.cpp`
  - 官方用法示例：`ElaWidgetTools/ElaWidgetToolsExample/ExamplePage/*.cpp`
- **只允许 `src/ui/` 和 `main.cpp` 引入 Ela 头文件**，业务层不得依赖
- 截图层（`SnipOverlay` / `PaintBar` / `PinWindow` / `Whiteboard` / `TextInputBox` / `SizeInputBox`）**保持自绘，不换 Ela**
- **设置窗口不用 `ElaWindow`**（带导航栏的主窗口壳）

### ⚠️ Ela 组件使用规则（血泪教训，用前必读源码）

**不要靠猜 API 和尺寸，直接读 `D:/study/code/ElaWidgetTools` 源码。** 下面每一条都是实际踩过的坑：

| 组件 | 坑 | 正确做法 |
|---|---|---|
| `ElaScrollPageArea` | 构造函数里写死 `setFixedHeight(75)`，是**单行卡片** | 只放一行（一个开关/按钮 + 文字）；多行分组用 `ElaGroupBox` |
| `ElaScrollPage` | `addCentralWidget()` 会把内容包进中间容器，`addWidget` **没给 stretch** | 内容布局末尾**必须** `addStretch()`，否则高度塌到最小（"压扁"） |
| `ElaScrollPage` | 内部是 `QStackedWidget` + 标题区 + 面包屑，是整页导航壳 | 单页设置用 `ElaScrollArea` 更合适；用 `ElaScrollPage` 时 `setTitleVisible(false)` |
| `ElaGroupBox` | 标题画在**边框线上**（`frameRect.top() + titleHeight/2`），且样式没重写 `subControlRect` | 布局顶部边距必须留 `fontMetrics().height() + 4`，否则首行内容压在标题上 |
| `ElaPushButton` | 默认色/文字色分亮暗两套 | 改色要同时设 `setLight*Color` 和 `setDark*Color` |
| 任何控件 | `new Xxx(parent)` 只建立父子关系，**不进布局就停在 (0,0)** | 必须 `addWidget()` 到某个布局，否则会压在标题/左上角上 |

### 首选项当前架构

```
PreferenceDialog : ElaDialog
├── NavList（自绘左侧导航）
└── ThemedStack : QStackedWidget
    └── 每页 = ElaScrollArea（Ela 滚动条 + 拖拽手势）
        └── ThemedPage（透明，QVBoxLayout）
            └── ElaGroupBox × N（多行分组）
                └── 行控件（ElaToggleSwitch / ElaComboBox / ElaPushButton …）
```

快捷键设置走 snow 式弹窗：行内 `ElaPushButton` 显示当前键 → 点击弹 `ElaContentDialog` → 弹窗内 `HotkeyConfigWidget` 支持多键增删 → 确定才生效。冲突时按钮文字变成 `F1 · 被占用` 并变红。

## 编码规范速查

详细版见 `docs/00-全局约定.md` 第 3 节。最容易犯的几条：

- 一律新式 `connect`（函数指针 / lambda），**禁止 `SIGNAL()`/`SLOT()` 宏**
- 字符串用 `QStringLiteral()`
- 头文件用 `#pragma once`
- **`#include <windows.h>` 只允许出现在 `src/platform/win/` 下**
- 信号槽、类名 PascalCase，成员 `m_camelCase`，常量 `kPascalCase`
- 不留 `TODO`、不留注释掉的死代码、不留未使用的 include

## 关键约定

- 标注模型使用**逻辑像素**坐标，与选区同一坐标系；只在导出时按 `devicePixelRatio` 换算物理像素
- `SnipOverlay` 的状态机、`Annotation` 基类接口、`PinManager` 的生命周期是后续阶段的依赖基础，**改这些接口要同步检查相关实现和测试**
- 每个阶段有验收清单，交付时必须逐条对照回答，不允许跳过

## 更新日志（强制）

**每次做完任务，都要把改动写进 `docs/update.md`。**

- 文件按版本分段，每段一个 `## vX.Y.Z` 标题，**新版本写在最上面**
- 同一个版本内的多次改动，持续追加到该版本段落，不要新开一段
- 用户发布某个版本后，下一次改动就写到**下一个版本号**的新段落里
- 每条记录写清：做了什么、为什么改（用户可见的行为变化），不要贴实现细节

格式：

```markdown
## v1.6.0（未发布）

### 新增
- ...

### 修复
- ...

## v1.5.0（2026-11-12）
...
```

## 排查问题的纪律

**不要靠猜。** 这次会话在设置界面上反复猜了 5 轮，全部猜错，真正的原因是 `ElaScrollPageArea` 写死 75px 高。

正确的顺序：

1. **先读源码**（Ela 组件在 `D:/study/code/ElaWidgetTools`）
2. **再读官方示例**（`ElaWidgetToolsExample/ExamplePage/`）
3. 还不确定就**加日志量实际几何**（`size()` / `geometry()` / `sizeHint()` / `sizePolicy()`），拿数据说话
4. 最后才改代码

## 红线（用户全局规则同样适用）

- 不自动 `git commit` / `git push`
- 不删除文件/目录（含 `rm -rf`，恢复备份也要先问）
- 不修改 `.env`、密钥、CI 配置
- 临时文件用完自己删

## 回复格式

中文回复，称呼用户为「老大」，结论先行。每次交付回答：
1. 修改了哪些文件
2. 完成到什么程度（对照验收清单）
3. 未解决问题
4. 下一阶段建议
