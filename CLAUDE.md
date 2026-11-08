# SnipasteClone — 项目配置

本文件供 Claude Code 每次会话自动加载。**动手前先读 `docs/00-全局约定.md`*

## 项目是什么

用 Qt 6 Widgets (C++20) 复刻 **Snipaste Pro 2.x** 的全部功能：截图、标注、贴图三大模块 + OCR、二维码、命令行、白板等辅助工具。

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
| 控件外观库 | **ElaWidgetTools 2.0.2**（`D:/cppsoft/MsvcEla`），仅用于对话框/菜单/首选项 |
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

### 标准构建流程（**用 VS 生成器，不要用 Ninja**）

本机 `NoDefaultCurrentDirectoryInExePath=1`，在 bash 里 `cmd //c "call vcvars64.bat && ..."` 会失败，**已实测**。改用 Visual Studio 生成器，CMake 自动定位 MSVC，无需 vcvars 环境：

```bash
# 1. 配置
D:/CMake/bin/cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/msvc2022_64

# 2. 编译
D:/CMake/bin/cmake --build build --config Release

# 3. 复制 exe 到 dist（VS 生成器的产物在 build/Release/）
mkdir -p dist/SnipasteClone
cp -f build/Release/SnipasteClone.exe dist/SnipasteClone/

# 4. 打包依赖
D:/Qt/6.8.3/msvc2022_64/bin/windeployqt.exe --release --no-translations \
  --no-system-d3d-compiler --no-opengl-sw dist/SnipasteClone/SnipasteClone.exe
```

**或者直接用脚本**（已写好）：
```bash
./scripts/build.sh          # 构建 + 打包
./scripts/build.sh clean    # 全量重建
./scripts/smoke-test.sh     # 冒烟测试
```

### 打包产物

- 输出目录：`dist/SnipasteClone/`
- 必须存在：`SnipasteClone.exe`、`Qt6Core.dll`、`Qt6Gui.dll`、`Qt6Widgets.dll`、`platforms/qwindows.dll`
- 报告时给出 **`D:\study\QT\Snipaste\dist\SnipasteClone\SnipasteClone.exe` 完整路径**，用户双击即可运行

### 编译失败时

- 自己读错误、自己修、自己重试，不要一报错就丢给用户
- 连续 3 次修不好才向用户报告，并附完整错误日志

## 文档地图

| 文件 | 什么时候读 |
|---|---|
| `docs/00-全局约定.md` | **每次会话必读**。硬约束、编码规范、**Ela 接入规则**、构建流程、视觉规格、交付标准 |

## 界面控件库：ElaWidgetTools

- 位置：`D:/cppsoft/MsvcEla`（2.0.2 预编译，ABI 已核对兼容 Qt 6.8.3）
- **只允许 `src/ui/` 和 `main.cpp` 引入 Ela 头文件**，业务层不得依赖
- 截图层（`SnipOverlay` / `PaintBar` / `PinWindow` / `Whiteboard` / `TextInputBox` / `SizeInputBox`）**保持自绘，不换 Ela**
- **设置窗口不用 `ElaWindow`**（带导航栏的主窗口壳），用 `ElaDialog + ElaTabWidget`

## 阶段进度

不再单独维护阶段进度文档。功能范围和验收依据统一以 `docs/功能清单.md` 为准。

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

## 交付标准（AI 自测通过才算完成）

每个阶段/每次改动交付前，AI 自己走完这条链：

1. ✅ **编译通过**（零错误；警告自己先看一遍，能修就修）
2. ✅ **打包完成** → `dist/SnipasteClone/SnipasteClone.exe`
3. ✅ **启动冒烟测试**：`dist/SnipasteClone/SnipasteClone.exe` 能起来，进程存活 3 秒不崩（用 `tasklist` / 退出码验证）
4. ✅ **日志无 ERROR**：启动后读 `%LOCALAPPDATA%/SnipasteClone/log/` 最新日志，确认无异常
5. ✅ 对照验收清单逐条自查（能自动化的跑一下，不能自动化的标注"需人工确认"）
6. ✅ **保持程序运行，交给用户截图验证**
   - 冒烟测试通过后**不要关闭进程**
   - 用 `start "" "D:\study\QT\Snipaste\dist\SnipasteClone\SnipasteClone.exe"` 以后台方式启动（不阻塞 shell）
   - 报告里写一句「程序已启动，可以直接截图验证」
   - **用户的工作方式：看到程序在跑，自己按 F1 截图测试并截图反馈**

**不允许**把编译不过、启动崩溃、或明显未完成的东西交给用户。

## 红线（用户全局规则同样适用）

- 不自动 `git commit` / `git push`
- 不删除文件/目录
- 不修改 `.env`、密钥、CI 配置
- 临时文件用完自己删

## 回复格式

中文回复，称呼用户为「老大」，结论先行。每次交付回答：
1. 修改了哪些文件
2. 完成到什么程度（对照验收清单）
3. 未解决问题
4. 下一阶段建议
