# F-Capture 用户手册

版本 1.0.0 · Windows 10/11 x64

---

## 一、快速开始

1. 双击 `FCapture.exe` 启动，程序驻留托盘（右下角）。
2. 按 **F1** 开始截图。
3. 鼠标移到目标上会自动高亮窗口/界面元素；单击直接选中，或拖动自由框选。
4. 按 **Enter** 复制到剪贴板。
5. 按 **F3** 把剪贴板内容贴成置顶图片窗口。

程序关闭窗口不会退出，真正退出请用托盘菜单的「退出」。

> 托盘图标悬停会显示当前版本与截图快捷键。

---

## 二、截图

### 2.1 三种检测模式

按 **Tab** 循环切换（默认从「元素检测」开始）：

| 模式 | 行为 |
|---|---|
| **元素检测** | 识别光标下最具体的界面控件（按钮、输入框、列表项…） |
| **窗口检测** | 识别整个窗口边界 |
| **自由选区** | 不做识别，完全手动框选 |

- 悬停时会把检测到的区域从黑色遮罩里「挖出来」，其余部分保持压暗
- **滚轮**在层级间切换：上滚到父容器 / 整窗，下滚回到更具体的控件
- **单击**落下当前高亮区域作为选区
- **`Alt` + 单击** 直接选中整个窗口（不看元素层级）
- 一旦开始手动拖拽，识别立即停止

### 2.2 选区调整

| 操作 | 说明 |
|---|---|
| 拖动选区内部 | 移动选区 |
| 拖动 8 个锚点 | 调整大小（短边 < 48px 时不显示锚点） |
| 方向键 | 移动 1px |
| `Ctrl` + 方向键 | 扩大 1px |
| `Shift` + 方向键 | 缩小 1px |
| 点击尺寸标签 | 输入精确宽高 |

### 2.3 截图内快捷键

