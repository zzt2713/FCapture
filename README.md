# F-Capture

用 Qt 6 Widgets (C++20) 实现的 Windows 截图 / 标注 / 贴图工具，仿 Snipaste 的交互。

<img src="logo/icon.png" width="96" alt="F-Capture" />

## 功能

**截图**
- 区域截图、窗口检测、界面元素检测（UI Automation，可识别按钮/输入框等具体控件）
- 滚轮在「具体控件 ↔ 父容器 ↔ 整窗」之间切换层级，`Alt + 单击` 直接选整窗
- 像素级微调、固定比例、圆角、阴影、延迟截图、历史回放
- 截图包含鼠标光标（`~` 切换）

**标注**
- 矩形 / 椭圆（支持空心与实心）、直线 / 箭头（锥形笔杆）、折线、铅笔、马克笔
- 文字（6~400px、背景填充、描边）、马赛克、高斯模糊、橡皮擦
- 序号标注、放大镜、套索选择、8 点缩放 + 旋转、复制/粘贴标注
- 撤销/重做 50 步、自定义调色板

**贴图**
- 剪贴板图片 / 文本 / HTML / 颜色 / 文件路径 → 置顶图片窗口
- 缩放、透明度、旋转、镜像、灰度、反色、裁剪
- 分组管理、吸附、鼠标穿透、虚拟桌面支持

**其他**
- OCR 文字识别（Windows.Media.Ocr，本地不联网）
- 托盘常驻、全局快捷键、命令行参数、白板、屏幕热角

## 硬性要求

| 项 | 值 |
|---|---|
| 系统 | Windows 10 / 11 x64 |
| Qt | **6.8.3 msvc2022_64** |
| 编译器 | **MSVC 2022 x64**（必须，OCR 依赖 C++/WinRT，MinGW 不可用） |
| CMake | ≥ 3.21，使用 **Visual Studio 17 2022** 生成器 |
| ElaWidgetTools | **2.0.2**，默认路径 `D:/cppsoft/MsvcEla` |

## 构建

```bash
# 1. 配置（路径按自己的环境改）
D:/CMake/bin/cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/msvc2022_64 \
  -DELA_ROOT=D:/cppsoft/MsvcEla

# 2. 编译
D:/CMake/bin/cmake --build build --config Release

# 3. 产物
#    build/Release/FCapture.exe
```

## 项目结构

```
src/
├── core/       应用控制、配置、日志、热键、诊断、更新检查
├── snip/       截图层（全屏遮罩、选区状态机、绘制）
├── annotate/   标注模型、工具栏、各类形状
├── paste/      贴图窗口与管理器
├── tools/      OCR、白板、热角、文字转图片
├── platform/win/  Win32 / UIA / WinRT 封装（唯一允许 include <windows.h> 的地方）
└── ui/         Ela 对话框、菜单、图标（唯一允许引入 ElaWidgetTools 的地方）
```

关键设计约定见 [`docs/00-全局约定.md`](docs/00-全局约定.md)。

## 便携模式

> 本程序默认使用系统标准路径存储配置与日志；若在 exe 同目录放置 `portable.txt` 即进入便携模式，所有数据改存于 `data/` 子目录、系统盘零残留（因需写注册表，便携模式下开机自启被禁用）。

| 项       | 普通模式                            | 便携模式                           |
| -------- | ----------------------------------- | ---------------------------------- |
| 触发条件 | 默认                                | exe 同目录存在 portable.txt        |
| 配置     | %LOCALAPPDATA%/F-Capture/config.ini | <exe目录>/data/config.ini          |
| 日志     | %LOCALAPPDATA%/F-Capture/log/       | <exe目录>/data/log/                |
| 崩溃转储 | %LOCALAPPDATA%/F-Capture/crash/     | <exe目录>/data/crash/              |
| 开机自启 | 可用（写注册表）                    | 禁用（注册表会留痕，破坏便携语义） |

## 更新

「首选项 → 关于 → 检查更新」会查询本仓库的 GitHub Releases，比较版本号后给出提示。

## 文档

- [`docs/user-guide.md`](docs/user-guide.md) —— 用户手册（功能、快捷键、命令行、配置项）
- [`docs/00-全局约定.md`](docs/00-全局约定.md) —— 开发约定（硬约束、编码规范、踩过的坑）

## 许可

本仓库仅用于学习与个人使用。
