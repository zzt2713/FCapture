#pragma once

#include "annotate/AnnotationTool.h"
#include "platform/win/ElementDetector.h"
#include "snip/LongShotStitcher.h"
#include "snip/ScreenCapturer.h"
#include "snip/SnipResult.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <QTimer>
#include <QWidget>

#include <memory>

class AnnotationModel;
class Annotation;
class PaintBar;
class TextStyleBar;
class TextInputBox;
class SizeInputBox;
class OcrEngine;
class OcrResultDialog;
class LongShotBar;
class LongShotPreview;
class LongShotPreviewBar;
class ShortcutTipsWidget;
class KeyboardHook;
class MagnifierPanel;

// 截图层：全屏遮罩 + 选区 + 放大镜 + 取色 + 窗口检测 + 标注
class SnipOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit SnipOverlay(QWidget *parent = nullptr);
    ~SnipOverlay() override;

    void startSession(const ScreenCapturer::Snapshot &snapshot);

    // 命令行参数：预置选区、延迟、输出方式
    void presetArea(const QRect &globalRect);
    void setHoldMode(bool hold) { m_holdMode = hold; }

    // 命令行 / 热键触发的活动窗口截图：重新抓屏后直接选中前台窗口。
    // knownActiveRect 由调用方在截图层弹出前取好（逻辑坐标），为空时自行探测。
    void captureActiveWindow(const QRect &knownActiveRect = QRect());

    // 仅供离屏测试观察内部状态
    AnnotationModel *modelForTest() const { return m_model; }

    // 长截图入口（热键 F2 / 托盘菜单）：已有选区直接进入准备态，
    // 否则记住意图，选区一就绪自动进入
    void requestLongScreenshot();

