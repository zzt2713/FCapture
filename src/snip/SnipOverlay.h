#pragma once

#include "annotate/AnnotationTool.h"
#include "platform/win/ElementDetector.h"
#include "snip/ScreenCapturer.h"
#include "snip/SnipResult.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <QWidget>

#include <memory>

class AnnotationModel;
class Annotation;
class PaintBar;
class TextInputBox;
class SizeInputBox;
class OcrEngine;
class OcrResultDialog;

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
    // 会话结束后立刻释放全屏快照（几十 MB），不等窗口析构
    void releaseSessionImages();
    void drawCapturedCursor(QPainter &p);
    void drawSelectionFrame(QPainter &p);
    void drawSizeLabel(QPainter &p);
    void drawSelectionInfo(QPainter &p);
    void drawAnchors(QPainter &p);
    void drawAuxiliaryLines(QPainter &p);
    void drawMagnifier(QPainter &p);
    // 放大镜面板的矩形（逻辑坐标）。取色信息面板用它避让，避免被放大镜盖住。
    QRect magnifierRect() const;
    void drawHint(QPainter &p);
    // 左下角快捷键帮助浮层：鼠标移动或开始选区后自动消失
    void drawShortcutTips(QPainter &p);
    // 居中提示条（在截图层内部绘制，保证一定可见）
    void showCenterNotice(const QString &text, int msec = 2600);
    void drawCenterNotice(QPainter &p);
    bool shortcutTipsVisible() const;
    QRect shortcutTipsRect() const;
    bool  m_hoveringTips = false;

    QString m_centerNotice;
    QElapsedTimer m_centerNoticeTimer;
    void drawWindowHighlight(QPainter &p);
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
    void    finishSnip(SnipOutput output);
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
    QImage selectionImage() const;

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
    void    invalidateBackgroundCache() { m_snapshotLayer = QPixmap(); }
    void    rebuildBackgroundCache();
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
    quint64  m_elementRequestId = 0;
    QPoint   m_elementRequestPos;
    QElapsedTimer m_elementThrottle;
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
    QPointer<TextInputBox> m_textBox;
    int              m_textBoxFontSize = 0;   // 输入框当前字号（滚轮可调），落定后直接采用
    qreal            m_textBoxLayoutWidth = 0; // 输入框排版宽度，落定后写进 TextShape
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