| 快捷键 | 动作 |
|---|---|
| `Tab` | 切换 元素 / 窗口 / 自由 检测 |
| 滚轮 | 切换元素层级 |
| `Alt` + 单击 | 选中整个窗口 |
| `Ctrl+A` | 选区扩展到全屏 |
| `R` | 恢复上次截图区域 |
| `,` `.` | 历史截图回放 |
| `WASD` | 移动鼠标光标 1px |
| `F5` | 刷新截图内容（保留标注），长按连续刷新 |
| `Ctrl+R` | 圆角截图开关（`Ctrl+滚轮` 调半径） |
| `` ` `` | 显示 / 隐藏截图时的鼠标指针 |
| `C` | 取色（`Shift` 切换 HEX / RGB） |
| `Ctrl+T` | 直接贴到屏幕 |
| `Enter` | 复制并结束 |
| `Ctrl+S` | 另存为 |
| `Ctrl+Shift+S` | 快速保存 |
| `Esc` | 中止截图 |

> 进入截图后左下角会显示一份快捷键浮层；鼠标移到浮层上会临时隐藏，移开即恢复；开始框选后不再出现。
> 右键选区可设置固定比例、圆角、阴影、精确尺寸、发送到其他应用。

---

## 三、标注

### 3.1 工具

| 工具 | 说明 |
|---|---|
| 矩形 / 椭圆 | 右键工具栏按钮可切换 **空心 / 实心**；矩形单独可设圆角半径 |
| 直线 / 箭头 | 箭头笔杆为锥形（起点极细 → 箭头最粗）；右键可切换 5 种箭头样式 |
| 折线 | 左键逐点添加，右键或双击结束 |
| 铅笔 / 马克笔 | 自由手绘，马克笔为半透明荧光 |
| 文字 | 字号 6~400px，支持背景填充、描边、阴影 |
| 马赛克 / 模糊 / 橡皮擦 | 涂抹或矩形两种模式 |
| 序号 | 1~9 组独立计数，`Ctrl+Shift+R` 重置当前组 |
| 放大镜 | 先框源区域，再框放置位置；完成后只保留放大画面 |
| 套索 | 圈选已有标注，`Ctrl` 追加选择 |

### 3.2 选中与编辑

选中标注后会出现 **8 个灰白手柄** + 一个圆形旋转点：

| 手柄 | 作用 |
|---|---|
| 4 个角 | 缩放（按住 `Shift` 保持宽高比；按住 `Alt` 改为旋转） |
| 4 个边中点 | 单向拉伸 |
| 圆形旋转点 | 旋转（按住 `Alt` 以 15° 步进） |

> 拖动手柄越过对边时，矩形会正常翻转继续变大。
> 小元素（短边 < 48px）不显示手柄，避免遮挡内容。

| 快捷键 | 动作 |
|---|---|
| `Ctrl+Z` / `Ctrl+Y` | 撤销 / 重做（50 步） |
| `Ctrl+Shift+Z` | 清除所有标注 |
| `Del` / `Backspace` | 删除选中标注 |
| `Shift+滚轮` | 缩放选中标注 |
| `Ctrl+Shift+C` / `Ctrl+Shift+V` | 复制 / 粘贴选中标注 |
| `Ctrl` + 拖拽标注 | 克隆一份 |
| `Esc` | 取消选中 → 取消工具 → 中止截图（逐级退出） |

### 3.3 文字

- 点击任意位置开始输入，**输入时看到的字号/颜色就是最终效果**
- 输入框内 **滚轮**可实时调字号；落定后拖角也能等比改字号、拖左右边可改宽度自动换行
- 右键工具栏文字按钮：背景填充、文字描边、描边宽度、字号（6~400）

---

## 四、贴图

按 **F3** 把剪贴板内容贴成置顶图片窗口。支持图片、纯文本、HTML、颜色值、本地文件路径、网页图片 URL。

| 快捷键 | 动作 |
|---|---|
| 滚轮 / `+` `-` | 缩放（`Shift+滚轮` 每次 1% 精细缩放） |
| `Ctrl+滚轮` | 调整透明度 |
| `1` / `2` | 旋转 ±90° |
| `3` / `4` | 水平 / 垂直镜像 |
| `5` / `6` | 灰度 / 反色 |
| 双击 | 重置为 100% |
| `↑↓←→` | 移动 1px（按住 `Shift` 为 10px） |
| `Ctrl+C` / `Ctrl+S` | 复制图像 / 另存为 |
| `Ctrl+Shift+C` | 复制原始纯文本 |
| `Ctrl+A` | 选中全部可见贴图（`Ctrl+单击` 加选） |
| `Esc` / `Ctrl+W` | 关闭（可用 `F3` 恢复） |
| `Shift+Esc` | 销毁贴图 |

> 缩放时左上角会显示当前比例；拖动四角等比缩放。
> 右键菜单还有：裁剪、缩略图模式、鼠标穿透、置顶切换、只显示此贴图、背景模式、移动到分组。

---

## 五、其他功能

| 功能 | 入口 |
|---|---|
| **OCR 文字识别** | 截图工具栏的「识别文字」按钮；结果可编辑、可换语言重识别 |
| 二维码识别 | 截图工具栏按钮（**计划 2.0 版本支持**） |
| 白板 | 托盘菜单「白板」 |
| 分组管理 | 托盘菜单「分组管理…」 |
| 屏幕热角 | 首选项 → 控制 |
| 延迟截图 | 命令行 `--delay` |
| 检查更新 | 首选项 → 关于 → 「检查更新」 |

### OCR 说明

- 使用 Windows 内置 OCR 引擎，**完全本地，不联网**
- 首选项 → 截图 → 可切换识别语言、开关「识别增强」
- 识别增强 = 对比度预处理 + 疑似失败时二次识别，更准但稍慢
- 语言包跟随 Windows 语言安装，机器上有哪些由系统决定

---

## 六、命令行

```
FCapture.exe <命令> [参数]
```

| 命令 | 说明 |
|---|---|
| `snip` | 开始截图 |
| `snip --full` | 整屏截图 |
| `snip --area x,y,w,h` | 指定区域 |
| `snip --last-region` | 重复上次区域 |
| `snip --active-window` | 活动窗口 |
| `snip --active-screen` | 当前屏幕 |
| `snip --delay <秒>` | 延迟截图 |
| `snip --hold` | 选好区域后不自动输出，等待确认 |
| `snip -o <输出>` | 输出方式，可用 `;` 组合 |
| `paste` | 贴图 |
| `paste --text <文本>` / `--html` / `--file` | 指定内容贴图 |
| `toggle-images` / `show-images` / `hide-images` | 贴图显隐 |
| `create-group <名>` / `switch-group <名>` / `empty-group` | 分组操作 |
| `show-group-manager` | 打开分组管理 |
| `whiteboard [--color=#RRGGBB] [--transparent]` | 白板 |
| `pick-color` | 取色 |
| `preferences` | 打开首选项 |
| `exit` | 退出 |

`-o` 可选值：`clipboard`、`pin`、`quick-save`、`file-dialog`、`ocr_clipboard`、`<路径>`、`exec(<命令>)`

示例：

```bat
:: 截全屏 → 存文件并复制到剪贴板
FCapture.exe snip --full -o "D:\shot.png;clipboard"

:: 延迟 3 秒截活动窗口，直接贴出
FCapture.exe snip --active-window --delay 3 -o pin
```

---

## 七、便携模式

在 `FCapture.exe` 同目录放一个空文件 `portable.txt` 即进入便携模式：

- 配置 → `data/config.ini`
- 日志 → `data/log/`
- 崩溃转储 → `data/crash/`

整个文件夹拷走即可带走全部配置，不在系统里留痕。
便携模式下「开机自动启动」不可用。

普通模式（没有 `portable.txt`）数据目录为 `%LOCALAPPDATA%\F-Capture`。

---

## 八、疑难排查

### 热键没反应

多半被其它软件占用（原版 Snipaste、输入法、录屏工具）。
启动日志里有 `RegisterHotKey failed`，托盘悬停提示也会显示「（被占用）」。
到「首选项 → 控制」换一个组合键即可。

### 元素识别不准

- 部分程序（游戏、自绘界面）不提供可访问性信息，只能识别到窗口级
- Chrome/Electron 内部控件可能需要滚轮切换层级
- 在首选项 → 截图里可配置「元素检测走 MSAA 的程序」

### 程序闪退

1. 下次启动会提示「上次运行异常退出」
2. 崩溃转储在 `<数据目录>\crash\*.dmp`
3. 「首选项 → 关于 → 导出诊断信息」会生成包含日志、配置、系统信息的 zip

### 日志在哪

- 普通模式：`%LOCALAPPDATA%\F-Capture\log\fcapture.log`
- 便携模式：`<exe 目录>\data\log\fcapture.log`

日志超过 2MB 自动轮转（最多 4 份），崩溃转储只保留最近 5 个。
需要更详细日志时用 `FCapture.exe --log-level=debug` 启动。

### 更新

「首选项 → 关于 → 检查更新」会查询 GitHub Releases。
有新版本时可点「立即更新」自动下载、校验 SHA256、覆盖安装并重启。
更新失败会自动回滚到旧版本。

---

## 九、配置项

配置文件是标准 INI，可用首选项改，也可直接手改（改完重启）。

| 键 | 默认值 | 说明 |
|---|---|---|
| `hotkey/capture` | `F1` | 截图热键 |
| `hotkey/paste` | `F3` | 贴图热键 |
| `hotkey/toggle-images` | `Shift+F3` | 贴图显隐热键 |
| `hotkey/capture-active-window` | `Ctrl+Shift+A` | 活动窗口热键 |
| `general/play_sound` | `true` | 截图完成提示音 |
| `general/record_all_snips` | `false` | 记录 Esc 中止的截图 |
| `general/copy_image_as_file` | `false` | 复制图像同时复制文件 |
| `general/auto_save_enabled` | `false` | 自动保存截图 |
| `general/auto_save_folder` | 图片目录 | 自动保存目录 |
| `general/quick_save_folder` | 图片目录\Snipaste | 快速保存目录 |
| `general/show_operation_toast` | `false` | 复制/保存后显示提示 |
| `general/hide_tray_icon` | `false` | 隐藏托盘图标 |
| `snip/capture_cursor` | `false` | 截图包含鼠标光标 |
| `snip/auto_detect_window` | `true` | 自动检测窗口/元素 |
| `snip/show_auxiliary_lines` | `true` | 显示辅助线 |
| `snip/corner_radius` | `0` | 圆角半径 |
| `snip/shadow_enabled` | `false` | 导出加阴影 |
| `snip/exclude_from_detection` | 空 | 不参与检测的进程名（逗号分隔） |
| `snip/element_msaa_apps` | 空 | 强制走 MSAA 的进程名 |
| `paste/magnetic_attach_threshold` | `20` | 贴图吸附距离 |
| `annotate/auto_select_on_creation` | `true` | 创建标注后自动选中 |
| `interface/theme` | `dark` | 主题：system / dark / light |
| `interface/uppercase_hex` | `false` | 取色用大写 HEX |
| `interface/size_unit_dip` | `false` | 尺寸单位用 dip |
| `output/file_name_format` | `Snipaste_{yyyy}-{MM}-{dd}_{HH}-{mm}-{ss}` | 自动保存文件名模板 |
| `output/format` | `png` | 默认格式 |
| `output/quality` | `-1` | 压缩质量（-1 = 自动） |
| `ocr/language` | `zh-Hans-CN` | OCR 语言 |
| `ocr/enhance` | `true` | OCR 识别增强 |
| `diagnostics/export_folder` | 空 | 诊断信息导出目录（空 = 桌面） |

---

## 十、已知限制

| 项 | 现状 |
|---|---|
| 二维码识别 | 计划 2.0 版本支持，当前点击会提示 |
| OCR 符号识别 | Windows OCR 引擎没有符号模型，箭头等可能被识别成形近汉字 |
| 元素识别 | 游戏、部分自绘程序不提供可访问性信息，只能到窗口级 |
| 原生分享 | 未实现（可用「发送到」打开其他应用） |
| 截图历史 | 仅内存保存，退出即清空 |