signals:
    void snipFinished(const SnipResult &result, SnipOutput output);
    void snipAborted();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    enum class State {
        Idle, Dragging, Adjusting, Resizing, Moving, Drawing, PolylineDrawing,
        LassoSelecting, MagnifyTarget
    };
    enum class Anchor { None, NW, N, NE, E, SE, S, SW, W };

    // --- 坐标换算 ---
    QPoint toLocal(const QPoint &globalPos) const;
    QRect  localSelection() const;
    QRect  globalSelection() const;
    QPoint virtualTopLeft() const;

    // --- 状态机 ---
    Anchor hitTestAnchor(const QPoint &localPos) const;
    bool   hitTestSelection(const QPoint &localPos) const;
    void   beginResize(Anchor anchor);
    void   updateResize(const QPoint &localPos);
    void   updateSelectionDrag(const QPoint &localPos);

    // --- 绘制 ---
    void drawMask(QPainter &p);
    // 选区轮廓变化时只重绘旧/新轮廓附近（拖拽热路径，别用整屏 update）
    void repaintSelectionOutline();
    QRect m_outlinePaintRect;   // 上一帧绘制过的选区（脏区计算基准）
    // 会话结束后立刻释放全屏快照（几十 MB），不等窗口析构
    void releaseSessionImages();
    void drawCapturedCursor(QPainter &p);
    void drawSelectionFrame(QPainter &p);
    void drawSizeLabel(QPainter &p);
    void drawSelectionInfo(QPainter &p);
    void drawAnchors(QPainter &p);
    void drawAuxiliaryLines(QPainter &p);
    void drawMagnifier(QPainter &p);
    void drawHint(QPainter &p);
    // 左下角快捷键帮助浮层：独立子控件，SyncShortcutTips 负责内容/位置/显隐
    void syncShortcutTips();
    QRect shortcutTipsRect() const;
    ShortcutTipsWidget *m_shortcutTips = nullptr;
    bool  m_hoveringTips = false;

    // 居中提示条（在截图层内部绘制，保证一定可见）
    void showCenterNotice(const QString &text, int msec = 2600);
    void drawCenterNotice(QPainter &p);
    QString m_centerNotice;
    QElapsedTimer m_centerNoticeTimer;
    void drawWindowHighlight(QPainter &p);
    // 悬停高亮的过渡动画（借鉴 Snow Shot 的 SmartSelectionTransition）：
    // 矩形从旧位置缓动到新位置，鼠标在元素间移动不再"啪"地跳。
    // 时长/是否启用由 snip/selection_transition_animation 决定（0/32/72）。
    void tickHoverAnim();
    // 当前过渡动画时长（ms）；0 = 关闭动画，直接跳到位
    int hoverAnimDurationMs() const;
    QRect hoverAnimRect() const;
    // 只重绘"鼠标移动时会变"的部分（高亮框/参考线/光标），
    // 不整屏重绘 —— 快速滑动卡顿的根治手段
    void hoverRepaint();
    // 带缓存的窗口矩形查询：同一窗口内连续移动不再反复 EnumWindows
    QRect windowRectAtCached(const QPoint &globalPos);
    // 带缓存的光标下顶层窗口句柄查询（同上）
    quintptr topWindowHwndAtCached(const QPoint &globalPos);
    void drawLassoPreview(QPainter &p);
    QRect sizeLabelRect() const;
    void  drawShadowPreview(QPainter &p);
    void  drawCornerPreview(QPainter &p);

    // --- 功能 ---
    void updateHoverWindow();
    void updateMagnifier();
    void pickColor();
    QString colorString() const;
    QImage  composeResultImage() const;
    // 返回是否真正结束了会话（保存对话框被取消 = false）
    bool    finishSnip(SnipOutput output);
    // 钉住窗口：把选区所在的窗口实时镜像成贴图
    void    pinSelectedWindow();
    // pinSelectedWindow 选中的目标窗口，随 SnipResult 交给 AppController
    quintptr m_pinTargetWindow = 0;
    void    abortSnip();

    // --- P4 检测 ---
    void cycleDetectionMode();
    void requestElementDetection();
    void applyElementLevel(int level);
    void changeElementLevel(int delta);
    void clearDetection();

    // --- P4 选区约束 ---
    void applyAspectToRect(QRect *r, Anchor anchor) const;
    bool isAspectLocked() const { return m_aspect.enabled; }
    void cycleAspectPreset();
    void setAspectPreset(const QString &preset);
    void showSnipMenu(const QPoint &globalPos);
    void toggleRoundedCorners();
    void adjustCornerRadius(int delta);
    void toggleShadowPreview();
    int  effectiveCornerRadius() const;

    // --- P4 刷新 ---
    void refreshSnapshot();
    void beginContinuousRefresh();
    void endContinuousRefresh();

    // --- P4 尺寸输入 ---
    void beginSizeInput();
    void endSizeInput();

    // --- P4 其他 ---
    void recordAbortedSnip();
    QImage postProcessForOutput(const QImage &image, const QString &path = QString()) const;
    void copyImageToClipboard(const QImage &image) const;
    void sendToApplication();

    // --- P5 工具 ---
    void runOcr();
    // 异步任务（OCR）结束后恢复截图层与工具栏
    void restorePaintBarAfterAsync();
    void runBarcodeScan();
    // 二维码识别结果的位置高亮（逻辑坐标，相对截图层）
    QVector<QRectF> m_barcodeRects;
    QImage selectionImage() const;

    // --- 长截图（滚动拼接）---
    // Off=普通模式 / Paused=工具栏打开未捕获 / Capturing=滚动拼接中。
    // 长截图有自己的独立工具栏（LongShotBar），与正常截图工具栏互不干扰。
    // 进入后：外圈与普通截图一致（冻结快照 + 半透明黑遮罩），选区用 setMask
    // 从窗口形状里挖掉（真"天窗"），实时页面直接透出来；
    // 鼠标穿透 + 焦点让给下层窗口，保证滚轮能滚动页面。
    enum class LongShotState { Off, Paused, Capturing };
    void enterLongShot();
    void startLongCapture();
    void pauseLongCapture();
    void cancelLongShot();
    void saveLongShot();
    void copyLongShot();
    void pinLongShot();
    void grabLongShotFrame();
    void syncLongShotUi();
    void drawLongShotRing(QPainter &p);
    LongShotState m_longShotState = LongShotState::Off;
    bool  m_longShotIntent = false;    // 选区就绪后自动进入长截图
    bool  m_longShotGrabbing = false;  // 抓帧在途（防 150ms 定时器重入）
    QRect m_longShotRect;              // 捕获视口（全局逻辑坐标），进入时定死
    QString m_longShotStatus;          // 外框上的状态提示（空 = 默认引导语）
    QTimer *m_longShotTimer = nullptr;
    // 自动滚动：点「开始」后定时把滚轮事件发给下层页面，
    // 用户按「暂停」/「钉」/「保存」/「关闭」/Esc 时停止
    QTimer *m_autoScrollTimer = nullptr;
    // 长截图期间焦点在下层页面，截图层收不到按键，Esc 靠全局钩子兜底
    KeyboardHook *m_longShotEscHook = nullptr;
    LongShotStitcher m_stitcher;
    LongShotBar *m_longShotBar = nullptr;
    // 右侧常驻预览栏（贴在选区右侧，实时显示拼接结果）
    LongShotPreviewBar *m_longShotPreviewBar = nullptr;
    // 悬停预览窗：盖在选区天窗上显示长图对应区段（悬停即预览）
    LongShotPreview *m_longShotHoverPreview = nullptr;
    // 悬停预览期间暂停了抓帧（移开后要恢复）
    bool  m_longShotHoverPaused = false;
    void  hideLongShotHoverPreview();
    int  m_longShotTrimTop = 0;      // 预览栏手柄：头/尾裁掉的行数（画布坐标）
    int  m_longShotTrimBottom = 0;

    void loadHistory(int offset);
    void resetToFullScreen();
    void resetToLastSnip();

    // --- 标注 ---
    void showPaintBar();
    void hidePaintBar();
    void togglePaintBar();
    void syncPaintBarPosition();

    void beginShape(const QPoint &localPos);
    void updateShape(const QPoint &localPos);
    void finishShape();
    void cancelShape();

    void beginPolyline(const QPoint &localPos);
    void addPolylinePoint(const QPoint &localPos);
    void updatePolylinePreview(const QPoint &localPos);
    void finishPolyline();

    void beginTextInput(const QPoint &localPos);
    void beginRectCornerEdit(const std::shared_ptr<Annotation> &shape);

    // 序号 / 放大镜
    void placeNumber(const QPoint &localPos);
    void beginMagnify(const QPoint &localPos);
    void confirmMagnifyTarget(const QPoint &localPos);

    void onToolChanged(Tool tool);
    void onColorChanged(const QColor &color);
    void onPenWidthChanged(int w);

    void drawAnnotations(QPainter &p);
    void drawInProgressShape(QPainter &p);

    std::shared_ptr<Annotation> createShapeForTool(Tool tool) const;
    void applyToolStyleTo(std::shared_ptr<Annotation> shape) const;
    void insertCreatedShape(const std::shared_ptr<Annotation> &shape);

    // --- 选中与编辑 ---
    void selectShapeAt(const QPoint &localPos, bool additive);
    void clearSelection();
    void deleteSelected();
    void drawSelectionHandles(QPainter &p);
    bool hitTestSelectedHandle(const QPoint &localPos, int *outIndex,
                               std::shared_ptr<Annotation> *outShape) const;
    bool hitTestRotationHandle(const QPoint &localPos, std::shared_ptr<Annotation> *outShape) const;
    // 圆角手柄（对齐 snow 的 SelectionCornerRadiusHandle）：
    // 只在单选一个矩形/椭圆时出现，四个角各一个，向中心内缩。
    // 拖动它调整该形状的圆角半径。
    QVector<QPointF> cornerRadiusHandlePositions(const std::shared_ptr<Annotation> &shape) const;
    bool hitTestCornerRadiusHandle(const QPoint &localPos, int *outCorner,
                                   std::shared_ptr<Annotation> *outShape) const;
    qreal maxCornerRadiusFor(const std::shared_ptr<Annotation> &shape) const;
    void beginRotate(const QPoint &localPos);
    void updateRotate(const QPoint &localPos);
    void drawSelectionBox(QPainter &p, const std::shared_ptr<Annotation> &shape);
    void scaleSelectedByWheel(int delta);

    // --- 套索 / 框选 ---
    void beginLasso(const QPoint &localPos);
    void addLassoPoint(const QPoint &localPos);
    void closeLasso();
    void beginRectSelect(const QPoint &localPos);
    void updateRectSelect(const QPoint &localPos);
    void finishRectSelect();

    // --- 复制 / 粘贴 ---
    void copySelectedToInternal();
    void pasteInternalClipboard();
    void beginCloneDrag(const QPoint &localPos);
    void clearSelectionSilently();

    // --- P5 工具 ---
    OcrEngine *m_ocr = nullptr;
    QPointer<OcrResultDialog> m_ocrDialog;

    // 放大镜/取色合并面板（Snow 同款单面板）
    MagnifierPanel *m_magnifierPanel = nullptr;

    // --- 数据 ---
    ScreenCapturer::Snapshot m_snapshot;
    QImage  m_snapshotImage;

    // 光标位图：优先用抓屏那一刻真实拾取的位图（能反映当时的形状），
    // 但位置永远跟随当前鼠标——抓到的是静止快照，而用户的鼠标一直在动，
    // 固定画在抓屏位置会看起来像"光标没显示"。
    QPixmap  m_liveCursorPixmap;
    QPoint   m_liveCursorHotSpot;
    qreal    m_liveCursorDpr = 1.0;
    void     ensureCursorPixmap();

    // 底图缓存：把物理像素快照按逻辑尺寸缩放一次，之后每帧只做 1:1 blit。
    // 之前每帧都 drawPixmap(rect(), m_snapshot.pixmap) —— 那是**缩放**绘制，
    // 4K/多屏下每帧要重采样上千万像素，是截图卡顿的主因。
    // 注意不要把遮罩也合进这张大图：每帧分配+合成整屏 pixmap 更慢，
    // 遮罩单独用 4 个 fillRect 画即可。
    QPixmap m_snapshotLayer;
    QSize   m_snapshotLayerSize;
    qreal   m_snapshotLayerDpr = 1.0;
    // 快照 + 遮罩的合成缓存：选区形状不变时每帧直接整块 blit，
    // 省掉整屏 alpha 混合（悬停/元素识别阶段卡顿的主要来源）
    QPixmap m_maskedLayer;
    QRect   m_maskedLayerSel;
    void    invalidateBackgroundCache() { m_snapshotLayer = QPixmap(); m_maskedLayer = QPixmap(); }
    void    rebuildBackgroundCache();
    void    rebuildMaskedCache();
    bool    m_showCursor = false;

    State   m_state = State::Idle;
    QRect   m_selection;
    QPoint  m_dragStart;
    QPoint  m_dragOrigin;
    QRect   m_resizeOriginRect;
    Anchor  m_activeAnchor = Anchor::None;

    QPoint  m_mouseLocal;
    bool    m_mouseInside = false;

    // 放大镜（工具栏预览）
    bool    m_magnifierOn = false;
    bool    m_mouseMoved = false;
    QElapsedTimer m_moveTimer;

    // 窗口检测
    bool    m_windowDetectionOn = false;
    QRect   m_hoverWindow;
    // 高亮矩形动画（显示值向 m_hoverWindow 缓动）
    QRectF  m_hoverAnimRect;
    QRectF  m_animFrom;
    QRectF  m_animTarget;
    QElapsedTimer m_hoverAnimClock;
    QTimer *m_hoverAnimTimer = nullptr;
    QRect  m_lastHoverPaintRect;     // 上一帧高亮框（脏区计算用）
    // 尺寸标签的显示位置（向 sizeLabelRect() 缓动，避免拖拽时标签"啪"地跳）
    QRectF m_sizeLabelAnimRect;
    QPoint m_lastMousePaintPos;      // 上一帧鼠标位置（参考线脏区用）
    QRect  m_winRectCache;           // 窗口矩形缓存（避免每次 EnumWindows）
    QPoint m_winRectCachePos;        // 缓存对应的光标位置（必须按坐标命中）
    QElapsedTimer m_winRectCacheTimer;
    // 光标下的顶层窗口句柄缓存。WindowFromPoint + EnumWindows 是跨进程查询，
    // 每帧都做会把元素切换拖到十几毫秒。按坐标命中缓存，指针不动就零开销。
    quintptr m_topWinCacheHwnd = 0;
    QPoint   m_topWinCachePos;
    QElapsedTimer m_topWinCacheTimer;
    // 按下左键那一刻的高亮目标（用于松手时落选区）。
    // 按下会 clearDetection() 停掉识别，这里必须先把它存下来，
    // 否则松手时只能退回到"光标下最顶层窗口"，表现为点了元素却截出整屏。
    QRect   m_clickTargetRect;
    // 目标是否是"元素块"（按钮/输入框等），决定自动选区是否显示 8 个锚点：
    // 整窗识别只需要一个方框，元素块才需要锚点微调。
    bool    m_clickTargetIsElement = false;
    QElapsedTimer m_hoverThrottle;

    // --- P4 检测模式 ---
    enum class DetectMode { Free, Window, Element };
    DetectMode m_detectMode = DetectMode::Free;

    QVector<ElementInfo> m_elementHierarchy;
    int      m_elementLevel = 0;
    // 滚轮在"重查期间"累计的待切层数：新链回来后一次性应用。
    // 没有它的话，光标移动后的第一下滚轮只会触发重查、层级不动（被吞掉）。
    int      m_pendingLevelDelta = 0;
    quint64  m_elementRequestId = 0;
    // 元素快照（进入截图时抓一次，鼠标移动纯本地命中，避免 UIA 跨进程延迟）
    QVector<ElementInfo> m_snapshotElements;
    // 命中链缓存：同一位置的结果只允许"向内收"，防止快照分批到达时来回跳
    QPoint m_lastChainPos;
    QVector<ElementInfo> m_lastChain;
    // 已经补抓过命中链的窗口（光标移到新窗口时按需补抓一次）
    quintptr m_snapshotHitHwnd = 0;
    // 从快照里查光标下的元素链（父→子），返回由深到浅的层级。
    // 只在最上层窗口的元素里找 + 结果单调收敛（防止扫描批次到达时高亮乱跳）
    QVector<ElementInfo> hitTestSnapshot(const QPoint &globalPos);
    QPoint   m_elementRequestPos;
    QElapsedTimer m_elementThrottle;
    // 精化门控（snow 同款）：指针停稳 80ms 后，用更大预算再查一次，
    // 让懒加载树把命中路径展开得更深。移动即取消。
    QTimer  *m_refineGate = nullptr;
    QPoint   m_refineAt;
    quintptr m_refineHwnd = 0;
    bool     m_refining = false;
    // 最新鼠标位置（流水线用：结果回来时据此补发）
    QPoint m_pendingCursor;
    bool     m_elementPending = false;
    QString  m_elementProcess;

    // --- P4 选区约束 ---
    struct AspectState
    {
        bool    enabled = false;
        qreal   ratio = 1.0;
        QString preset = QStringLiteral("free");
    };
    AspectState m_aspect;

    qreal m_dragAspectRatio = 0.0;   // 拖拽开始时的选区宽高比（Ctrl 锁定用）

    bool m_roundedCorners = false;
    int  m_cornerRadius = 12;
    bool m_shadowPreview = false;

    // --- P4 刷新 ---
    QTimer *m_refreshTimer = nullptr;
    bool    m_refreshing = false;
    bool    m_refreshHintVisible = false;

    // 会话期置顶守卫：周期性把遮罩推回置顶带最前（任务栏会抢 z 序）
    QTimer *m_topmostGuardTimer = nullptr;
    // 右键菜单打开期间暂停置顶守卫（否则遮罩会被推到菜单之上）
    bool m_menuOpen = false;

    // 右键已被 mousePressEvent 消费（取消工具/取消选中）时，
    // 不再弹选区菜单，避免一次右键做两件事
    bool    m_suppressContextMenu = false;

    // 命令行 --hold：选好区域后不自动输出，等用户点"完成"
    bool    m_holdMode = false;

    // --- P4 尺寸输入 ---
    QPointer<SizeInputBox> m_sizeBox;

    // 取色
    QColor  m_pickedColor;
    bool    m_hexFormat = true;

    // 历史
    int     m_historyOffset = -1;

    bool    m_showAnchors = true;

    // --- 标注状态 ---
    AnnotationModel *m_model = nullptr;
    PaintBar        *m_paintBar = nullptr;
    // 文字标注的浮动样式工具栏（选中单个文字时出现）
    TextStyleBar    *m_textStyleBar = nullptr;
    void syncTextStyleBar();
    QPointer<TextInputBox> m_textBox;
    int              m_textBoxFontSize = 0;   // 输入框当前字号（滚轮可调），落定后直接采用
    bool             m_paintBarVisible = false;
    Tool             m_currentTool = Tool::None;

    std::shared_ptr<Annotation> m_inProgress;
    QPoint m_shapeStart;
    QVector<QPointF> m_polylinePoints;
    QPoint m_polylineCursor;

    // 放大镜标注的两阶段状态
    QPointF m_magnifySourceStart;
    bool    m_magnifyTargetDragging = false;

    // --- 选中与编辑 ---
    std::vector<std::shared_ptr<Annotation>> m_selectedShapes;
    std::shared_ptr<Annotation> m_movingShape;
    int m_resizeShapeHandleIndex = -1;
    // 正在拖动的圆角手柄（-1 = 没在拖）
    int m_cornerRadiusDragCorner = -1;
    // 悬停候选形状（snow 的 hovered element）：鼠标经过未选中的图形时
    // 画一圈虚线候选框，让用户知道"点这里会选中它"。
    std::shared_ptr<Annotation> m_hoveredShape;
    // 悬停时的目标指示形状（用于判断是否要重绘）
    // 选中标注的右键菜单（置顶 / 置底 / 删除）
    void showAnnotationMenu(const QPoint &globalPos);
    // mousePressEvent 判中"右键点在选中标注上"时置位，
    // contextMenuEvent 据此弹标注菜单而不是选区菜单
    bool m_annotationMenuPending = false;
    void updateHoveredShape(const QPoint &localPos);
    void drawHoveredShapeFrame(QPainter &p);
    // 按当前工具返回"落笔光标"（对齐 snow 的 ToolPolicy::default_cursor）：
    // 形状类 = 十字，橡皮 = Eraser，文字 = I 形，序号 = 十字，
    // 选择工具 = 普通箭头。
    Qt::CursorShape toolCursor() const;

    // 旋转
    std::shared_ptr<Annotation> m_rotatingShape;
    qreal m_rotateStartAngle = 0.0;
    qreal m_rotateStartRotation = 0.0;

    // 套索 / 框选
    QVector<QPointF> m_lassoPoints;
    QPoint m_lassoCursor;
    QRect m_rectSelectRect;
    bool m_rectSelectAdditive = false;
    bool m_rectSelectActive = false;

    // 内部剪贴板（形状副本，绕过 fromJson 未实现的问题）
    std::vector<std::shared_ptr<Annotation>> m_internalShapes;
};
