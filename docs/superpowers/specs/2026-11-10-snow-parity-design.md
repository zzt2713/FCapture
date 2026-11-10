# 对齐 Snow Shot：元素识别修复 + 长截图悬停预览 + 提示框 + OCR 结果交互

> 2026-11-10 · 参考项目 `D:\下载\snow-apps-main`（snow_shot + snow-crates）
> 状态：设计已确认（悬停预览 / 保留 WinOcr）；C、D 的视觉方案待用户从 `preview/` 选定。

## 已定决策

| 决策点 | 结论 |
|---|---|
| 长截图预览触发方式 | **悬停即预览**（snow 同款）：悬停缩略图 → 选区显示对应位置已截内容，移开恢复 |
| OCR 引擎 | **保留 Windows.Media.Ocr**（硬约束），只抄 snow 的结果交互（行框/行框选/字符级选中/文字高亮视图） |
| 提示框 / OCR 窗口视觉 | 从 `preview/hint-box.html`（C1–C6）、`preview/ocr-dialog.html`（D1–D6）中选定后实现 |

## A. 元素识别修复（bug）

根因：
1. `ElementDetector::Worker::snapshot` 的 `EnumWindows` 只查 `IsWindowVisible`，未过滤最小化（`IsIconic`）与 DWM cloaked（`DWMWA_CLOAKED`）；最小化窗口的 UIA 元素以"还原矩形"进快照。
2. `SnipOverlay::hitTestSnapshot` 的归属判定是**几何中心包含**（`topWin.contains(e.center)`），且 `topWin` 为空时**不过滤**——被遮挡窗口元素混入、桌面处全部命中。

改动（对齐 snow `snow-ui-selector/src/windows/{window,uia,cache}.rs`）：
- `WindowDetector::acceptWindow`（`src/platform/win/WindowDetector.cpp`）：新增 `DWMWA_CLOAKED != 0` 丢弃。
- `ElementDetector::Worker::snapshot`：候选窗口改用 `WindowDetector::enumTopLevelWindows()`（含 IsIconic/壳窗口/WS_EX_TRANSPARENT/配置排除/遮罩排除），删除自写的 EnumWindows 过滤；「排除本进程」改为「只排除遮罩」（与 WindowDetector 一致）。
- `scanOneWindow` / `snapshotWindowTree`：每个 `ElementInfo.nativeHandle` 填所属顶层窗口 HWND；元素矩形与窗口 frame 矩形求交，空矩形丢弃。
- `SnipOverlay::hitTestSnapshot`：归属判定改为 `e.nativeHandle == windowFromPoint(pos).hwnd`；无最上层窗口（桌面）→ 返回空链；删除几何包含判定。

验证：最小化记事本不再出元素；两窗重叠只识别上窗；桌面上不高亮；首选项/白板可被识别。

## B. 长截图悬停预览（snow 同款）

- 删除 `LongShotPreview`（`src/snip/LongShotPreview.{h,cpp}`）弹窗查看器及其 connect。
- `LongShotPreviewBar`：悬停亮带由全宽横条改为 **视口大小矩形**（中心跟随光标，snow `updateHover()` 同款映射）；持续发 `hoverRegionChanged(QRect canvasRect)`（画布坐标），离开发空矩形。`imageYClicked` 信号删除。
- `SnipOverlay` 长截图部分：收到 `hoverRegionChanged` → 在选区位置显示预览：**独立 Tool 小窗**（`Qt::Tool | FramelessWindowHint | WindowStaysOnTopHint | WindowDoesNotAcceptFocus` + 点击穿透）盖在"天窗"上，内容 = `m_stitcher.canvas()` 对应区域缩放 + 「预览中」状态胶囊（snow CanvasStatusReadout 同款）。移开即隐藏，天窗恢复透实时页面。
  - 选天窗上盖窗而不是 setMask 回填：天窗是 `setMask` 挖洞，overlay 画不进洞里；回填 mask 会闪。
- 悬停期间自动暂停抓帧/自动滚动（snow hover-pause），移开恢复（仅当原本处于 Capturing）。
- 缩略图补「最近捕获段高亮」（snow `m_highlightedRows` 同款：新增拼接段重画亮）。

## C. 左下角提示框

视觉与内容按选定方案实现（C1–C6 见 `preview/hint-box.html`），公共点：
- 内容按模式切换：元素/窗口检测（含滚轮切层级）、自由模式精简、长截图模式（滚轮滚动、Esc 退出等）。
- 行为保持：`State::Idle` 无选区时显示；开始拉选区隐藏；鼠标移入临时隐藏（几何仍由 `shortcutTipsRect()` 统一）。

## D. OCR 结果交互（引擎不动）

布局按选定方案实现（D1–D6 见 `preview/ocr-dialog.html`），公共交互（snow `screenshotocrpresentation` 同款）：
- 图上叠加每行文字框（WinOcr Lines/Words BoundingBox → 选区图坐标），hover 行框 ↔ 文本行双向高亮。
- 行框选（Shift/Ctrl 增减）、字符级文本选中、复制选中/全文。
- 「文字高亮视图」：非文字区压暗、只留文字框（snow `ScreenshotOcrTextLayer` 同款）。
- 保持识别完成自动复制全文、失败恢复截图层（`restorePaintBarAfterAsync()`）。

## 实施顺序与验证

A → C → B → D。每步走项目交付链：编译零错误 → 打包 `dist/F-Capture/FCapture.exe` → 启动冒烟 3s → 日志无 ERROR → 对照上文验证项（不能自动化的标注「需人工确认」）→ 保持进程运行交用户截图验证。
