#include "snip/SnipOverlay.h"

#include "annotate/AnnotationModel.h"
#include "annotate/PaintBar.h"
#include "annotate/TextInputBox.h"
#include "annotate/shapes/FreehandShape.h"
#include "annotate/shapes/LineShape.h"
#include "annotate/shapes/MagnifyShape.h"
#include "annotate/shapes/NumberShape.h"
#include "annotate/shapes/PixelShape.h"
#include "annotate/shapes/RectShapes.h"
#include "annotate/shapes/TextShape.h"
#include "core/ConfigManager.h"
#include "core/ImageOutput.h"
#include "platform/win/AppLauncher.h"
#include "platform/win/WinUtil.h"
#include "platform/win/WindowDetector.h"
#include "snip/ImageEffects.h"
#include "snip/SnipHistory.h"
#include "snip/SizeInputBox.h"
#include "tools/OcrEngine.h"
#include "ui/AppMenu.h"
#include "ui/OcrResultDialog.h"
#include "ui/BarcodeResultDialog.h"
#include "ui/OperationToast.h"
#include "ui/Theme.h"

#include "platform/win/QrScanner.h"
#include "snip/InfoPanel.h"
#include "snip/MagnifierPanel.h"
#include "snip/LongShotBar.h"

#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QTextEdit>

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDir>
#include <QFileDialog>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>


namespace {

constexpr int kAnchorSize = 8;
constexpr int kAnchorHitTolerance = 10;   // 命中范围比视觉尺寸大，方便抓取
constexpr int kMagnifierSize = 160;
constexpr int kMagnifierSamplePixels = 20;
constexpr int kMagnifierOffset = 22;
constexpr int kMinSelection = 3;
// 短边小于这个值时算"小元素"，不画锚点（否则锚点比内容还大）
constexpr int kAnchorMinSide = 48;

constexpr int kMinCornerRadius = 4;   // 小于这个值肉眼看不出来，按"关闭"处理
constexpr int kDefaultCornerRadius = 12;

// candidate 是否基本覆盖了 full（按 full 的面积算 ≥ 92%）。
// 用于判断"元素链顶端就是整个窗口"：MSAA accLocation / UIA BoundingRectangle
// 上报的窗口矩形常比可见边框小一圈（内缩 1~2px 或只给客户区），
// 而 DWMWA_EXTENDED_FRAME_BOUNDS 才是屏幕上真实可见的窗口边框。
bool coversMostOf(const QRectF &candidate, const QRectF &full)
{
    if (!candidate.isValid() || !full.isValid() || candidate.isEmpty() || full.isEmpty())
        return false;
    const QRectF inter = candidate.intersected(full);
    const qreal fullArea = full.width() * full.height();
    return fullArea > 0 && inter.width() * inter.height() >= fullArea * 0.92;
}

QString defaultFileName()
{
    return QStringLiteral("Snipaste_%1.png")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")));
}

} // namespace

SnipOverlay::SnipOverlay(QWidget *parent)
    : QWidget(parent)
    , m_model(new AnnotationModel(this))
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    // 注意：窗口保持不透明。橡皮擦的"擦除"由绘制逻辑处理
    //（把下方内容还原成遮罩态），而不是靠真正的透明像素，
    // 否则在不透明窗口上会显示成黑色。
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);

    // 截图层专用 QSS：全局 QSS 收窄后只剩字体，颜色规则改挂在这里。
    // 子控件（PaintBar / SizeInputBox / TextInputBox）与截图层内弹出的标准对话框
    // 保持原样式，Ela 对话框完全不受影响。
    setStyleSheet(Theme::overlayStyleSheet());

    m_hexFormat = !ConfigManager::instance()->value(QStringLiteral("interface/uppercase_hex")).toBool();

    // 标注工具栏：截图层的子控件，不抢焦点
    m_paintBar = new PaintBar(this);
    connect(m_paintBar, &PaintBar::toolChanged, this, &SnipOverlay::onToolChanged);
    connect(m_paintBar, &PaintBar::toolCleared, this, [this]() {
        // 取消工具 → 回到可拖动/调整选区的状态
        if (m_state == State::Drawing)
            cancelShape();
        else if (m_state == State::PolylineDrawing)
            finishPolyline();
        m_currentTool = Tool::None;
        update();
    });
    connect(m_paintBar, &PaintBar::colorChanged, this, &SnipOverlay::onColorChanged);
    connect(m_paintBar, &PaintBar::penWidthChanged, this, &SnipOverlay::onPenWidthChanged);
    connect(m_paintBar, &PaintBar::undoRequested, this, [this]() {
        m_model->undo();
        update();
    });
    connect(m_paintBar, &PaintBar::redoRequested, this, [this]() {
        m_model->redo();
        update();
    });
    connect(m_paintBar, &PaintBar::clearRequested, this, [this]() {
        m_model->clear();
        update();
    });
    connect(m_paintBar, &PaintBar::copyRequested, this, [this]() { finishSnip(SnipOutput::Copy); });
    connect(m_paintBar, &PaintBar::saveRequested, this, [this]() { finishSnip(SnipOutput::FileDialog); });
    connect(m_paintBar, &PaintBar::pinRequested, this, [this]() { finishSnip(SnipOutput::Pin); });
    connect(m_paintBar, &PaintBar::closeRequested, this, [this]() { abortSnip(); });
    connect(m_paintBar, &PaintBar::resetNumberRequested, this,
            [](int groupId) { NumberShape::resetNumbering(groupId); });
    // 线型/箭头样式变更：同步到已选中的标注（否则改虚线对已画的图形无效）
    connect(m_paintBar, &PaintBar::styleChanged, this, [this]() {
        const ToolStyle st = ToolStyleStore::instance()->style(m_currentTool);
        for (const auto &shape : m_selectedShapes) {
            if (!shape)
                continue;
            if (auto *line = dynamic_cast<LineShape *>(shape.get())) {
                line->setPenStyle(st.penStyle);
                line->setArrowStyle(static_cast<LineShape::ArrowStyle>(st.arrowStyle));
            } else if (auto *rect = dynamic_cast<RectShape *>(shape.get())) {
                rect->setPenStyle(st.penStyle);
            } else if (auto *fh = dynamic_cast<FreehandShape *>(shape.get())) {
                fh->setPenStyle(st.penStyle);
            } else if (auto *px = dynamic_cast<PixelShape *>(shape.get())) {
                px->invalidateCache();
            }
        }
        update();
    });
    connect(m_paintBar, &PaintBar::sendToRequested, this, &SnipOverlay::sendToApplication);
    connect(m_paintBar, &PaintBar::ocrRequested, this, &SnipOverlay::runOcr);
    connect(m_paintBar, &PaintBar::barcodeRequested, this, &SnipOverlay::runBarcodeScan);
    connect(m_model, &AnnotationModel::undoStateChanged, m_paintBar, &PaintBar::setCanUndoRedo);
    // 新建标注（含自动选中）后必须重绘，否则选中框/golden 锚点要等下一次鼠标移动才出现
    connect(m_model, &AnnotationModel::changed, this, [this]() { update(); });

    // 元素检测：结果在 500ms 内回来才用，超时静默丢弃
    connect(ElementDetector::instance(), &ElementDetector::detected, this,
            [this](quint64 id, const QVector<ElementInfo> &hierarchy) {
                if (id != m_elementRequestId)
                    return;
                m_elementPending = false;
                m_elementHierarchy = hierarchy;
                if (m_elementHierarchy.isEmpty()) {
                    const QRect win = WindowDetector::bestWindowRectAt(m_elementRequestPos);
                    m_hoverWindow = win.isEmpty()
                                        ? QRect()
                                        : QRect(win.topLeft() - virtualTopLeft(), win.size())
                                              .intersected(rect());
                    update();
                } else {
                    // 两种模式都从光标下最深的那个块开始：
                    // Snipaste 的手感就是"指哪块就框哪块"，滚轮往上才是容器/窗口。
                    applyElementLevel(0);
                }

                // 流水线：结果回来时鼠标可能已经移走了，
                // 立刻用**最新位置**再发一次，保证视觉上始终跟手。
                if (m_state == State::Idle && m_detectMode != DetectMode::Free
                    && (m_pendingCursor - m_elementRequestPos).manhattanLength() >= 3) {
                    requestElementDetection();
                }
            });
    connect(ElementDetector::instance(), &ElementDetector::timedOut, this,
            [this](quint64 id) {
                if (id != m_elementRequestId)
                    return;
                m_elementPending = false;
                // 超时后降级为只显示窗口轮廓，不阻塞用户
                m_elementHierarchy.clear();
                update();

                // 超时同样要补发（否则用户移动后就再也不更新了）
                if (m_state == State::Idle && m_detectMode != DetectMode::Free
                    && (m_pendingCursor - m_elementRequestPos).manhattanLength() >= 3) {
                    requestElementDetection();
                }
            });

    m_ocr = new OcrEngine(this);

    // 长按 F5 连续刷新：每 500ms 刷一次，F5 松开后自动停
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(500);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        if (!WinUtil::isFunctionKeyHeld(QStringLiteral("F5"))) {
            m_refreshTimer->stop();
            return;
        }
        refreshSnapshot();
    });

    // 长截图：滚动期间定时抓帧拼接（150ms ≈ 6.7 帧/秒）
    m_longShotTimer = new QTimer(this);
    m_longShotTimer->setInterval(150);
    connect(m_longShotTimer, &QTimer::timeout, this, &SnipOverlay::grabLongShotFrame);
}

SnipOverlay::~SnipOverlay()
{
    // 各工具的样式（颜色/笔宽/虚线段位/圆角）随会话结束写回配置
    ToolStyleStore::instance()->save();

    // m_paintBar 和 m_textBox 都是子控件，由 Qt 父子关系自动销毁，
    // 这里只清指针避免悬空
    m_paintBar = nullptr;
    m_textBox = nullptr;
    PixelShape::clearSourceImage();

    // 长截图的工具栏是独立顶层窗口，必须手动释放
    delete m_longShotBar;
    m_longShotBar = nullptr;

    delete m_infoPanel;
    m_infoPanel = nullptr;

    delete m_magnifierPanel;
    m_magnifierPanel = nullptr;
}

void SnipOverlay::startSession(const ScreenCapturer::Snapshot &snapshot)
{
    m_snapshot = snapshot;
    m_snapshotImage = snapshot.pixmap.toImage();
    invalidateBackgroundCache();

    // 光标位图在这里预取一次：composeResultImage() 是 const，不能在那里懒加载
    m_liveCursorPixmap = QPixmap();
    ensureCursorPixmap();
    m_snapshotImage.setDevicePixelRatio(1.0);

    // 马赛克/模糊需要底图，注入一次
    PixelShape::setSourceImage(m_snapshotImage, snapshot.devicePixelRatio);

    m_state = State::Idle;
    m_selection = QRect();
    m_hoverWindow = QRect();
    m_clickTargetRect = QRect();
    m_clickTargetIsElement = false;
    m_mouseMoved = false;
    m_mouseInside = false;
    m_hoveringTips = false;
    m_historyOffset = -1;
    m_windowDetectionOn = ConfigManager::instance()->value(QStringLiteral("snip/auto_detect_window")).toBool();
    m_showCursor = ConfigManager::instance()->value(QStringLiteral("snip/capture_cursor")).toBool();
    m_showAnchors = true;

    // P4 状态复位
    // 默认直接进元素检测（Snipaste 的手感就是"指哪块框哪块"）。
    // 只有用户显式关掉自动检测时才从自由选区开始。
    m_detectMode = m_windowDetectionOn ? DetectMode::Element : DetectMode::Free;
    m_elementHierarchy.clear();
    m_elementLevel = 0;
    m_elementRequestId = 0;
    m_elementPending = false;
    clearDetection();

    // 圆角：0 = 关闭，>0 = 开启。这里不能 qMax(1, ...)，
    // 否则"关闭"会被读成 1px 圆角，Ctrl+R 打开后看起来毫无变化。
    // 1~3px 在视觉上等于没开，按关闭处理并把配置自愈回 0。
    m_cornerRadius = qBound(0, ConfigManager::instance()
                                   ->value(QStringLiteral("snip/corner_radius")).toInt(), 50);
    if (m_cornerRadius > 0 && m_cornerRadius < kMinCornerRadius) {
        m_cornerRadius = 0;
        ConfigManager::instance()->setValue(QStringLiteral("snip/corner_radius"), 0);
    }
    m_roundedCorners = m_cornerRadius > 0;
    m_shadowPreview = ConfigManager::instance()->value(QStringLiteral("snip/shadow_enabled")).toBool();

    // 比例锁定：记住上次用的预设，但**不自动启用**。
    // 上次锁了 16:9 不代表这次也想锁——否则用户一拖鼠标选区就被锁死，
    // 看起来像"选区不跟手"。要锁比例请用右键菜单或按住 Ctrl 拖。
    const QString lastPreset =
        ConfigManager::instance()->value(QStringLiteral("snip/aspect_preset")).toString();
    m_aspect.preset = lastPreset.isEmpty() ? QStringLiteral("free") : lastPreset;
    m_aspect.enabled = false;
    m_aspect.ratio = 0.0;
    m_dragAspectRatio = 0.0;

    m_model->clear();
    m_currentTool = Tool::None;
    m_inProgress.reset();
    m_polylinePoints.clear();
    m_paintBarVisible = false;
    clearSelectionSilently();
    m_movingShape.reset();
    m_resizeShapeHandleIndex = -1;
    m_rotatingShape.reset();
    m_lassoPoints.clear();
    m_rectSelectRect = QRect();
    m_rectSelectActive = false;
    m_magnifyTargetDragging = false;
    NumberShape::resetAllNumbering();

    setGeometry(snapshot.virtualGeometry);
    show();
    raise();
    activateWindow();

    // 登记遮罩 HWND：窗口检测只排除它自己，本进程的其它窗口仍可被识别
    WindowDetector::setOverlayWindow(static_cast<quintptr>(winId()));
    setFocus(Qt::OtherFocusReason);

    m_hoverThrottle.start();
    m_moveTimer.start();
}

void SnipOverlay::presetArea(const QRect &globalRect)
{
    if (globalRect.isEmpty())
        return;

    m_selection = QRect(globalRect.topLeft() - virtualTopLeft(), globalRect.size())
                      .intersected(rect());
    if (m_selection.width() < kMinSelection || m_selection.height() < kMinSelection)
        return;

    m_state = State::Adjusting;
    showPaintBar();
    update();
}

// ---------------------------------------------------------------- 坐标换算

QPoint SnipOverlay::toLocal(const QPoint &globalPos) const
{
    return mapFromGlobal(globalPos);
}

QPoint SnipOverlay::virtualTopLeft() const
{
    return m_snapshot.virtualGeometry.topLeft();
}

QRect SnipOverlay::localSelection() const
{
    return m_selection.normalized();
}

QRect SnipOverlay::globalSelection() const
{
    const QRect sel = localSelection();
    if (sel.isEmpty())
        return {};
    return QRect(sel.topLeft() + virtualTopLeft(), sel.size());
}

// ---------------------------------------------------------------- 状态机

SnipOverlay::Anchor SnipOverlay::hitTestAnchor(const QPoint &localPos) const
{
    if (localSelection().isEmpty())
        return Anchor::None;

    const QRect r = localSelection();
    const int t = kAnchorHitTolerance * 2;   // 命中框边长
    const QPoint pts[8] = {
        r.topLeft(), QPoint(r.center().x(), r.top()), r.topRight(),
        QPoint(r.right(), r.center().y()), r.bottomRight(),
        QPoint(r.center().x(), r.bottom()), r.bottomLeft(),
        QPoint(r.left(), r.center().y())
    };
    static const Anchor anchors[8] = { Anchor::NW, Anchor::N, Anchor::NE, Anchor::E,
                                       Anchor::SE, Anchor::S, Anchor::SW, Anchor::W };

    for (int i = 0; i < 8; ++i) {
        const QRect hit(pts[i].x() - t / 2, pts[i].y() - t / 2, t, t);
        if (hit.contains(localPos))
            return anchors[i];
    }
    return Anchor::None;
}

bool SnipOverlay::hitTestSelection(const QPoint &localPos) const
{
    return !localSelection().isEmpty() && localSelection().contains(localPos);
}

void SnipOverlay::beginResize(Anchor anchor)
{
    m_state = State::Resizing;
    m_movingShape.reset();
    m_resizeShapeHandleIndex = -1;
    m_activeAnchor = anchor;
    m_resizeOriginRect = localSelection();
    m_dragOrigin = m_resizeOriginRect.topLeft();
    m_dragStart = toLocal(QCursor::pos());
}

// 比例锁定：把矩形修正到目标宽高比。
// 拖角时以对角为不动点；拖边时以对边中点为不动点。
void SnipOverlay::applyAspectToRect(QRect *r, Anchor anchor) const
{
    if (!m_aspect.enabled || m_aspect.ratio <= 0.0 || !r)
        return;

    const int w = r->width();
    const int h = r->height();
    if (w < 1 || h < 1)
        return;

    // 以"变化更大的边"为准，另一边跟随，避免抖动
    int newW = w;
    int newH = h;
    if (qAbs(w / m_aspect.ratio - h) >= qAbs(h * m_aspect.ratio - w)) {
        newH = qMax(1, qRound(w / m_aspect.ratio));
    } else {
        newW = qMax(1, qRound(h * m_aspect.ratio));
    }

    const QPoint fixed =
        (anchor == Anchor::NW) ? r->bottomRight()
        : (anchor == Anchor::NE) ? r->bottomLeft()
        : (anchor == Anchor::SE) ? r->topLeft()
        : (anchor == Anchor::SW) ? r->topRight()
        : r->center();

    QRect adjusted(fixed.x(), fixed.y(), 1, 1);
    switch (anchor) {
    case Anchor::NW: adjusted = QRect(fixed - QPoint(newW, newH), QSize(newW, newH)); break;
    case Anchor::NE: adjusted = QRect(QPoint(fixed.x() - newW, fixed.y()), QSize(newW, newH)); break;
    case Anchor::SE: adjusted = QRect(fixed, QSize(newW, newH)); break;
    case Anchor::SW: adjusted = QRect(QPoint(fixed.x(), fixed.y() - newH), QSize(newW, newH)); break;
    default:
        // 拖边 / 新建选区：围绕中心展开
        adjusted = QRect(fixed.x() - newW / 2, fixed.y() - newH / 2, newW, newH);
        break;
    }
    *r = adjusted;
}

void SnipOverlay::updateResize(const QPoint &localPos)
{
    QRect r = m_resizeOriginRect;
    const QPoint d = localPos - m_dragStart;

    switch (m_activeAnchor) {
    case Anchor::NW: r.setTopLeft(r.topLeft() + d); break;
    case Anchor::N:  r.setTop(r.top() + d.y()); break;
    case Anchor::NE: r.setTopRight(r.topRight() + d); break;
    case Anchor::E:  r.setRight(r.right() + d.x()); break;
    case Anchor::SE: r.setBottomRight(r.bottomRight() + d); break;
    case Anchor::S:  r.setBottom(r.bottom() + d.y()); break;
    case Anchor::SW: r.setBottomLeft(r.bottomLeft() + d); break;
    case Anchor::W:  r.setLeft(r.left() + d.x()); break;
    default: break;
    }
    r = r.normalized();

    // Ctrl 拖动时临时锁定比例（放开 Ctrl 恢复自由）
    const bool ctrl = QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
    if (m_aspect.enabled || ctrl) {
        AspectState saved = m_aspect;
        if (ctrl && !m_aspect.enabled) {
            m_aspect.enabled = true;
            m_aspect.ratio = qreal(m_resizeOriginRect.width())
                             / qMax(1, m_resizeOriginRect.height());
        }
        applyAspectToRect(&r, m_activeAnchor);
        m_aspect = saved;
    }

    // 钳制在窗口内
    r = r.intersected(rect());
    if (r.width() < kMinSelection || r.height() < kMinSelection)
        return;
    m_selection = r;
    if (m_paintBarVisible)
        syncPaintBarPosition();
    update();
}

void SnipOverlay::updateSelectionDrag(const QPoint &localPos)
{
    QPoint end = localPos;

    // Ctrl 拖拽新建选区 = 保持拖拽开始时的宽高比（没有则 16:9）
    const bool ctrl = QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
    const qreal ratio = ctrl ? (m_dragAspectRatio > 0.0 ? m_dragAspectRatio : 16.0 / 9.0)
                             : m_aspect.ratio;
    if (ctrl || m_aspect.enabled) {
        const int dx = end.x() - m_dragStart.x();
        const int dy = end.y() - m_dragStart.y();
        const int w = qMax(qAbs(dx), 1);
        const int h = qMax(qAbs(dy), 1);
        if (qAbs(w / ratio - h) >= qAbs(h * ratio - w))
            end.setY(m_dragStart.y() + (dy >= 0 ? 1 : -1) * qMax(1, qRound(w / ratio)));
        else
            end.setX(m_dragStart.x() + (dx >= 0 ? 1 : -1) * qMax(1, qRound(h * ratio)));
    }

    QRect r(m_dragStart, end);
    r = r.normalized().intersected(rect());
    m_selection = r;
    update();
}

// ---------------------------------------------------------------- 事件

void SnipOverlay::mousePressEvent(QMouseEvent *e)
{
    // 长截图态：鼠标穿透生效时事件到不了这里；兜底一律忽略，
    // 防止拖动把定死的捕获视口改掉
    if (m_longShotState != LongShotState::Off)
        return;

    if (e->button() == Qt::RightButton) {
        m_suppressContextMenu = false;
        // 折线：右键结束
        if (m_state == State::PolylineDrawing) {
            finishPolyline();
            return;
        }
        // 套索：右键闭合
        if (m_state == State::LassoSelecting) {
            if (m_rectSelectActive) {
                m_rectSelectRect = QRect();
                m_rectSelectActive = false;
                m_state = State::Adjusting;
                update();
            } else {
                closeLasso();
            }
            return;
        }
        // 放大镜第二阶段：右键放弃
        if (m_state == State::MagnifyTarget) {
            m_inProgress.reset();
            m_magnifyTargetDragging = false;
            m_state = State::Adjusting;
            update();
            return;
        }
        // 绘制中：右键结束当前形状
        if (m_state == State::Drawing) {
            finishShape();
            return;
        }
        if (m_state == State::Dragging) {
            m_state = State::Idle;
            m_selection = QRect();
            update();
            return;
        }

        // 右键逐级"退出当前操作"，避免用户想取消工具时误退出整个截图：
        //   1. 有选中标注 → 取消选中
        //   2. 有激活工具 → 取消工具
        //   3. 都没有 → 才是取消截图（保持原有语义）
        if (!m_selectedShapes.empty()) {
            m_suppressContextMenu = true;
            clearSelection();
            update();
            return;
        }
        if (m_currentTool != Tool::None) {
            m_suppressContextMenu = true;
            if (m_paintBar)
                m_paintBar->toggleTool(m_currentTool);   // 再次点击同一工具 = 取消
            update();
            return;
        }

        // 已有选区但无工具/无选中：右键 = 打开选区菜单（比例/圆角/阴影/尺寸/发送到）。
        // 菜单在 contextMenuEvent 里弹出，这里不设 suppress，让菜单正常弹。
        if (!localSelection().isEmpty()) {
            update();
            return;
        }

        abortSnip();
        return;
    }

    if (e->button() != Qt::LeftButton)
        return;

    const QPoint local = e->position().toPoint();
    m_mouseLocal = local;
    m_mouseInside = true;
    const bool ctrl = e->modifiers().testFlag(Qt::ControlModifier);

    // Alt + 单击 = 直接选中光标下整个窗口。
    // 默认单击落在"最具体的元素块"上，窗口里按钮密集时很难正好点中
    // 能代表整窗的空白处，需要一个明确入口。
    if (e->modifiers().testFlag(Qt::AltModifier) && m_state == State::Idle
        && localSelection().isEmpty()) {
        const QRect win = WindowDetector::bestWindowRectAt(e->globalPosition().toPoint());
        if (!win.isEmpty()) {
            m_selection = QRect(win.topLeft() - virtualTopLeft(), win.size())
                              .intersected(rect());
            if (!m_selection.isEmpty()) {
                m_state = State::Adjusting;
                // 也是可选中的选区，和手动/元素选区一样给 8 个锚点，方便微调
                m_showAnchors = true;
                clearDetection();
                showPaintBar();
                update();
                return;
            }
        }
    }

    // 点尺寸标签 = 输入精确宽高
    if (m_state == State::Adjusting && !localSelection().isEmpty()
        && sizeLabelRect().contains(local) && !m_sizeBox) {
        beginSizeInput();
        return;
    }

    // 折线绘制中：左键加一个顶点
    if (m_state == State::PolylineDrawing) {
        addPolylinePoint(local);
        return;
    }

    // 放大镜第二阶段：再次拖拽确定目标区域
    if (m_state == State::MagnifyTarget) {
        m_shapeStart = local;
        m_magnifyTargetDragging = true;
        return;
    }

    // ---- 选区锚点最先判：即使有选中标注，用户也可能想调整截图区域 ----
    // （以前形状拖动排在前面，导致选中标注后选区锚点完全拖不动）
    if (m_state == State::Adjusting && !localSelection().isEmpty()) {
        const Anchor selAnchor = hitTestAnchor(local);
        if (selAnchor != Anchor::None) {
            beginResize(selAnchor);
            return;
        }
    }

    // ---- 编辑态：已选中形状时，旋转点 > 角手柄旋转 > 锚点 > Ctrl拖克隆 > 拖动 ----
    if (m_state == State::Adjusting && !m_selectedShapes.empty()) {
        if (hitTestRotationHandle(local, nullptr)) {
            beginRotate(local);
            return;
        }

        int handleIndex = -1;
        std::shared_ptr<Annotation> handleShape;
        if (hitTestSelectedHandle(local, &handleIndex, &handleShape)) {
            // 角手柄（0/2/4/6）+ Alt = 旋转。
            // 角手柄既是缩放抓手又要能转，用 Alt 区分，不再额外画一个旋转点引线。
            const bool isCorner = (handleIndex % 2) == 0;
            if (isCorner && e->modifiers().testFlag(Qt::AltModifier) && handleShape
                && handleShape->isRotatable()) {
                m_rotatingShape = handleShape;
                const QPointF center = handleShape->boundingRect().center();
                m_rotateStartAngle = QLineF(center, QPointF(local)).angle();
                m_rotateStartRotation = handleShape->rotation();
                m_state = State::Resizing;
                m_resizeShapeHandleIndex = -1;
                m_activeAnchor = Anchor::None;
                return;
            }

            m_state = State::Resizing;
            m_activeAnchor = Anchor::None;
            m_dragStart = local;
            m_resizeShapeHandleIndex = handleIndex;
            m_movingShape = handleShape;   // 锚点操作针对单个形状
            // 让形状记录拖拽基准（文字缩字号必须基于固定基准，否则会累积放大）
            if (handleShape)
                handleShape->beginHandleDrag();
            return;
        }

        for (const auto &shape : m_selectedShapes) {
            if (!shape->hitTest(QPointF(local), 4.0))
                continue;

            if (ctrl && m_currentTool == Tool::None) {
                // Ctrl + 拖拽 = 克隆一份并跟随鼠标
                beginCloneDrag(local);
                return;
            }

            m_state = State::Moving;
            m_dragStart = local;
            m_dragOrigin = local;
            m_movingShape = shape;
            return;
        }

        // 点到空白处：取消选中，继续走选新形状 / 选区逻辑。
        // 但画笔工具激活时不取消：那一下是要开始画新标注（套索/画笔等），
        // 清掉已有选中会让"套索画太小 = 误点"这类情况把选中一起丢失。
        if (m_currentTool == Tool::None)
            clearSelection();
    }

    // 标注模式优先：工具栏可见且已选工具时进入绘制
    if (m_paintBarVisible && m_currentTool != Tool::None
        && m_state == State::Adjusting && !localSelection().isEmpty()) {
        if (m_currentTool == Tool::Text) {
            // 文字工具激活时点到**已有文字**：进入拖动，而不是叠一个新输入框
            const auto hit = m_model->hitTestShape(QPointF(local), 2.0);
            if (hit && hit->type() == Annotation::Type::Text) {
                clearSelectionSilently();
                hit->setSelected(true);
                m_selectedShapes.push_back(hit);
                m_state = State::Moving;
                m_dragStart = local;
                m_dragOrigin = local;
                m_movingShape = hit;
                update();
                return;
            }
            beginTextInput(local);
            return;
        }
        if (m_currentTool == Tool::Polyline) {
            beginPolyline(local);
            return;
        }
        if (m_currentTool == Tool::Number) {
            placeNumber(local);
            return;
        }
        if (m_currentTool == Tool::Magnify) {
            beginMagnify(local);
            return;
        }
        if (m_currentTool == Tool::Lasso) {
            // Shift + 拖 = 矩形框选，否则点顶点画套索
            if (e->modifiers().testFlag(Qt::ShiftModifier))
                beginRectSelect(local);
            else
                beginLasso(local);
            return;
        }
        beginShape(local);
        return;
    }

    // 未选工具时：点已有形状 → 选中；Shift/Ctrl 组合做套索或框选
    if (m_state == State::Adjusting && m_currentTool == Tool::None) {
        if (e->modifiers().testFlag(Qt::ShiftModifier)) {
            beginRectSelect(local);
            return;
        }
        const bool hadSelection = !m_selectedShapes.empty();
        selectShapeAt(local, ctrl);
        if (!m_selectedShapes.empty())
            return;
        // 刚从选中态点了空白：只取消选中，不要顺手开始框新选区
        // （否则"点别处取消选中"会变成"重新拉一个选区"）
        if (hadSelection) {
            update();
            return;
        }
    }

    if (m_state == State::Adjusting) {
        const Anchor a = hitTestAnchor(local);
        if (a != Anchor::None) {
            beginResize(a);
            return;
        }
        if (hitTestSelection(local)) {
            m_state = State::Moving;
            m_dragStart = local;
            m_dragOrigin = localSelection().topLeft();
            m_movingShape.reset();
            return;
        }
    }

    // 注意：检测模式不在按下时直接选中窗口/元素——否则鼠标一动就再也拉不出自由选区。
    // 按下统一进入 Dragging，松手时若位移很小（等于"点了一下"）再落到检测结果上。
    //
    // 一旦用户开始手动拖拽，立刻停止元素/窗口识别：
    // 继续识别会让高亮块在手动画的选区上乱闪，还会误导用户以为是自动选区。
    m_clickTargetRect = m_hoverWindow;
    m_clickTargetIsElement = !m_elementHierarchy.isEmpty();
    clearDetection();
    m_state = State::Dragging;
    m_dragStart = local;
    m_dragOrigin = local;
    m_dragAspectRatio = localSelection().isEmpty()
                            ? 0.0
                            : qreal(localSelection().width()) / qMax(1, localSelection().height());
    m_selection = QRect(local, QSize(0, 0));
    update();
}

void SnipOverlay::mouseMoveEvent(QMouseEvent *e)
{
    if (m_longShotState != LongShotState::Off)
        return;

    const QPoint local = e->position().toPoint();
    if (local != m_mouseLocal)
        m_mouseMoved = true;
    m_mouseLocal = local;
    m_mouseInside = true;

    // 鼠标移到左下角帮助浮层上时临时隐藏它（露出下面的画面），移开即恢复。
    if (shortcutTipsRect().contains(local) != m_hoveringTips) {
        m_hoveringTips = shortcutTipsRect().contains(local);
        update();
    }

    switch (m_state) {
    case State::PolylineDrawing:
        updatePolylinePreview(local);
        break;
    case State::Drawing:
        updateShape(local);
        break;
    case State::MagnifyTarget:
        if (m_magnifyTargetDragging)
            updateShape(local);
        break;
    case State::LassoSelecting:
        if (m_rectSelectActive) {
            updateRectSelect(local);
        } else {
            // 按住拖拽画自由曲线
            addLassoPoint(local);
        }
        break;
    case State::Resizing:
        if (m_rotatingShape) {
            updateRotate(local);
        } else if (m_resizeShapeHandleIndex >= 0 && m_movingShape) {
            // Shift + 拖角 = 保持宽高比（矩形/椭圆）
            auto *rectShape = dynamic_cast<RectShape *>(m_movingShape.get());
            const bool isCorner = (m_resizeShapeHandleIndex % 2) == 0;
            const bool shiftHeld = QGuiApplication::keyboardModifiers()
                                       .testFlag(Qt::ShiftModifier);
            if (rectShape && isCorner && shiftHeld) {
                QPointF local2 = QPointF(local);
                if (!qFuzzyIsNull(m_movingShape->rotation())) {
                    local2 = m_movingShape->rotationTransform().inverted().map(local2);
                }
                rectShape->moveHandleKeepAspect(m_resizeShapeHandleIndex, local2);
            } else {
                m_movingShape->moveHandle(m_resizeShapeHandleIndex, QPointF(local));
            }
            m_movingShape->invalidateCache();
            update();
        } else {
            updateResize(local);
        }
        break;

    case State::Dragging:
        updateSelectionDrag(local);
        // 拖拽建选区时也让工具栏跟随（选区已确定后才显示，这里是保险）
        if (m_paintBarVisible)
            syncPaintBarPosition();
        break;
    case State::Moving: {
        if (m_movingShape) {
            // 拖动标注形状：按增量平移，每次把 dragOrigin 推到当前位置。
            // 多选时所有选中形状一起移动（锚点编辑另一形状的选中框也一起走）
            const QPointF delta = QPointF(local - m_dragOrigin);
            m_movingShape->translate(delta);
            m_movingShape->invalidateCache();
            for (const auto &s : m_selectedShapes) {
                if (s != m_movingShape) {
                    s->translate(delta);
                    s->invalidateCache();
                }
            }
            m_dragOrigin = local;
            update();
            break;
        }
        QPoint newTopLeft = m_dragOrigin + (local - m_dragStart);
        const QRect cur = localSelection();
        // 钳制
        newTopLeft.setX(qBound(0, newTopLeft.x(), width() - cur.width()));
        newTopLeft.setY(qBound(0, newTopLeft.y(), height() - cur.height()));
        m_selection = QRect(newTopLeft, cur.size());
        // 工具栏跟着选区走
        if (m_paintBarVisible)
            syncPaintBarPosition();
        update();
        break;
    }
    case State::Adjusting:
    case State::Idle: {
        // 已选中标注的形状手柄永远优先：画完图形会自动选中且工具仍激活，
        // 不先判手柄的话会一路落到"工具激活＝十字"，拖拽点永远是十字。
        if (!m_selectedShapes.empty() && m_state == State::Adjusting) {
            if (hitTestRotationHandle(local, nullptr)) {
                setCursor(Qt::CrossCursor);   // 旋转点：按住 Alt/角手柄旋转
            } else {
                int handleIndex = -1;
                std::shared_ptr<Annotation> handleShape;
                if (hitTestSelectedHandle(local, &handleIndex, &handleShape)) {
                    const bool isCorner = (handleIndex % 2) == 0;
                    if (isCorner)
                        setCursor(Qt::SizeFDiagCursor);
                    else if (handleIndex == 1 || handleIndex == 5)
                        setCursor(Qt::SizeVerCursor);
                    else
                        setCursor(Qt::SizeHorCursor);
                } else {
                    bool onShape = false;
                    for (const auto &shape : m_selectedShapes) {
                        if (shape && shape->hitTest(QPointF(local), 4.0)) {
                            onShape = true;
                            break;
                        }
                    }
                    if (onShape) {
                        setCursor(Qt::SizeAllCursor);
                    } else if (m_currentTool != Tool::None && m_paintBarVisible) {
                        const Anchor a = hitTestAnchor(local);
                        switch (a) {
                        case Anchor::NW:
                        case Anchor::SE: setCursor(Qt::SizeFDiagCursor); break;
                        case Anchor::NE:
                        case Anchor::SW: setCursor(Qt::SizeBDiagCursor); break;
                        case Anchor::N:
                        case Anchor::S:  setCursor(Qt::SizeVerCursor);  break;
                        case Anchor::E:
                        case Anchor::W:  setCursor(Qt::SizeHorCursor);  break;
                        case Anchor::None: setCursor(Qt::CrossCursor);  break;
                        }
                    } else {
                        const Anchor a = hitTestAnchor(local);
                        switch (a) {
                        case Anchor::NW:
                        case Anchor::SE: setCursor(Qt::SizeFDiagCursor); break;
                        case Anchor::NE:
                        case Anchor::SW: setCursor(Qt::SizeBDiagCursor); break;
                        case Anchor::N:
                        case Anchor::S:  setCursor(Qt::SizeVerCursor);  break;
                        case Anchor::E:
                        case Anchor::W:  setCursor(Qt::SizeHorCursor);  break;
                        case Anchor::None:
                            setCursor(hitTestSelection(local) ? Qt::SizeAllCursor
                                                              : Qt::CrossCursor);
                            break;
                        }
                    }
                }
            }
        } else if (m_currentTool != Tool::None && m_paintBarVisible) {
            // 选中工具后，整个画布（含选区内部）统一是十字光标：
            // 此时鼠标的唯一用途就是"落笔"，在选区里显示抓手/移动光标会误导。
            // 只有拖动选区锚点时仍保留缩放光标，那是唯一需要区分的手势。
            const Anchor a = hitTestAnchor(local);
            switch (a) {
            case Anchor::NW:
            case Anchor::SE: setCursor(Qt::SizeFDiagCursor); break;
            case Anchor::NE:
            case Anchor::SW: setCursor(Qt::SizeBDiagCursor); break;
            case Anchor::N:
            case Anchor::S:  setCursor(Qt::SizeVerCursor);  break;
            case Anchor::E:
            case Anchor::W:  setCursor(Qt::SizeHorCursor);  break;
            case Anchor::None: setCursor(Qt::CrossCursor);  break;
            }
        } else if (m_state == State::Adjusting && !localSelection().isEmpty()) {
            // 未选工具：选区锚点给缩放光标，选区内给移动光标，其余十字
            const Anchor a = hitTestAnchor(local);
            switch (a) {
            case Anchor::NW:
            case Anchor::SE: setCursor(Qt::SizeFDiagCursor); break;
            case Anchor::NE:
            case Anchor::SW: setCursor(Qt::SizeBDiagCursor); break;
            case Anchor::N:
            case Anchor::S:  setCursor(Qt::SizeVerCursor);  break;
            case Anchor::E:
            case Anchor::W:  setCursor(Qt::SizeHorCursor);  break;
            case Anchor::None:
                setCursor(hitTestSelection(local) ? Qt::SizeAllCursor : Qt::CrossCursor);
                break;
            }
        } else {
            setCursor(Qt::CrossCursor);
        }

        updateHoverWindow();
        updateMagnifier();
        update();
        break;
    }
    }

    if (m_state == State::Drawing || m_state == State::Dragging
        || m_state == State::Resizing || m_state == State::Moving) {
        update();
    }
}

void SnipOverlay::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_longShotState != LongShotState::Off)
        return;

    if (e->button() != Qt::LeftButton)
        return;

    if (m_state == State::PolylineDrawing) {
        // 折线的点已在 mousePress 里加过，这里不做事
        return;
    }

    if (m_state == State::Drawing) {
        // 拖拽模式的形状松手即完成（放大镜第一阶段结束，进入第二阶段）
        finishShape();
        return;
    }

    if (m_state == State::MagnifyTarget) {
        if (m_magnifyTargetDragging)
            confirmMagnifyTarget(e->position().toPoint());
        return;
    }

    if (m_state == State::LassoSelecting) {
        // 框选 / 套索都是松手即完成
        if (m_rectSelectActive)
            finishRectSelect();
        else
            closeLasso();
        return;
    }

    if (m_state == State::Resizing && m_rotatingShape) {
        m_rotatingShape.reset();
        m_state = State::Adjusting;
        update();
        return;
    }

    if (m_state == State::Resizing && m_resizeShapeHandleIndex >= 0) {
        if (m_movingShape)
            m_movingShape->endHandleDrag();
        m_state = State::Adjusting;
        m_resizeShapeHandleIndex = -1;
        m_movingShape.reset();
        update();
        return;
    }

    if (m_state == State::Moving && m_movingShape) {
        m_state = State::Adjusting;
        m_movingShape.reset();
        update();
        return;
    }

    if (m_state == State::Dragging) {
        const QRect r = localSelection();
        if (r.width() < kMinSelection || r.height() < kMinSelection) {
            m_state = State::Idle;
            m_selection = QRect();

            // 位移极小 = 单击：检测模式下把检测到的窗口/元素变成选区。
            // 必须取"当前高亮的那一层"（滚轮调过的 m_elementLevel），
            // 不能一律用整窗或最深元素，否则滚轮选父/子块的结果会被丢弃。
            if (m_detectMode != DetectMode::Free) {
                // 优先用按下左键那一刻的高亮目标：按下时已经 clearDetection()，
                // 此时 m_hoverWindow / m_elementHierarchy 都是空的。
                // 以前直接读它们 → 空 → 退回 bestWindowRectAt()（最顶层窗口，
                // 最大化窗口就是整屏），这就是"点元素却截了整屏"的原因。
                // 坐标系说明：m_clickTargetRect 是局部坐标，而 hierarchy 的矩形
                // 与 bestWindowRectAt() 都是全局逻辑坐标，统一凑成全局后
                // 最后一次性换算回局部。
                QRect target = m_clickTargetRect;
                if (!target.isEmpty()) {
                    target.translate(virtualTopLeft());
                } else if (!m_elementHierarchy.isEmpty()) {
                    const int level = qBound(0, m_elementLevel, m_elementHierarchy.size() - 1);
                    target = m_elementHierarchy.at(level).logicalRect.toAlignedRect();
                    // 链顶端 = 整窗层 → 换成 DWM 可见边框，和高亮一致（严丝合缝）
                    if (level == m_elementHierarchy.size() - 1) {
                        const QRect win =
                            WindowDetector::bestWindowRectAt(e->globalPosition().toPoint());
                        if (!win.isEmpty() && coversMostOf(QRectF(target), QRectF(win)))
                            target = win;
                    }
                }
                if (target.isEmpty())
                    target = WindowDetector::bestWindowRectAt(e->globalPosition().toPoint());

                if (!target.isEmpty()) {
                    m_selection = QRect(target.topLeft() - virtualTopLeft(), target.size())
                                      .intersected(rect());
                    if (!m_selection.isEmpty()) {
                        m_state = State::Adjusting;
                        // 元素块自动选区：显示 8 个锚点，方便微调；
                        // 整窗自动选区：只给方框（窗口边界通常不需要逐像素调），
                        // 用户想微调可以拖动方框或直接手画。
                        // 只有"元素且足够大"才给锚点；整窗或小元素只给方框
                        m_showAnchors = m_clickTargetIsElement
                                        && qMin(m_selection.width(), m_selection.height())
                                               >= kAnchorMinSide;
                        m_clickTargetRect = QRect();
                        clearDetection();
                        showPaintBar();
                        update();
                        return;
                    }
                }
            }

            hidePaintBar();
        } else {
            m_state = State::Adjusting;
            // 选区确定后自动显示工具栏，不用再按空格。
            // 锚点只在选区"够大"时显示：太小的话 8 个锚点会盖住选区本身。
            m_showAnchors = qMin(r.width(), r.height()) >= kAnchorMinSide;
            showPaintBar();
        }
        update();
    } else if (m_state == State::Resizing || m_state == State::Moving) {
        m_state = State::Adjusting;
        const QRect r2 = localSelection();
        m_showAnchors = qMin(r2.width(), r2.height()) >= kAnchorMinSide;
        if (!m_paintBarVisible)
            showPaintBar();
        else
            syncPaintBarPosition();
        update();
    }
}

void SnipOverlay::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (m_longShotState != LongShotState::Off)
        return;

    if (e->button() != Qt::LeftButton)
        return;

    // 折线双击 = 结束
    if (m_state == State::PolylineDrawing) {
        finishPolyline();
        return;
    }

    // 套索双击 = 闭合
    if (m_state == State::LassoSelecting && !m_rectSelectActive) {
        closeLasso();
        return;
    }

    if (m_state == State::Adjusting) {
        // 双击文字本身 → 重新编辑内容（位置沿用文字原位置）
        const QPoint local = e->position().toPoint();
        if (m_selectedShapes.size() == 1) {
            const auto &sel = m_selectedShapes.front();
            if (sel->type() == Annotation::Type::Text && sel->hitTest(QPointF(local), 2.0)) {
                beginTextInput(local);
                return;
            }
            // 双击矩形不再弹圆角输入框（画图时极易误触，模态框会卡住整个会话）。
            // 圆角半径改由选区右键菜单显式触发：[圆角半径…]。
            if (sel->type() == Annotation::Type::Rectangle && sel->hitTest(QPointF(local), 4.0)) {
                showSnipMenu(mapToGlobal(local));
                return;
            }
            return;
        }

        // 没有选中任何形状时双击文字：直接进入编辑（Snipaste 的交互就是这样，
        // 不需要先选中再点工具）
        if (m_selectedShapes.empty() && m_currentTool == Tool::None) {
            const auto hit = m_model->hitTestShape(QPointF(local), 2.0);
            if (hit && hit->type() == Annotation::Type::Text) {
                clearSelectionSilently();
                hit->setSelected(true);
                m_selectedShapes.push_back(hit);
                update();
                beginTextInput(local);
                return;
            }
        }

        if (!m_selectedShapes.empty())
            return;
        finishSnip(SnipOutput::Copy);
    }
}

void SnipOverlay::wheelEvent(QWheelEvent *e)
{
    // 长截图态：滚轮要滚动下层页面。鼠标穿透生效时事件根本到不了这里；
    // 万一穿透失效，把滚轮转发给光标下的窗口兜底
    if (m_longShotState != LongShotState::Off) {
        WinUtil::forwardWheel(winId(), e->angleDelta().y(),
                              e->globalPosition().toPoint());
        return;
    }

    // Shift + 滚轮：缩放选中图形
    if (e->modifiers().testFlag(Qt::ShiftModifier) && !m_selectedShapes.empty()) {
        scaleSelectedByWheel(e->angleDelta().y());
        e->accept();
        return;
    }

    // 检测模式（元素/窗口）：滚轮切换父/子层级（上滚 = 父级，下滚 = 更深的控件）
    if (m_detectMode != DetectMode::Free && m_state == State::Idle
        && !m_elementHierarchy.isEmpty()) {
        changeElementLevel(e->angleDelta().y() > 0 ? 1 : -1);
        e->accept();
        return;
    }

    // Ctrl + 滚轮：调整圆角半径（选区已确定时）
    if (e->modifiers().testFlag(Qt::ControlModifier) && m_roundedCorners
        && m_state == State::Adjusting && !localSelection().isEmpty()) {
        adjustCornerRadius(e->angleDelta().y() > 0 ? 2 : -2);
        e->accept();
        return;
    }

    e->ignore();
}

void SnipOverlay::keyPressEvent(QKeyEvent *e)
{
    // 长截图态：截图层已把焦点让给下层窗口，这里只是兜底
    if (m_longShotState != LongShotState::Off)
        return;

    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    const bool shift = e->modifiers() & Qt::ShiftModifier;

    switch (e->key()) {
    case Qt::Key_Escape:
        if (m_state == State::PolylineDrawing) {
            finishPolyline();
            return;
        }
        if (m_state == State::LassoSelecting) {
            m_lassoPoints.clear();
            m_rectSelectRect = QRect();
            m_rectSelectActive = false;
            m_state = State::Adjusting;
            update();
            return;
        }
        if (m_state == State::MagnifyTarget) {
            m_inProgress.reset();
            m_magnifyTargetDragging = false;
            m_state = State::Adjusting;
            update();
            return;
        }
        if (m_state == State::Drawing) {
            cancelShape();
            return;
        }
        if (!m_selectedShapes.empty()) {
            clearSelection();
            return;
        }
        abortSnip();
        return;

    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (!m_selectedShapes.empty()) {
            deleteSelected();
            return;
        }
        break;

    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_state == State::LassoSelecting) {
            closeLasso();
            return;
        }
        if (m_state == State::MagnifyTarget) {
            confirmMagnifyTarget(m_mouseLocal);
            return;
        }
        finishSnip(SnipOutput::Copy);
        return;

    case Qt::Key_C:
        if (ctrl && shift) {
            copySelectedToInternal();     // Ctrl+Shift+C 复制选中标注
            return;
        }
        if (ctrl) {
            finishSnip(SnipOutput::Copy);
            return;
        }
        if (m_magnifierOn || m_mouseInside) {
            pickColor();
            return;
        }
        break;

    case Qt::Key_V:
        if (ctrl && shift) {
            pasteInternalClipboard();   // Ctrl+Shift+V 粘贴标注
            return;
        }
        break;

    case Qt::Key_Z:
        if (ctrl && shift) {
            m_model->clear();       // Ctrl+Shift+Z = 清除所有标注
            clearSelectionSilently();
            update();
            return;
        }
        if (ctrl) {
            m_model->undo();
            clearSelectionSilently();
            update();
            return;
        }
        break;

    case Qt::Key_Y:
        if (ctrl) {
            m_model->redo();
            update();
            return;
        }
        break;

    case Qt::Key_S:
        if (ctrl && shift) {
            finishSnip(SnipOutput::QuickSave);
            return;
        }
        if (ctrl) {
            finishSnip(SnipOutput::FileDialog);
            return;
        }
        break;   // 无修饰键时落到 WASD 处理

    case Qt::Key_T:
        if (ctrl) {
            finishSnip(SnipOutput::Pin);
            return;
        }
        break;

    case Qt::Key_A:
        if (ctrl) {
            resetToFullScreen();
            return;
        }
        break;   // 无修饰键时落到 WASD 处理

    case Qt::Key_R:
        if (ctrl && shift) {
            // Ctrl+Shift+R 重置序号（重置当前工具的分组）
            const int group = ToolStyleStore::instance()->style(Tool::Number).numberGroup;
            NumberShape::resetNumbering(group);
            return;
        }
        if (ctrl) {
            toggleRoundedCorners();
            return;
        }
        if (!ctrl) {
            resetToLastSnip();
            return;
        }
        break;

    case Qt::Key_F5:
        // 长按 = 连续刷新；首次按下先立即刷一次（自动重复交给定时器）
        if (!e->isAutoRepeat())
            refreshSnapshot();
        beginContinuousRefresh();
        return;

    case Qt::Key_Comma:
        loadHistory(m_historyOffset + 1);
        return;

    case Qt::Key_Period:
        loadHistory(m_historyOffset - 1);
        return;

    case Qt::Key_Tab:
        // 绘制中 Tab 切换直线/箭头
        if (m_state == State::Drawing) {
            auto *line = dynamic_cast<LineShape *>(m_inProgress.get());
            if (line) {
                line->setKind(line->kind() == LineShape::Kind::Arrow ? LineShape::Kind::Line
                                                                     : LineShape::Kind::Arrow);
                update();
                return;
            }
        }
        // 空闲时 Tab 循环：自由选区 → 窗口检测 → 元素检测
        cycleDetectionMode();
        return;

    case Qt::Key_Space:
        togglePaintBar();
        return;

    case Qt::Key_QuoteLeft:
    case Qt::Key_AsciiTilde:
        // ` / ~ 的语义是"截图时显示鼠标"：只切换本次会话的显示状态，
        // 不写回 snip/capture_cursor。写回会让"首选项里关掉的光标截图"
        // 按一次 ~ 就永久变成开启，用户下次完全无法理解。
        m_showCursor = !m_showCursor;
        update();
        return;

    case Qt::Key_Shift:
        m_hexFormat = !m_hexFormat;
        update();
        return;

    case Qt::Key_Alt:
        m_magnifierOn = true;
        update();
        return;

    default:
        break;
    }

    // WASD 移动光标 1px
    if (!ctrl && !shift && (e->key() == Qt::Key_W || e->key() == Qt::Key_A
                            || e->key() == Qt::Key_S || e->key() == Qt::Key_D)) {
        QPoint pos = QCursor::pos();
        if (e->key() == Qt::Key_W) pos.ry() -= 1;
        if (e->key() == Qt::Key_S) pos.ry() += 1;
        if (e->key() == Qt::Key_A) pos.rx() -= 1;
        if (e->key() == Qt::Key_D) pos.rx() += 1;
        QCursor::setPos(pos);
        m_mouseLocal = toLocal(pos);
        updateMagnifier();
        update();
        return;
    }

    // 方向键：移动/扩大/缩小选区
    if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right
        || e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) {
        if (m_state != State::Adjusting)
            return;

        QRect r = localSelection();
        const int dx = (e->key() == Qt::Key_Left) ? -1 : (e->key() == Qt::Key_Right ? 1 : 0);
        const int dy = (e->key() == Qt::Key_Up) ? -1 : (e->key() == Qt::Key_Down ? 1 : 0);

        if (ctrl)
            r = r.adjusted(dx < 0 ? dx : 0, dy < 0 ? dy : 0,
                           dx > 0 ? dx : 0, dy > 0 ? dy : 0);
        else if (shift)
            r = r.adjusted(dx > 0 ? dx : 0, dy > 0 ? dy : 0,
                           dx < 0 ? dx : 0, dy < 0 ? dy : 0);
        else
            r.translate(dx, dy);

        r = r.intersected(rect());
        if (r.width() >= kMinSelection && r.height() >= kMinSelection) {
            m_selection = r;
            update();
        }
        return;
    }

    QWidget::keyPressEvent(e);
}

void SnipOverlay::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_F5 && !e->isAutoRepeat()) {
        endContinuousRefresh();
        return;
    }
    QWidget::keyReleaseEvent(e);
}

// ---------------------------------------------------------------- 功能

void SnipOverlay::updateHoverWindow()
{
    // 只有 "还没选区"（Idle）时才做窗口/元素识别。
    // 选区一旦确定（Adjusting）就停掉：用户已经在调选区了，
    // 再叠一个高亮块只会干扰视线，也容易误以为选区被改。
    if (m_state != State::Idle) {
        if (!m_hoverWindow.isEmpty()) {
            m_hoverWindow = QRect();
            update();
        }
        return;
    }

    if (m_detectMode == DetectMode::Free) {
        if (!m_hoverWindow.isEmpty()) {
            m_hoverWindow = QRect();
            update();
        }
        return;
    }

    requestElementDetection();
}

// 元素检测。实时策略：
//   1. 鼠标移动即记录最新位置（m_pendingCursor）
//   2. 同一时刻只允许一个请求在飞（m_elementPending）
//   3. 结果回来时若鼠标已移开，立刻用最新位置补发（见 detected 回调）
//   4. 检测本身在工作线程执行（MSAA 优先，UIA 兜底），不阻塞 UI
void SnipOverlay::requestElementDetection()
{
    const QPoint cursor = QCursor::pos();

    // 窗口层永远先同步显示：UIA 请求要 100~300ms 才回来，
    // 这段时间里用户必须已经看到"鼠标下是哪个窗口"，否则会以为检测失灵。
    const QRect win = WindowDetector::bestWindowRectAt(cursor);
    const QRect localWin = win.isEmpty() ? QRect()
                                         : QRect(win.topLeft() - virtualTopLeft(), win.size())
                                               .intersected(rect());
    if (!m_elementPending && m_elementHierarchy.isEmpty() && localWin != m_hoverWindow) {
        m_hoverWindow = localWin;
        update();
    }

    if (ElementDetector::instance()->isDisabled()) {
        if (m_detectMode == DetectMode::Element)
            m_detectMode = DetectMode::Window;
        if (m_hoverThrottle.isValid() && m_hoverThrottle.elapsed() < 120)
            return;
        m_hoverThrottle.restart();
        if (localWin != m_hoverWindow) {
            m_hoverWindow = localWin;
            update();
        }
        return;
    }
    // 实时策略：鼠标一动就记下最新位置，但**同一时刻只允许一个请求在飞**。
    // 结果回来后立刻用最新位置再发一次（见 onElementDetected 末尾）。
    // 这样既不会把 UIA/MSAA 打爆，视觉上又是"跟着鼠标走"的。
    //
    // 不在这里做"等鼠标停下再识别"——那会明显滞后，用户要的是实时。
    m_pendingCursor = cursor;
    if (m_elementPending)
        return;

    // 最小间隔只用于挡住同一帧内的重复调用（不是人为延迟）
    if (m_elementThrottle.isValid() && m_elementThrottle.elapsed() < 16)
        return;

    m_elementThrottle.restart();
    m_elementRequestPos = cursor;
    m_elementProcess = WindowDetector::processNameAt(cursor);
    m_elementPending = true;

    // 必须把遮罩下面那个真实窗口的 HWND 传进去：
    // UIA 的 ElementFromPoint 会命中我们自己的全屏遮罩，不传窗口句柄
    // 就永远只能识别"整屏"，表现为元素检测完全失灵。
    const WindowInfo target = WindowDetector::windowFromPoint(cursor);
    m_elementRequestId =
        ElementDetector::instance()->request(cursor, m_elementProcess, target.hwnd);
    if (m_elementRequestId == 0) {
        m_elementPending = false;
        m_detectMode = DetectMode::Window;
        m_hoverWindow = localWin;
        update();
    }
}

// 把当前层级对应的元素矩形同步到 m_hoverWindow（画面上就是虚线轮廓）
void SnipOverlay::applyElementLevel(int level)
{
    if (m_elementHierarchy.isEmpty()) {
        m_hoverWindow = QRect();
        update();
        return;
    }

    m_elementLevel = qBound(0, level, m_elementHierarchy.size() - 1);

    // 整屏大小的元素（桌面/根节点）对用户没有意义：高亮它等于整屏不发暗，
    // 表现为"选区外也是亮的"，还会在整屏画一圈虚线。
    // 这里一律把它当成"没检测到"，绝不显示。
    const auto localOf = [this](const QRectF &r) {
        return QRect(r.topLeft().toPoint() - virtualTopLeft(), r.size().toSize())
            .intersected(rect());
    };
    const auto isFullScreen = [this](const QRect &r) {
        return r.width() >= width() - 2 && r.height() >= height() - 2;
    };

    for (int level2 = m_elementLevel; level2 < m_elementHierarchy.size(); ++level2) {
        QRect candidate = localOf(m_elementHierarchy.at(level2).logicalRect);
        if (candidate.isEmpty() || isFullScreen(candidate))
            continue;
        // 链顶端 = 整窗层：换成 DWM 可见边框，做到和 Snipaste 一样严丝合缝。
        // 只在基本覆盖整个窗口时才换，避免链被截断时把大面板误当成整窗。
        if (level2 == m_elementHierarchy.size() - 1) {
            const QRect win = WindowDetector::bestWindowRectAt(QCursor::pos());
            const QRect localWin = win.isEmpty() ? QRect() : localOf(QRectF(win));
            if (!localWin.isEmpty() && !isFullScreen(localWin)
                && coversMostOf(QRectF(candidate), QRectF(localWin)))
                candidate = localWin;
        }
        m_elementLevel = level2;
        m_hoverWindow = candidate;
        update();
        return;
    }

    // 整条 UIA 链都是整屏：退回顶层窗口矩形；窗口也是整屏就彻底不显示
    const QRect win = WindowDetector::bestWindowRectAt(QCursor::pos());
    const QRect localWin = win.isEmpty() ? QRect() : localOf(QRectF(win));
    m_hoverWindow = (localWin.isEmpty() || isFullScreen(localWin)) ? QRect() : localWin;
    update();
}

void SnipOverlay::changeElementLevel(int delta)
{
    if (m_elementHierarchy.isEmpty())
        return;
    applyElementLevel(m_elementLevel + delta);
}

void SnipOverlay::cycleDetectionMode()
{
    // 顺序：元素 → 窗口 → 自由 → 元素（默认停在元素）
    switch (m_detectMode) {
    case DetectMode::Element:
        m_detectMode = DetectMode::Window;
        break;
    case DetectMode::Window:
        m_detectMode = DetectMode::Free;
        break;
    case DetectMode::Free:
        m_detectMode = DetectMode::Element;
        break;
    }

    clearDetection();
    m_windowDetectionOn = (m_detectMode != DetectMode::Free);

    if (m_detectMode != DetectMode::Free)
        requestElementDetection();
    else
        updateHoverWindow();

    update();
}

void SnipOverlay::clearDetection()
{
    m_hoverWindow = QRect();
    m_elementHierarchy.clear();
    m_elementLevel = 0;
    m_elementRequestId = 0;
    m_elementPending = false;
    m_elementRequestPos = QPoint(-1, -1);
}

void SnipOverlay::updateMagnifier()
{
    if (m_state == State::Dragging || m_state == State::Resizing || m_state == State::Moving) {
        m_magnifierOn = false;
        return;
    }

    // 正在画标注/有标注被选中时不显示放大镜：它会盖住落笔点。
    // 其余情况（含选区已确定）保持显示 —— 用户正是要对着选区里的像素取色。
    if (m_currentTool != Tool::None || !m_selectedShapes.empty()) {
        m_magnifierOn = false;
        return;
    }

    // 选区确定后也保留放大镜：用户正是要对着选区里的像素取色。
    // 取样点仍然是当前鼠标位置（不固定），只是不再画红色准心。
    if (m_mouseMoved || m_state == State::Adjusting)
        m_magnifierOn = true;
}

void SnipOverlay::pickColor()
{
    const QImage &img = m_snapshotImage;
    const qreal dpr = m_snapshot.devicePixelRatio;
    const int px = qRound(m_mouseLocal.x() * dpr);
    const int py = qRound(m_mouseLocal.y() * dpr);
    if (px < 0 || py < 0 || px >= img.width() || py >= img.height())
        return;

    m_pickedColor = img.pixelColor(px, py);
    QApplication::clipboard()->setText(colorString());
    update();
}

QString SnipOverlay::colorString() const
{
    if (!m_pickedColor.isValid())
        return {};
    if (m_hexFormat) {
        const bool upper = ConfigManager::instance()->value(QStringLiteral("interface/uppercase_hex")).toBool();
        QString s = m_pickedColor.name(QColor::HexRgb);
        return upper ? s.toUpper() : s;
    }
    return QStringLiteral("RGB(%1, %2, %3)")
        .arg(m_pickedColor.red())
        .arg(m_pickedColor.green())
        .arg(m_pickedColor.blue());
}

void SnipOverlay::resetToFullScreen()
{
    // 第一次：当前屏；第二次：全部屏
    if (localSelection() == rect()) {
        m_selection = rect();
    } else {
        const QPoint cursorLocal = toLocal(QCursor::pos());
        for (QScreen *screen : QGuiApplication::screens()) {
            const QRect localGeom(screen->geometry().topLeft() - virtualTopLeft(), screen->geometry().size());
            if (localGeom.contains(cursorLocal)) {
                m_selection = localGeom.intersected(rect());
                break;
            }
        }
        if (m_selection.isEmpty())
            m_selection = rect();
    }
    m_state = State::Adjusting;
    showPaintBar();
    update();
    if (m_paintBarVisible)
        syncPaintBarPosition();
}

void SnipOverlay::resetToLastSnip()
{
    const QRect last = SnipHistory::instance()->lastRect();
    if (last.isEmpty())
        return;
    m_selection = QRect(last.topLeft() - virtualTopLeft(), last.size()).intersected(rect());
    if (m_selection.width() >= kMinSelection && m_selection.height() >= kMinSelection) {
        m_state = State::Adjusting;
        showPaintBar();
    }
    update();
}

void SnipOverlay::loadHistory(int offset)
{
    SnipHistory *hist = SnipHistory::instance();
    const int maxOffset = hist->count() - 1;
    if (maxOffset < 0)
        return;

    offset = qBound(0, offset, maxOffset);
    if (offset == m_historyOffset && !m_selection.isEmpty())
        return;

    const SnipRecord *rec = hist->record(offset);
    if (!rec)
        return;

    m_historyOffset = offset;
    m_selection = QRect(rec->logicalRect.topLeft() - virtualTopLeft(), rec->logicalRect.size())
                      .intersected(rect());
    if (m_selection.width() >= kMinSelection && m_selection.height() >= kMinSelection) {
        m_state = State::Adjusting;
        showPaintBar();
    }
    update();
}

QImage SnipOverlay::composeResultImage() const
{
    // 长截图有拼接内容：保存/复制输出的是长图（与选区/标注无关）
    if (m_longShotState != LongShotState::Off && !m_stitcher.isEmpty())
        return m_stitcher.canvas();

    const QRect sel = localSelection();
    if (sel.isEmpty() || m_snapshotImage.isNull())
        return {};

    const qreal dpr = m_snapshot.devicePixelRatio;
    const QRect phys(qRound(sel.x() * dpr), qRound(sel.y() * dpr),
                     qRound(sel.width() * dpr), qRound(sel.height() * dpr));
    QImage result = m_snapshotImage.copy(phys.intersected(m_snapshotImage.rect()));
    result.setDevicePixelRatio(1.0);

    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setClipRect(result.rect());
    p.scale(dpr, dpr);
    p.translate(-sel.topLeft());

    if (m_showCursor) {
        if (!m_liveCursorPixmap.isNull()) {
            // 导出时同样用抓屏时刻的位置（截图里应该钉在那个点上）
            const QPoint localCursor = m_snapshot.cursorPosition - virtualTopLeft();
            const qreal cursorDpr = m_liveCursorDpr > 0.0 ? m_liveCursorDpr : dpr;
            const QSizeF cursorSize(m_liveCursorPixmap.width() / cursorDpr,
                                    m_liveCursorPixmap.height() / cursorDpr);
            p.drawPixmap(QRectF(QPointF(localCursor - m_liveCursorHotSpot), cursorSize),
                         m_liveCursorPixmap, m_liveCursorPixmap.rect());
        }
    }

    if (!m_model->isEmpty())
        m_model->render(&p, QRectF(sel));
    p.end();

    return result;
}

bool SnipOverlay::finishSnip(SnipOutput output)
{
    // 输入框还开着：先提交内容，避免文字丢失
    if (m_textBox) {
        TextInputBox *box = m_textBox;
        m_textBox = nullptr;          // 提前清空，避免 commit 回调里重入
        box->commit();
    }

    // 正在绘制时先结束当前形状
    if (m_state == State::Drawing)
        finishShape();
    else if (m_state == State::PolylineDrawing)
        finishPolyline();

    const QRect sel = localSelection();
    if (sel.isEmpty())
        return false;

    SnipResult result;
    result.image = composeResultImage();
    result.logicalRect = globalSelection();
    result.valid = !result.image.isNull();

    if (!result.valid) {
        abortSnip();
        return true;
    }

    switch (output) {
    case SnipOutput::Copy:
        copyImageToClipboard(postProcessForOutput(result.image));
        break;

    case SnipOutput::Save:
    case SnipOutput::FileDialog: {
        const QString path = QFileDialog::getSaveFileName(
            nullptr, QStringLiteral("保存截图"),
            QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath(defaultFileName()),
            QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg);;BMP 图片 (*.bmp);;WebP 图片 (*.webp)"));
        if (path.isEmpty())
            return false;
        if (!ImageOutput::save(postProcessForOutput(result.image, path), path)) {
            qWarning() << "SnipOverlay: failed to save image to" << path;
            return false;
        }
        OperationToast::imageSaved();
        break;
    }

    case SnipOutput::QuickSave: {
        const QString folder = ConfigManager::instance()->quickSaveFolder();
        QDir().mkpath(folder);
        const QString path = QDir(folder).filePath(defaultFileName());
        if (!ImageOutput::save(postProcessForOutput(result.image, path), path)) {
            qWarning() << "SnipOverlay: failed to quick-save to" << path;
        } else {
            OperationToast::imageSaved();
        }
        break;
    }

    case SnipOutput::Pin:
        // 真正的贴图由 AppController 在 onSnipFinished 里完成，
        // 这里不要复制到剪贴板（否则会污染剪贴板，且贴图内容会重复）
        break;
    }

    SnipHistory::instance()->add(result.image, result.logicalRect);
    ToolStyleStore::instance()->save();

    emit snipFinished(result, output);

    // 立刻丢掉全屏快照（物理像素，多屏时可达几十 MB）。
    // 窗口还没销毁，但会话已经结束，不会再绘制；留着只是白占内存。
    releaseSessionImages();
    close();
    return true;
}

void SnipOverlay::releaseSessionImages()
{
    m_snapshot.pixmap = QPixmap();
    m_snapshot.cursorPixmap = QPixmap();
    m_snapshotImage = QImage();
    m_snapshotLayer = QPixmap();
    PixelShape::clearSourceImage();
    invalidateBackgroundCache();
}

void SnipOverlay::abortSnip()
{
    // 记录所有截图：Esc 中止时把当前选区内容存进历史，可用 , . 找回
    recordAbortedSnip();

    if (m_textBox) {
        m_textBox->cancel();   // 中止截图时丢弃未提交的文字
        m_textBox = nullptr;
    }
    if (m_sizeBox) {
        m_sizeBox->cancel();
        m_sizeBox = nullptr;
    }
    endContinuousRefresh();
    if (m_paintBar)
        m_paintBar->hide();
    releaseSessionImages();
    emit snipAborted();
    close();
}

// ---------------------------------------------------------------- 绘制

void SnipOverlay::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // 长截图态：外圈和普通截图一样（冻结快照 + 半透明黑遮罩）；
    // 选区已用 setMask 从窗口形状里挖掉（真"天窗"），像素到不了选区，
    // 实时页面直接透出来给用户滚动/观看
    if (m_longShotState != LongShotState::Off) {
        rebuildBackgroundCache();
        if (!m_snapshotLayer.isNull())
            p.drawPixmap(0, 0, m_snapshotLayer);
        drawMask(p);
        drawLongShotRing(p);
        return;
    }

    rebuildBackgroundCache();
    rebuildMaskedCache();
    if (!m_maskedLayer.isNull()) {
        p.drawPixmap(0, 0, m_maskedLayer);
    } else {
        if (!m_snapshotLayer.isNull())
            p.drawPixmap(0, 0, m_snapshotLayer);
        drawMask(p);
    }

    drawCapturedCursor(p);
    drawWindowHighlight(p);      // 悬停的窗口/元素块要在遮罩之上，否则被压暗看不清
    drawAnnotations(p);          // 标注画在底图之上、遮罩之下
    drawInProgressShape(p);
    // 二维码识别结果高亮（画在标注之上、锚点之下）
    if (!m_barcodeRects.isEmpty()) {
        p.save();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Theme::kAccent, 2));
        for (const QRectF &r : m_barcodeRects)
            p.drawRect(r);
        p.restore();
    }

    drawSelectionHandles(p);     // 选中框 + 锚点
    drawLassoPreview(p);
    drawAuxiliaryLines(p);
    drawSelectionFrame(p);
    drawAnchors(p);
    drawSizeLabel(p);
    drawMagnifier(p);
    drawSelectionInfo(p);
    drawHint(p);
    drawShortcutTips(p);
    drawCenterNotice(p);
}

void SnipOverlay::rebuildBackgroundCache()
{
    const qreal dpr = devicePixelRatioF() > 0.0 ? devicePixelRatioF() : 1.0;
    if (!m_snapshotLayer.isNull() && m_snapshotLayerSize == size()
        && qFuzzyCompare(m_snapshotLayerDpr, dpr)) {
        return;
    }

    // 快照本身尺寸就与窗口一致时（常见情况：单屏 100% 缩放），
    // 直接复用那份 pixmap，省掉一整份全屏副本（4K 下约 30MB）。
    const QPixmap &shot = m_snapshot.pixmap;
    if (!shot.isNull() && shot.size() == size() * dpr) {
        m_snapshotLayer = shot;
        m_snapshotLayerSize = size();
        m_snapshotLayerDpr = dpr;
        return;
    }

    // 只在需要缩放时（多屏不同 dpr / 混合 DPI）才重建一份。
    // 之后每帧 drawPixmap 是 1:1 拷贝，不再重采样。
    QPixmap layer(size() * dpr);
    layer.setDevicePixelRatio(dpr);
    layer.fill(Qt::black);
    {
        QPainter cp(&layer);
        cp.setRenderHint(QPainter::SmoothPixmapTransform, false);
        if (!m_snapshot.pixmap.isNull())
            cp.drawPixmap(rect(), m_snapshot.pixmap);
    }

    m_snapshotLayer = layer;
    m_snapshotLayerSize = size();
    m_snapshotLayerDpr = dpr;
}

// 快照 + 遮罩的合成缓存：悬停/取色/元素识别阶段每帧都要重绘整屏，
// 没缓存时每帧都做一遍整屏 alpha 混合，大屏上是肉眼可见的卡顿。
// 只在选区形状变化时重新合成：拖选区时与原来同价，静止时几乎免费。
void SnipOverlay::rebuildMaskedCache()
{
    const QRect sel = localSelection();
    if (!m_maskedLayer.isNull() && m_maskedLayerSel == sel
        && m_maskedLayer.size() == m_snapshotLayer.size())
        return;

    if (m_snapshotLayer.isNull()) {
        m_maskedLayer = QPixmap();
        return;
    }

    m_maskedLayer = m_snapshotLayer.copy();
    QPainter p(&m_maskedLayer);
    drawMask(p);
    m_maskedLayerSel = sel;
}

void SnipOverlay::drawMask(QPainter &p)
{
    const QRect sel = localSelection();
    if (sel.isEmpty()) {
        p.fillRect(rect(), Theme::kMask);
        return;
    }

    const QColor mask = Theme::kMask;
    p.fillRect(QRect(0, 0, width(), sel.top()), mask);
    p.fillRect(QRect(0, sel.bottom() + 1, width(), height() - sel.bottom() - 1), mask);
    p.fillRect(QRect(0, sel.top(), sel.left(), sel.height()), mask);
    p.fillRect(QRect(sel.right() + 1, sel.top(),
                     width() - sel.right() - 1, sel.height()), mask);
}

void SnipOverlay::ensureCursorPixmap()
{
    if (!m_liveCursorPixmap.isNull())
        return;

    // 优先用抓屏时拾取的真实光标（形状、大小、热点都准确）
    if (!m_snapshot.cursorPixmap.isNull()) {
        m_liveCursorPixmap = m_snapshot.cursorPixmap;
        m_liveCursorHotSpot = m_snapshot.cursorHotSpot;
        m_liveCursorDpr = m_snapshot.cursorDpr > 0.0 ? m_snapshot.cursorDpr
                                                     : m_snapshot.devicePixelRatio;
        return;
    }

    // 抓屏时没拾取到（GetCursorInfo 失败）时用一个标准箭头兜底，
    // 保证 `~` 一定有可见反馈，而不是按键毫无反应。
    const QCursor arrow(Qt::ArrowCursor);
    m_liveCursorPixmap = arrow.pixmap();
    m_liveCursorHotSpot = arrow.hotSpot();
    m_liveCursorDpr = devicePixelRatioF() > 0.0 ? devicePixelRatioF() : 1.0;
}

void SnipOverlay::drawCapturedCursor(QPainter &p)
{
    if (!m_showCursor)
        return;

    ensureCursorPixmap();
    if (m_liveCursorPixmap.isNull())
        return;

    // 位置固定在**抓屏那一刻**的鼠标位置。
    // 这是"把截图时的光标画进截图里"的语义（和 Snipaste 一致），
    // 不是跟随当前鼠标——那会让光标跟着乱跑，根本没法判断它会被截到哪里。
    const QPoint localCursor = m_snapshot.cursorPosition - virtualTopLeft();
    const QPoint hotSpot = m_liveCursorHotSpot;
    const qreal dpr = m_liveCursorDpr > 0.0 ? m_liveCursorDpr : 1.0;
    const QSizeF cursorSize(m_liveCursorPixmap.width() / dpr,
                            m_liveCursorPixmap.height() / dpr);

    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawPixmap(QRectF(QPointF(localCursor - hotSpot), cursorSize),
                 m_liveCursorPixmap, m_liveCursorPixmap.rect());
    p.restore();
}

void SnipOverlay::drawLassoPreview(QPainter &p)
{
    if (m_state != State::LassoSelecting)
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    QPen dash(Theme::kAccent, 1, Qt::DashLine);
    dash.setDashPattern({4, 3});
    p.setPen(dash);
    p.setBrush(QColor(0x3D, 0x8B, 0xFD, 40));

    if (m_rectSelectActive) {
        p.drawRect(m_rectSelectRect);
    } else if (m_lassoPoints.size() >= 2) {
        // 自由曲线：填充半透明多边形 + 描边，让用户看到会选中哪一块
        QPolygonF poly;
        for (const QPointF &pt : m_lassoPoints)
            poly << pt;
        p.drawPolygon(poly);
    }

    p.restore();
}

void SnipOverlay::drawWindowHighlight(QPainter &p)
{
    if (m_hoverWindow.isEmpty())
        return;

    // 鼠标已经不在检测区域内（例如停在工具栏/选区外沿）就别再闪一个框
    if (m_mouseInside && !m_hoverWindow.contains(m_mouseLocal)
        && m_state == State::Adjusting) {
        return;
    }

    // 选区已确定：只画实线轮廓，和手动选区完全一致（Snipaste 也是实线）。
    // 不再叠主色填充：那会让整片发蓝，用户会看成"截图直接蓝屏"。
    if (m_state == State::Adjusting) {
        p.save();
        p.setPen(QPen(Theme::kAccent, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(m_hoverWindow.adjusted(0, 0, -1, -1));
        p.restore();
        return;
    }

    // 未选区（Idle）：把检测到的窗口/元素从黑色遮罩里"挖"出来，
    // 和 Snipaste 一致 —— 其余区域保持暗，只有这一块是原始画面 + 1px 实线边框。
    p.save();
    p.setClipRect(m_hoverWindow);
    const qreal dpr = m_snapshot.devicePixelRatio;
    const QRectF source(m_hoverWindow.x() * dpr, m_hoverWindow.y() * dpr,
                        m_hoverWindow.width() * dpr, m_hoverWindow.height() * dpr);
    p.drawImage(QRectF(m_hoverWindow), m_snapshotImage, source);

    p.setClipping(false);
    p.setPen(QPen(Theme::kAccent, 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(m_hoverWindow.adjusted(0, 0, -1, -1));

    // 元素块额外画 8 个锚点，整窗只留方框。
    // 但**小元素**不画：几十像素的图标/按钮，8 个锚点会糊成一团、把内容全盖住，
    // 判断标准用短边 >= kAnchorMinSide，而不是面积（长条按钮短边很小但仍需好看）。
    const bool showHoverAnchors = !m_elementHierarchy.isEmpty()
                                  && qMin(m_hoverWindow.width(),
                                          m_hoverWindow.height()) >= kAnchorMinSide;
    if (showHoverAnchors) {
        const QRect r = m_hoverWindow;
        const QPoint pts[8] = {
            r.topLeft(), QPoint(r.center().x(), r.top()), r.topRight(),
            QPoint(r.right(), r.center().y()), r.bottomRight(),
            QPoint(r.center().x(), r.bottom()), r.bottomLeft(),
            QPoint(r.left(), r.center().y())
        };
        p.setPen(QPen(Qt::white, 1));
        p.setBrush(Theme::kAccent);
        for (const QPoint &pt2 : pts) {
            const QRect box(pt2.x() - kAnchorSize / 2, pt2.y() - kAnchorSize / 2,
                            kAnchorSize, kAnchorSize);
            p.drawRect(box);
        }
    }
    p.restore();
}

void SnipOverlay::drawAuxiliaryLines(QPainter &p)
{
    if (!ConfigManager::instance()->value(QStringLiteral("snip/show_auxiliary_lines")).toBool())
        return;
    if (!m_mouseInside || !localSelection().isEmpty())
        return;

    p.save();
    p.setPen(QPen(Theme::kGuideLine, 1));
    p.drawLine(0, m_mouseLocal.y(), width(), m_mouseLocal.y());
    p.drawLine(m_mouseLocal.x(), 0, m_mouseLocal.x(), height());
    p.restore();
}

void SnipOverlay::drawSelectionFrame(QPainter &p)
{
    const QRect sel = localSelection();
    if (sel.isEmpty())
        return;

    // 选区边框：单层 2px 蓝色实线（Snipaste 风格，不要虚线；1px 在高分屏上太细）
    p.save();
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Theme::kAccent, 2));
    p.drawRect(sel.adjusted(0, 0, -1, -1));
    p.restore();

    drawCornerPreview(p);
    drawShadowPreview(p);
}

// 圆角开启时，在选区四角画圆角虚线预览
void SnipOverlay::drawCornerPreview(QPainter &p)
{
    if (!m_roundedCorners)
        return;

    const QRect sel = localSelection();
    if (sel.isEmpty())
        return;

    const int r = qMin(m_cornerRadius, qMin(sel.width(), sel.height()) / 2);
    if (r <= 0)
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    // 圆角预览也用实线，和选区边框统一（虚线会和"检测到的那样"混淆）
    p.setPen(QPen(Theme::kAccent, 1));
    p.setBrush(Qt::NoBrush);

    QPainterPath path;
    path.addRoundedRect(QRectF(sel.adjusted(0, 0, -1, -1)), r, r);
    p.drawPath(path);
    p.restore();
}

// 阴影开启时，在选区下方画一圈柔和的投影提示（导出后效果一致）
void SnipOverlay::drawShadowPreview(QPainter &p)
{
    if (!m_shadowPreview)
        return;

    const QRect sel = localSelection();
    if (sel.isEmpty())
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    const int layers = 8;
    const int spread = 10;
    for (int i = layers; i >= 1; --i) {
        const int alpha = 6 + (layers - i) * 2;
        const int grow = qRound(spread * qreal(i) / layers);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, alpha));
        p.drawRoundedRect(QRectF(sel.adjusted(-grow, -grow + 6, grow, grow + 6)),
                          m_roundedCorners ? m_cornerRadius : 0,
                          m_roundedCorners ? m_cornerRadius : 0);
    }
    p.restore();
}

void SnipOverlay::drawSizeLabel(QPainter &p)
{
    // 悬停检测到的窗口：还没确定选区，但用户需要先看到"这个窗口多大"，
    // 否则只能盲拖。尺寸标签固定在识别框左上角（不跟随光标）。
    // 选区确定后把鼠标移回窗口上时同样显示这个尺寸（方便对照选区大小）。
    const bool hoveringDetected = !m_hoverWindow.isEmpty()
                                  && m_hoverWindow.contains(m_mouseLocal);
    if (hoveringDetected) {
        const bool dip = ConfigManager::instance()
                             ->value(QStringLiteral("interface/size_unit_dip"))
                             .toBool();
        const qreal dpr = m_snapshot.devicePixelRatio;
        const QSize shown = dip ? m_hoverWindow.size()
                                : QSize(qRound(m_hoverWindow.width() * dpr),
                                        qRound(m_hoverWindow.height() * dpr));

        QFont f = Theme::appFont();
        f.setPointSize(9);
        const QFontMetrics fm(f);
        p.save();
        p.setFont(f);

        const QString sizeText = QStringLiteral("%1 × %2").arg(shown.width()).arg(shown.height());
        const int pad = 8;
        const QRect sizeRect(0, 0, fm.horizontalAdvance(sizeText) + pad * 2,
                             fm.height() + pad);

        // 固定在识别框左上角：沿框上沿贴左缘，贴屏幕顶部时翻进框内
        int x = m_hoverWindow.left();
        int y = m_hoverWindow.top() - sizeRect.height() - 6;
        if (y < 2)
            y = m_hoverWindow.top() + 4;
        if (x + sizeRect.width() > width() - 2)
            x = qMax(0, width() - sizeRect.width() - 2);

        const QRect sBox(x, y, sizeRect.width(), sizeRect.height());

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 200));
        p.drawRoundedRect(sBox, 3, 3);
        p.setPen(QColor(0xE8, 0xE8, 0xE8));
        p.drawText(sBox, Qt::AlignCenter, sizeText);
        p.restore();
        return;
    }

    const QRect sel = localSelection();
    if (sel.isEmpty())
        return;

    // 单位可切换 px / dip
    const bool useDip = ConfigManager::instance()
                            ->value(QStringLiteral("interface/size_unit_dip"))
                            .toBool();
    const qreal dpr = m_snapshot.devicePixelRatio;
    const QSize shown = useDip ? sel.size()
                               : QSize(qRound(sel.width() * dpr), qRound(sel.height() * dpr));
    const QString text = QStringLiteral("%1 × %2").arg(shown.width()).arg(shown.height());

    const QRect box = sizeLabelRect();
    if (box.isEmpty())
        return;

    p.save();
    QFont f = Theme::appFont();
    f.setPointSize(9);
    p.setFont(f);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 190));
    p.drawRoundedRect(box, 3, 3);
    p.setPen(QColor(0xE8, 0xE8, 0xE8));
    p.drawText(box, Qt::AlignCenter, text);

    // 圆角 / 阴影开启时在标签右侧给出状态
    QString badge;
    if (m_roundedCorners)
        badge += QStringLiteral("圆角 %1").arg(m_cornerRadius);
    if (m_shadowPreview) {
        if (!badge.isEmpty())
            badge += QStringLiteral("  ");
        badge += QStringLiteral("阴影");
    }
    if (!badge.isEmpty()) {
        const QFontMetrics fm(f);
        const QRect badgeBox(box.right() + 4, box.top(), fm.horizontalAdvance(badge) + 12,
                             box.height());
        if (badgeBox.right() < width()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x3D, 0x8B, 0xFD, 190));
            p.drawRoundedRect(badgeBox, 3, 3);
            p.setPen(Qt::white);
            p.drawText(badgeBox, Qt::AlignCenter, badge);
        }
    }
    p.restore();
}

void SnipOverlay::drawSelectionInfo(QPainter &p)
{
    Q_UNUSED(p);

    // 面板改为独立置顶窗口绘制（见 InfoPanel）：
    // PaintBar 是截图层子控件，画在父窗口内容之上，信息面板靠近工具栏时会被盖住。
    const QRect sel = localSelection();
    // 选择绘制工具时不显示（鼠标用途是落笔，不是取色）
    const bool toolActive = (m_currentTool != Tool::None && m_paintBarVisible);
    // 已经框好选区时：只在选区内部显示；
    // 还没框选区时（Idle/Dragging）：全屏都可以取色，鼠标在哪就显示哪
    const bool insideSelection = !sel.isEmpty() && sel.contains(m_mouseLocal);
    const bool pickingWholeScreen = sel.isEmpty()
                                    && (m_state == State::Idle || m_state == State::Dragging);
    const bool shouldShow = !toolActive && m_mouseMoved && !m_sizeBox
                            && m_longShotState == LongShotState::Off
                            && (insideSelection || pickingWholeScreen);
    if (!shouldShow) {
        if (m_infoPanel)
            m_infoPanel->hideInfo();
        return;
    }

    const qreal dpr = m_snapshot.devicePixelRatio;
    const int px = qRound(m_mouseLocal.x() * dpr);
    const int py = qRound(m_mouseLocal.y() * dpr);
    if (px < 0 || py < 0 || px >= m_snapshotImage.width()
        || py >= m_snapshotImage.height()) {
        if (m_infoPanel)
            m_infoPanel->hideInfo();
        return;
    }

    if (!m_infoPanel)
        m_infoPanel = new InfoPanel();

    const QColor sampled = m_snapshotImage.pixelColor(px, py);
    const QPoint globalPos = m_mouseLocal + virtualTopLeft();

    InfoPanel::Data data;
    data.positionText = QStringLiteral("(%1, %2)").arg(globalPos.x()).arg(globalPos.y());
    data.colorText = m_hexFormat ? sampled.name(QColor::HexRgb)
                                 : QStringLiteral("%1, %2, %3")
                                       .arg(sampled.red())
                                       .arg(sampled.green())
                                       .arg(sampled.blue());
    data.swatch = sampled;
    data.copyHint = QStringLiteral("按 C 复制颜色值");
    data.formatHint = QStringLiteral("按 Shift 切换 RGB/HEX");

    // 和放大镜是"一块整体"：放大镜显示时，信息面板贴在它正上方、左右对齐；
    // 放大镜没开时才退化为跟随鼠标
    const QRect magRect = m_magnifierPanel ? m_magnifierPanel->globalRect() : QRect();
    if (!magRect.isEmpty()) {
        m_infoPanel->showAbove(data, magRect);
        return;
    }

    QRect avoid;
    if (m_paintBarVisible && m_paintBar)
        avoid = QRect(m_paintBar->mapToGlobal(QPoint(0, 0)), m_paintBar->size());
    m_infoPanel->showInfo(data, mapToGlobal(m_mouseLocal), avoid);
}

void SnipOverlay::drawAnchors(QPainter &p)
{
    const QRect sel = localSelection();
    if (sel.isEmpty() || !m_showAnchors)
        return;

    // 始终显示 8 个锚点（4 角 + 4 边中点），和 Snipaste 一致。
    // 之前按 anchor_threshold 自动隐藏，导致正常选区看不到锚点、无法拖拽调整。

    const QPoint pts[8] = {
        sel.topLeft(), QPoint(sel.center().x(), sel.top()), sel.topRight(),
        QPoint(sel.right(), sel.center().y()), sel.bottomRight(),
        QPoint(sel.center().x(), sel.bottom()), sel.bottomLeft(),
        QPoint(sel.left(), sel.center().y())
    };

    p.save();
    p.setPen(QPen(Qt::white, 1));
    p.setBrush(Theme::kAccent);
    for (const QPoint &pt : pts) {
        const QRect r(pt.x() - kAnchorSize / 2, pt.y() - kAnchorSize / 2, kAnchorSize, kAnchorSize);
        p.drawRect(r);
    }
    p.restore();
}

QRect SnipOverlay::magnifierRect() const
{
    if (!m_magnifierOn || m_snapshotImage.isNull())
        return {};

    // 位置：默认光标右下，靠近边缘翻转
    int mx = m_mouseLocal.x() + kMagnifierOffset;
    int my = m_mouseLocal.y() + kMagnifierOffset;
    if (mx + kMagnifierSize > width())
        mx = m_mouseLocal.x() - kMagnifierOffset - kMagnifierSize;
    if (my + kMagnifierSize > height())
        my = m_mouseLocal.y() - kMagnifierOffset - kMagnifierSize;
    return QRect(mx, my, kMagnifierSize, kMagnifierSize);
}

void SnipOverlay::drawMagnifier(QPainter &p)
{
    Q_UNUSED(p);

    // 放大镜改为独立置顶小窗（MagnifierPanel）：
    // 原本画在截图层上，会被工具栏子控件盖住。
    // 选择了绘制工具时不显示：此时鼠标是用来落笔的，取色/放大是干扰
    if (!m_magnifierOn || m_snapshotImage.isNull()
        || (m_currentTool != Tool::None && m_paintBarVisible)) {
        if (m_magnifierPanel)
            m_magnifierPanel->hidePanel();
        return;
    }

    const qreal dpr = m_snapshot.devicePixelRatio;
    const int cx = qRound(m_mouseLocal.x() * dpr);
    const int cy = qRound(m_mouseLocal.y() * dpr);

    const int half = kMagnifierSamplePixels / 2;
    QRect srcRect(cx - half, cy - half, kMagnifierSamplePixels, kMagnifierSamplePixels);
    srcRect = srcRect.intersected(m_snapshotImage.rect());
    if (srcRect.isEmpty()) {
        if (m_magnifierPanel)
            m_magnifierPanel->hidePanel();
        return;
    }

    if (!m_magnifierPanel)
        m_magnifierPanel = new MagnifierPanel();

    MagnifierPanel::Data data;
    data.sample = m_snapshotImage.copy(srcRect);
    data.picking = (m_state == State::Idle);
    data.pixelSize = kMagnifierSize / kMagnifierSamplePixels;

    QString info = QStringLiteral("%1, %2").arg(m_mouseLocal.x()).arg(m_mouseLocal.y());
    if (cx >= 0 && cy >= 0 && cx < m_snapshotImage.width() && cy < m_snapshotImage.height()) {
        const QColor c = m_snapshotImage.pixelColor(cx, cy);
        data.pixelColor = c;
        data.pixelValid = true;
        info += QStringLiteral("  ")
                + (m_hexFormat ? c.name(QColor::HexRgb)
                               : QStringLiteral("RGB(%1,%2,%3)")
                                     .arg(c.red()).arg(c.green()).arg(c.blue()));
    }
    data.infoText = info;

    // 避开工具栏；信息面板若已显示也避开它（两者不重叠）
    QRect avoid;
    if (m_paintBarVisible && m_paintBar)
        avoid = QRect(m_paintBar->mapToGlobal(QPoint(0, 0)), m_paintBar->size());

    m_magnifierPanel->showPanel(data, mapToGlobal(m_mouseLocal), avoid);

    // 放大镜先定位，信息面板随后避开它
    if (m_infoPanel && m_infoPanel->isVisible()) {
        const QRect magRect = m_magnifierPanel->globalRect();
        if (!magRect.isEmpty() && avoid.isEmpty())
            avoid = magRect;
        else if (!magRect.isEmpty())
            avoid = avoid.united(magRect);
    }
    m_infoPanelAvoid = avoid;
}

bool SnipOverlay::shortcutTipsVisible() const
{
    // 还在挑选选区（没有选区）、并且没有开始调整时才可能显示。
    // 鼠标经过提示框本身时隐藏（避免挡住框下面的内容，也方便看清），
    // 移开就恢复。
    if (m_state != State::Idle || !localSelection().isEmpty() || m_refreshing)
        return false;
    return !m_hoveringTips;
}

QRect SnipOverlay::shortcutTipsRect() const
{
    struct Metrics
    {
        int width = 0;
        int height = 0;
    };
    static int cachedW = 0;
    static int cachedH = 0;

    if (cachedW == 0) {
        QFont keyFont = Theme::appFont();
        keyFont.setPointSize(9);
        keyFont.setBold(true);
        QFont descFont = Theme::appFont();
        descFont.setPointSize(9);
        const QFontMetrics keyFm(keyFont);
        const QFontMetrics descFm(descFont);

        static const char *const kKeys[] = {
            "W A S D", "Tab", "Alt+Click", "Ctrl + A", "R", ", .", "`",
        };
        static const char *const kDescs[] = {
            "将鼠标指针移动 1 像素",
            "切换 检测窗口 / 检测界面元素",
            "直接选中整个窗口",
            "选区扩展到整个屏幕",
            "使用上一次截图区域",
            "回溯截图历史",
            "显示 / 隐藏鼠标指针",
        };
        int keyColumn = 0;
        int descColumn = 0;
        for (int i = 0; i < 7; ++i) {
            keyColumn = qMax(keyColumn, keyFm.horizontalAdvance(QString::fromUtf8(kKeys[i])));
            descColumn = qMax(descColumn, descFm.horizontalAdvance(QString::fromUtf8(kDescs[i])));
        }
        constexpr int kPad = 14;
        constexpr int kKeyPadH = 10;
        constexpr int kKeyGap = 12;
        constexpr int kRowH = 26;
        cachedW = kPad * 2 + (keyColumn + kKeyPadH * 2) + kKeyGap + descColumn;
        cachedH = kPad * 2 + kRowH * 7;
    }

    return QRect(16, height() - cachedH - 16, cachedW, cachedH);
}

void SnipOverlay::drawShortcutTips(QPainter &p)
{
    if (!shortcutTipsVisible())
        return;

    struct Row
    {
        const char *keys;   // 纯 ASCII
        const char *desc;   // UTF-8 中文，必须用 fromUtf8
    };
    static const Row kRows[] = {
        { "W A S D",   "将鼠标指针移动 1 像素" },
        { "Tab",       "切换 检测窗口 / 检测界面元素" },
        { "Alt+Click", "直接选中整个窗口" },
        { "Ctrl + A",  "选区扩展到整个屏幕" },
        { "R",         "使用上一次截图区域" },
        { ", .",       "回溯截图历史" },
        { "`",         "显示 / 隐藏鼠标指针" },
    };

    QFont keyFont = Theme::appFont();
    keyFont.setPointSize(9);
    keyFont.setBold(true);
    QFont descFont = Theme::appFont();
    descFont.setPointSize(9);
    const QFontMetrics keyFm(keyFont);
    const QFontMetrics descFm(descFont);

    // 键帽列宽取最长的那一项
    int keyColumn = 0;
    int descColumn = 0;
    for (const Row &r : kRows) {
        keyColumn = qMax(keyColumn, keyFm.horizontalAdvance(QString::fromUtf8(r.keys)));
        descColumn = qMax(descColumn, descFm.horizontalAdvance(QString::fromUtf8(r.desc)));
    }

    constexpr int kPad = 14;
    constexpr int kKeyPadH = 10;
    constexpr int kKeyPadV = 4;
    constexpr int kKeyGap = 12;
    constexpr int kRowH = 26;
    const int keyWidth = keyColumn + kKeyPadH * 2;

    // 左下角，避开任务栏和工具栏（几何由 shortcutTipsRect() 统一算，用于命中测试）
    const QRect box = shortcutTipsRect();

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    // 比之前更透明：这是帮助浮层，不该压住底下的画面
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 150));
    p.drawRoundedRect(box, 8, 8);

    int top = box.top() + kPad;
    for (const Row &r : kRows) {
        // 键帽：白底黑字
        const QRect keyRect(box.left() + kPad, top, keyWidth,
                            keyFm.height() + kKeyPadV * 2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xF2, 0xF2, 0xF2));
        p.drawRoundedRect(keyRect, 4, 4);
        p.setFont(keyFont);
        p.setPen(QColor(0x1A, 0x1A, 0x1A));
        p.drawText(keyRect, Qt::AlignCenter, QString::fromUtf8(r.keys));

        // 说明：浅灰白
        p.setFont(descFont);
        p.setPen(QColor(0xE8, 0xE8, 0xE8));
        p.drawText(QRect(keyRect.right() + kKeyGap, top,
                         descColumn, keyRect.height()),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QString::fromUtf8(r.desc));

        top += kRowH;
    }

    p.restore();
}

void SnipOverlay::drawHint(QPainter &p)
{
    // 多选状态下给明确提示，否则用户不知道套索选中之后能干什么。
    // 位置必须在顶部：底部是工具栏的地盘，子控件画在父控件之上，
    // 提示条放底部会被工具栏整个盖住（用户反馈"选中后没提示"就是这个原因）。
    if (!m_selectedShapes.empty() && m_state == State::Adjusting) {
        // 单个时不说"1 个标注"：新建标注自动选中后会一直挂着这条提示，
        // 用"已选中标注"更自然，也顺便告诉用户 Esc 可以退出选中。
        const QString text = m_selectedShapes.size() == 1
                                 ? QStringLiteral("已选中标注   ·   拖动移动   ·   拖动控制点缩放/旋转   ·   Del 删除   ·   Esc 取消选中")
                                 : QStringLiteral("已选中 %1 个标注   ·   拖动移动   ·   Del 删除   ·   Ctrl+Shift+C 复制   ·   Shift+滚轮缩放")
                                       .arg(m_selectedShapes.size());
        p.save();
        QFont f = Theme::appFont();
        f.setPointSize(9);
        p.setFont(f);
        const QFontMetrics fm(f);
        const int w = fm.horizontalAdvance(text) + 24;
        const int h = fm.height() + 12;
        const QRect box(qMax(8, (width() - w) / 2), 12, w, h);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x3D, 0x8B, 0xFD, 235));
        p.drawRoundedRect(box, 4, 4);
        p.setPen(Qt::white);
        p.drawText(box, Qt::AlignCenter, text);
        p.restore();
        return;
    }

    if (!localSelection().isEmpty() || m_state != State::Idle)
        return;

    // 左下角已经显示完整快捷键浮层时，不再重复一条横幅，
    // 否则底部同时出现两段提示，反而更乱。
    if (shortcutTipsVisible())
        return;

    QString text = QStringLiteral("拖动鼠标选择截图区域   |   右键或 Esc 取消   |   Tab 切换检测模式");
    if (m_refreshing)
        text = QStringLiteral("刷新中…");
    else if (m_refreshHintVisible)
        text = QStringLiteral("已刷新截图内容（标注保留）");
    else if (m_currentTool == Tool::Lasso)
        text = QStringLiteral("按住左键拖拽，圈住要选中的标注   ·   Ctrl 追加选择");

    p.save();
    QFont f = Theme::appFont();
    f.setPointSize(10);
    p.setFont(f);
    const QFontMetrics fm(f);
    const int w = fm.horizontalAdvance(text) + 28;
    const int h = fm.height() + 16;

    const QRect box((width() - w) / 2, height() - h - 80, w, h);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 200));
    p.drawRoundedRect(box, 6, 6);
    p.setPen(QColor(0xE8, 0xE8, 0xE8));
    p.drawText(box, Qt::AlignCenter, text);
    p.restore();
}

// 选区右键菜单：比例 / 圆角 / 阴影 / 尺寸 / 发送到
void SnipOverlay::showSnipMenu(const QPoint &globalPos)
{
    AppMenu::Menu menu(this);

    QMenu *aspectMenu = AppMenu::addSubMenu(menu.get(), QStringLiteral("固定比例"));
    struct Preset { const char *key; const char *label; };
    static const Preset kPresets[] = {
        { "free", "自由" }, { "1:1", "1:1" }, { "4:3", "4:3" },
        { "3:2", "3:2" }, { "16:9", "16:9" }, { "9:16", "9:16（竖屏）" }
    };
    for (const Preset &p : kPresets) {
        const QString key = QString::fromLatin1(p.key);
        QAction *act = aspectMenu->addAction(QString::fromUtf8(p.label));
        act->setCheckable(true);
        act->setChecked(m_aspect.preset == key);
        connect(act, &QAction::triggered, this, [this, key]() {
            setAspectPreset(key);
            // 立即把当前选区修正到该比例
            if (m_aspect.enabled && !localSelection().isEmpty()) {
                QRect r = localSelection();
                applyAspectToRect(&r, Anchor::SE);
                r = r.intersected(rect());
                if (r.width() >= kMinSelection && r.height() >= kMinSelection) {
                    m_selection = r;
                    if (m_paintBarVisible)
                        syncPaintBarPosition();
                }
            }
            update();
        });
    }

    QAction *rounded = AppMenu::addIconAction(menu.get(), ElaIconType::CropSimple,
                                              QStringLiteral("圆角截图\tCtrl+R"));
    rounded->setCheckable(true);
    rounded->setChecked(m_roundedCorners);
    connect(rounded, &QAction::triggered, this, &SnipOverlay::toggleRoundedCorners);

    QAction *radius = menu->addAction(QStringLiteral("圆角半径…"));
    radius->setEnabled(m_roundedCorners);
    connect(radius, &QAction::triggered, this, [this]() {
        bool ok = false;
        const int r = QInputDialog::getInt(this, QStringLiteral("圆角半径"),
                                           QStringLiteral("半径（0~50 逻辑像素）："),
                                           m_cornerRadius, 0, 50, 1, &ok);
        if (!ok)
            return;
        m_cornerRadius = r;
        m_roundedCorners = r > 0;
        ConfigManager::instance()->setValue(QStringLiteral("snip/corner_radius"),
                                            m_roundedCorners ? m_cornerRadius : 0);
        update();
    });

    // 选中的矩形标注调圆角：双击已不弹框，统一从这里进
    if (m_selectedShapes.size() == 1
        && m_selectedShapes.front()->type() == Annotation::Type::Rectangle) {
        QAction *shapeRadius = menu->addAction(QStringLiteral("标注圆角半径…"));
        connect(shapeRadius, &QAction::triggered, this, [this]() {
            if (m_selectedShapes.size() != 1)
                return;
            beginRectCornerEdit(m_selectedShapes.front());
        });
    }

    QAction *shadow = menu->addAction(QStringLiteral("导出阴影"));
    shadow->setCheckable(true);
    shadow->setChecked(m_shadowPreview);
    connect(shadow, &QAction::triggered, this, &SnipOverlay::toggleShadowPreview);

    menu->addSeparator();

    QAction *sizeAct = menu->addAction(QStringLiteral("设定尺寸…"));
    connect(sizeAct, &QAction::triggered, this, &SnipOverlay::beginSizeInput);

    QAction *unitAct = menu->addAction(QStringLiteral("尺寸单位：px / dip"));
    connect(unitAct, &QAction::triggered, this, [this]() {
        ConfigManager *cfg = ConfigManager::instance();
        const bool dip = cfg->value(QStringLiteral("interface/size_unit_dip")).toBool();
        cfg->setValue(QStringLiteral("interface/size_unit_dip"), !dip);
        update();
    });

    QAction *sendAct = AppMenu::addIconAction(menu.get(), ElaIconType::PaperPlane,
                                              QStringLiteral("发送到…"));
    connect(sendAct, &QAction::triggered, this, &SnipOverlay::sendToApplication);

    menu->addSeparator();
    QAction *refreshAct = menu->addAction(QStringLiteral("刷新截图\tF5"));
    connect(refreshAct, &QAction::triggered, this, &SnipOverlay::refreshSnapshot);

    menu->exec(globalPos);
}

void SnipOverlay::contextMenuEvent(QContextMenuEvent *e)
{
    if (m_longShotState != LongShotState::Off)
        return;

    // mousePressEvent 已经用这次右键做了别的事（取消选中/取消工具/中止截图）
    if (m_suppressContextMenu) {
        m_suppressContextMenu = false;
        e->ignore();
        return;
    }
    // 只有选区已确定时才弹菜单；其余情况沿用右键逐级退出
    if (localSelection().isEmpty() || m_state != State::Adjusting) {
        e->ignore();
        return;
    }
    showSnipMenu(e->globalPos());
    e->accept();
}

// ---------------------------------------------------------------- P4 检测指示


// ---------------------------------------------------------------- P4 比例 / 圆角 / 阴影

void SnipOverlay::setAspectPreset(const QString &preset)
{
    const QString key = preset.trimmed().isEmpty() ? QStringLiteral("free") : preset;

    struct Preset { const char *name; qreal ratio; };
    static const Preset kPresets[] = {
        { "free", 0.0 },
        { "1:1",  1.0 },
        { "4:3",  4.0 / 3.0 },
        { "3:2",  3.0 / 2.0 },
        { "16:9", 16.0 / 9.0 },
        { "9:16", 9.0 / 16.0 },
    };

    for (const Preset &p : kPresets) {
        if (key == QLatin1String(p.name)) {
            m_aspect.preset = key;
            m_aspect.enabled = (p.ratio > 0.0);
            m_aspect.ratio = p.ratio;
            // 只在用户真的改变时写配置，避免每次截图开始都写盘
            if (ConfigManager::instance()->value(QStringLiteral("snip/aspect_preset")).toString() != key)
                ConfigManager::instance()->setValue(QStringLiteral("snip/aspect_preset"), key);
            return;
        }
    }

    m_aspect.preset = QStringLiteral("free");
    m_aspect.enabled = false;
    m_aspect.ratio = 0.0;
}

void SnipOverlay::cycleAspectPreset()
{
    static const char *kOrder[] = { "free", "1:1", "4:3", "3:2", "16:9", "9:16" };
    int idx = 0;
    for (int i = 0; i < int(sizeof(kOrder) / sizeof(kOrder[0])); ++i) {
        if (m_aspect.preset == QLatin1String(kOrder[i])) {
            idx = i;
            break;
        }
    }
    setAspectPreset(QString::fromLatin1(kOrder[(idx + 1) % int(sizeof(kOrder) / sizeof(kOrder[0]))]));
    update();
}

int SnipOverlay::effectiveCornerRadius() const
{
    if (!m_roundedCorners)
        return 0;
    const qreal dpr = m_snapshot.devicePixelRatio;
    const QRect sel = localSelection();
    return ImageEffects::clampCornerRadius(qRound(m_cornerRadius * dpr),
                                           QSize(qRound(sel.width() * dpr),
                                                 qRound(sel.height() * dpr)));
}

void SnipOverlay::toggleRoundedCorners()
{
    m_roundedCorners = !m_roundedCorners;
    // 打开时给一个肉眼可见的半径，别用 1~3px 那种看不出效果的值
    if (m_roundedCorners && m_cornerRadius < kMinCornerRadius)
        m_cornerRadius = kDefaultCornerRadius;
    ConfigManager::instance()->setValue(QStringLiteral("snip/corner_radius"),
                                        m_roundedCorners ? m_cornerRadius : 0);
    update();
}

void SnipOverlay::adjustCornerRadius(int delta)
{
    if (!m_roundedCorners)
        return;
    m_cornerRadius = qBound(kMinCornerRadius, m_cornerRadius + delta, 50);
    ConfigManager::instance()->setValue(QStringLiteral("snip/corner_radius"), m_cornerRadius);
    update();
}

void SnipOverlay::toggleShadowPreview()
{
    m_shadowPreview = !m_shadowPreview;
    ConfigManager::instance()->setValue(QStringLiteral("snip/shadow_enabled"), m_shadowPreview);
    update();
}

// ---------------------------------------------------------------- 长截图

void SnipOverlay::requestLongScreenshot()
{
    if (m_longShotState != LongShotState::Off)
        return;

    if (!localSelection().isEmpty() && m_state == State::Adjusting) {
        enterLongShot();
        return;
    }

    m_longShotIntent = true;
    showCenterNotice(QStringLiteral("长截图模式：框选区域后自动进入"));
}

// 进入长截图：弹出独立的图标工具栏（开始/暂停/保存/复制/钉/关闭）。
// 外圈与普通截图一致（冻结快照 + 半透明黑遮罩）；选区用 setMask 从窗口
// 形状里挖掉（真"天窗"），实时页面直接透出来；鼠标穿透 + 焦点交给下层
// 窗口，滚轮才能滚动页面。
void SnipOverlay::enterLongShot()
{
    if (m_longShotState != LongShotState::Off || localSelection().isEmpty())
        return;

    // 文字输入还开着先提交，避免悬空丢字
    if (m_textBox) {
        TextInputBox *box = m_textBox;
        m_textBox = nullptr;
        box->commit();
    }

    m_longShotRect = globalSelection();
    if (m_longShotRect.isEmpty())
        return;
    m_stitcher.reset();
    m_longShotState = LongShotState::Paused;
    m_longShotIntent = false;
    m_longShotStatus.clear();
    clearDetection();

    // 连续刷新（F5 长按）全程停用：refreshSnapshot 的 hide/show + 抢焦点
    // 会打断页面滚动，长截图期间也不需要重抓底图
    endContinuousRefresh();

    // 真"天窗"：把选区从窗口形状里挖掉。选区位置不再是本窗口的一部分，
    // 下面的实时页面直接透出来，滚动看得见、抓帧也天然干净；
    // 不用透明像素——截图层必须保持不透明（见构造函数的说明）
    setMask(QRegion(rect()) - QRegion(localSelection()));

    // 正常截图的工具栏/输入框全部退场，两者不混在一起
    hidePaintBar();
    if (m_sizeBox)
        m_sizeBox->hide();

    if (!m_longShotBar) {
        m_longShotBar = new LongShotBar();
        connect(m_longShotBar, &LongShotBar::startRequested,
                this, &SnipOverlay::startLongCapture);
        connect(m_longShotBar, &LongShotBar::pauseRequested,
                this, &SnipOverlay::pauseLongCapture);
        connect(m_longShotBar, &LongShotBar::closeRequested,
                this, &SnipOverlay::cancelLongShot);
        connect(m_longShotBar, &LongShotBar::saveRequested,
                this, &SnipOverlay::saveLongShot);
        connect(m_longShotBar, &LongShotBar::copyRequested,
                this, &SnipOverlay::copyLongShot);
        connect(m_longShotBar, &LongShotBar::pinRequested,
                this, &SnipOverlay::pinLongShot);
    }
    m_longShotBar->positionNear(m_longShotRect);
    m_longShotBar->show();
    m_longShotBar->raise();
    WinUtil::applyToolWindowStyle(m_longShotBar->winId());
    WinUtil::applyNoActivateStyle(m_longShotBar->winId());

    // 输入放行 + 焦点让给下层窗口（滚轮默认发给焦点窗口）
    WinUtil::setClickThrough(winId(), true);
    WinUtil::activateWindowAt(QCursor::pos());

    update();
    syncLongShotUi();
}

void SnipOverlay::startLongCapture()
{
    if (m_longShotState != LongShotState::Paused)
        return;
    m_longShotState = LongShotState::Capturing;
    // 每次开始都重新放行输入/让出焦点（暂停期间焦点可能被别的窗口拿走）
    WinUtil::setClickThrough(winId(), true);
    WinUtil::activateWindowAt(QCursor::pos());
    grabLongShotFrame();   // 先抓一帧打底/续接
    m_longShotTimer->start();
    syncLongShotUi();
}

void SnipOverlay::pauseLongCapture()
{
    if (m_longShotState != LongShotState::Capturing)
        return;
    m_longShotTimer->stop();
    m_longShotState = LongShotState::Paused;
    syncLongShotUi();
}

// 关闭：直接退出整个截图会话（不返回普通截图）
void SnipOverlay::cancelLongShot()
{
    if (m_longShotState == LongShotState::Off)
        return;
    m_longShotTimer->stop();
    m_stitcher.reset();
    m_longShotState = LongShotState::Off;
    WinUtil::setClickThrough(winId(), false);
    if (m_longShotBar)
        m_longShotBar->hide();
    abortSnip();
}

void SnipOverlay::saveLongShot()
{
    if (m_longShotState == LongShotState::Off || m_stitcher.isEmpty())
        return;
    pauseLongCapture();
    if (m_longShotBar)
        m_longShotBar->hide();
    if (!finishSnip(SnipOutput::FileDialog)) {
        // 用户取消了保存对话框：把工具栏放回来继续长截图
        if (m_longShotBar) {
            m_longShotBar->show();
            m_longShotBar->raise();
        }
    }
}

void SnipOverlay::copyLongShot()
{
    if (m_longShotState == LongShotState::Off || m_stitcher.isEmpty())
        return;
    pauseLongCapture();
    if (m_longShotBar)
        m_longShotBar->hide();
    finishSnip(SnipOutput::Copy);
}

// 钉：把长图贴到屏幕（和普通截图的 Ctrl+T 同一条输出链路）
void SnipOverlay::pinLongShot()
{
    if (m_longShotState == LongShotState::Off || m_stitcher.isEmpty())
        return;
    pauseLongCapture();
    if (m_longShotBar)
        m_longShotBar->hide();
    finishSnip(SnipOutput::Pin);
}

void SnipOverlay::syncLongShotUi()
{
    if (!m_longShotBar)
        return;
    m_longShotBar->setRunning(m_longShotState == LongShotState::Capturing);
    m_longShotBar->setHasContent(!m_stitcher.isEmpty());
}

void SnipOverlay::grabLongShotFrame()
{
    if (m_longShotState != LongShotState::Capturing || m_longShotRect.isEmpty())
        return;
    // 防重入：抓帧本身推迟到事件循环下一轮（见下），150ms 定时器可能再次触发
    if (m_longShotGrabbing)
        return;
    m_longShotGrabbing = true;

    // 长截图工具栏压住捕获区域时，抓帧期间藏起来防止被拍进长图；
    // 不相交就不会进画面，不藏（避免无谓闪烁）。
    // 选区是挖掉的"天窗"，遮罩/圆环/提示都画在捕获区外，抓帧天然干净。
    bool hideBar = false;
    if (m_longShotBar && m_longShotBar->isVisible()) {
        const QRect barRect(m_longShotBar->pos(), m_longShotBar->size());
        if (barRect.intersects(m_longShotRect)) {
            m_longShotBar->hide();
            hideBar = true;
        }
    }

    // 藏完等一拍让 DWM 合成后再抓（没藏就下一拍抓，逻辑统一）。
    // 不能在这里 processEvents 等合成：那会重入定时器/事件处理器，
    // 抓帧、拼接、隐藏全部乱套——这就是之前"越滚越卡、时好时坏"的来源。
    QTimer::singleShot(hideBar ? 40 : 0, this, [this, hideBar]() {
        const QImage frame = ScreenCapturer::captureRegion(m_longShotRect);
        if (hideBar && m_longShotBar) {
            m_longShotBar->show();
            m_longShotBar->raise();
        }
        m_longShotGrabbing = false;

        if (m_longShotState != LongShotState::Capturing || frame.isNull())
            return;

        if (!m_stitcher.feed(frame)) {
            // 画布到顶自动暂停；其余情况（没滚动/匹配不可靠）静默丢弃该帧
            if (m_stitcher.isFull()) {
                pauseLongCapture();
                m_longShotStatus = QStringLiteral("已达最大高度，已自动暂停");
                update();
            }
            return;
        }

        m_longShotStatus = QStringLiteral("已拼接 %1 帧 · 高 %2 px")
                               .arg(m_stitcher.acceptedFrames())
                               .arg(m_stitcher.height());
        syncLongShotUi();
        update();
    });
}

// 长截图态的外框：圆环画在选区外侧 + 状态提示，全部严格在捕获区外
void SnipOverlay::drawLongShotRing(QPainter &p)
{
    const QRect sel = localSelection();
    if (sel.isEmpty())
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // 圆环中心线在选区外 3px、笔宽 3 → 覆盖外侧 -4.5~-1.5px，全程在捕获区外
    p.setPen(QPen(Theme::kAccent, 3));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(sel).adjusted(-3, -3, 3, 3), 8, 8);

    const QString text = m_longShotStatus.isEmpty()
                             ? QStringLiteral("滚轮滚动页面拼接长图")
                             : m_longShotStatus;
    QFont f = font();
    f.setPointSize(8);
    f.setBold(true);
    p.setFont(f);
    QRectF chip = p.fontMetrics().boundingRect(text).adjusted(-12, -5, 12, 6);
    chip.moveLeft(sel.left() + (sel.width() - chip.width()) / 2);
    chip.moveTop(sel.top() - 9 - chip.height());
    if (chip.top() < 2)
        chip.moveTop(sel.bottom() + 9);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 170));
    p.drawRoundedRect(chip, 6, 6);
    p.setPen(Qt::white);
    p.drawText(chip, Qt::AlignCenter, text);
    p.restore();
}

void SnipOverlay::refreshSnapshot()
{
    // 长截图期间禁止重抓：hide/show 闪烁 + activateWindow 抢焦点都会打断页面滚动
    if (m_longShotState != LongShotState::Off)
        return;
    if (m_refreshing || m_state == State::Dragging)
        return;
    // 正在输入文字 / 尺寸时不能重抓，会把输入框状态打断
    if (m_textBox || m_sizeBox)
        return;

    m_refreshing = true;

    // 自己先藏起来，否则会把遮罩和标注拍进新快照。
    // processEvents 期间窗口可能被延迟销毁，用 QPointer 兜住
    QPointer<SnipOverlay> selfGuard(this);
    const bool wasVisible = isVisible();
    if (wasVisible) {
        hide();
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    const ScreenCapturer::Snapshot fresh = ScreenCapturer::captureAllScreens();
    if (!selfGuard)
        return;

    if (wasVisible) {
        show();
        raise();
        activateWindow();
        setFocus(Qt::OtherFocusReason);
    }

    m_refreshing = false;

    if (!fresh.isValid() || fresh.virtualGeometry != m_snapshot.virtualGeometry) {
        m_refreshHintVisible = true;
        update();
        QTimer::singleShot(1200, this, [this]() {
            m_refreshHintVisible = false;
            update();
        });
        return;
    }

    m_snapshot = fresh;
    m_snapshotImage = fresh.pixmap.toImage();
    invalidateBackgroundCache();
    m_snapshotImage.setDevicePixelRatio(1.0);

    // 底图换了，马赛克/模糊/放大镜的缓存必须重算
    PixelShape::setSourceImage(m_snapshotImage, fresh.devicePixelRatio);
    for (const auto &shape : m_model->shapes()) {
        if (!shape)
            continue;
        shape->invalidateCache();
        if (auto *mag = dynamic_cast<MagnifyShape *>(shape.get())) {
            mag->setSourceRect(mag->sourceRect(), m_snapshotImage,
                               fresh.devicePixelRatio, QPointF(0, 0));
        }
    }

    m_refreshHintVisible = true;
    update();
    QTimer::singleShot(1200, this, [this]() {
        m_refreshHintVisible = false;
        update();
    });
}

void SnipOverlay::beginContinuousRefresh()
{
    if (!m_refreshTimer->isActive())
        m_refreshTimer->start();
}

void SnipOverlay::endContinuousRefresh()
{
    m_refreshTimer->stop();
}

// ---------------------------------------------------------------- P4 尺寸输入

QRect SnipOverlay::sizeLabelRect() const
{
    const QRect sel = localSelection();
    if (sel.isEmpty())
        return {};

    const bool useDip = ConfigManager::instance()
                            ->value(QStringLiteral("interface/size_unit_dip"))
                            .toBool();
    const qreal dpr = m_snapshot.devicePixelRatio;
    const QSize shown = useDip ? sel.size()
                               : QSize(qRound(sel.width() * dpr),
                                       qRound(sel.height() * dpr));
    QFont f = Theme::appFont();
    f.setPointSize(9);
    const QFontMetrics fm(f);
    const QString text = QStringLiteral("%1 × %2").arg(shown.width()).arg(shown.height());
    const QSize textSize = fm.size(Qt::TextSingleLine, text);
    const int w = textSize.width() + 12;
    const int h = textSize.height() + 6;

    int x = sel.left();
    int y = sel.top() - h - 6;
    if (y < 0)
        y = qMax(0, sel.top() + 4);
    if (x + w > width())
        x = width() - w - 4;
    if (x < 0)
        x = 0;
    return QRect(x, y, w, h);
}

void SnipOverlay::beginSizeInput()
{
    const QRect sel = localSelection();
    if (sel.isEmpty())
        return;
    if (m_sizeBox)
        m_sizeBox->deleteLater();

    const bool useDip = ConfigManager::instance()
                            ->value(QStringLiteral("interface/size_unit_dip"))
                            .toBool();
    const qreal dpr = m_snapshot.devicePixelRatio;
    const QSize current = useDip ? sel.size()
                                 : QSize(qRound(sel.width() * dpr), qRound(sel.height() * dpr));

    auto *box = new SizeInputBox(this);
    m_sizeBox = box;

    connect(box, &SizeInputBox::committed, this, [this, useDip](const QSize &size) {
        m_sizeBox = nullptr;
        activateWindow();
        setFocus(Qt::OtherFocusReason);

        const qreal dpr = m_snapshot.devicePixelRatio;
        const QSize logical = useDip ? size
                                     : QSize(qRound(size.width() / dpr),
                                             qRound(size.height() / dpr));
        QRect r = localSelection();
        r.setSize(logical.expandedTo(QSize(kMinSelection, kMinSelection)));
        // 只平移不裁剪：超出屏幕时把选区整体推回来，保持用户输入的尺寸
        if (r.right() > width() - 1)
            r.moveRight(width() - 1);
        if (r.bottom() > height() - 1)
            r.moveBottom(height() - 1);
        if (r.left() < 0)
            r.moveLeft(0);
        if (r.top() < 0)
            r.moveTop(0);

        if (r.width() >= kMinSelection && r.height() >= kMinSelection) {
            m_selection = r;
            m_state = State::Adjusting;
            if (m_paintBarVisible)
                syncPaintBarPosition();
        }
        update();
    });

    connect(box, &SizeInputBox::cancelled, this, [this]() {
        m_sizeBox = nullptr;
        activateWindow();
        setFocus(Qt::OtherFocusReason);
        update();
    });

    box->startEditing(current, useDip ? QStringLiteral("dip") : QStringLiteral("px"));

    // 贴在尺寸标签上方
    const QRect label = sizeLabelRect();
    const QPoint pos(label.left(), qMax(0, label.top() - box->height() - 2));
    box->move(pos);
}

void SnipOverlay::endSizeInput()
{
    if (m_sizeBox)
        m_sizeBox->cancel();
}

// ---------------------------------------------------------------- P4 其他

void SnipOverlay::captureActiveWindow(const QRect &knownActiveRect)
{
    // 活动窗口应该在截图层弹出之前就取好：截图层一旦 show 就会成为前台窗口。
    // AppController 在热键触发的瞬间取矩形并传进来；为空时才退回自己探测。
    QRect active = knownActiveRect;

    if (active.isEmpty()) {
        // 自己探测：先隐藏，让前台回到目标窗口，再取 GetForegroundWindow
        QPointer<SnipOverlay> selfGuard(this);
        hide();
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        active = WinUtil::activeWindowRect();
        if (!selfGuard)
            return;
    }

    const ScreenCapturer::Snapshot fresh = ScreenCapturer::captureAllScreens();
    if (!fresh.isValid())
        return;

    m_snapshot = fresh;
    m_snapshotImage = fresh.pixmap.toImage();
    invalidateBackgroundCache();
    m_snapshotImage.setDevicePixelRatio(1.0);
    PixelShape::setSourceImage(m_snapshotImage, fresh.devicePixelRatio);
    setGeometry(fresh.virtualGeometry);

    show();
    raise();
    activateWindow();
    setFocus(Qt::OtherFocusReason);

    if (active.isEmpty())
        return;

    m_selection = QRect(active.topLeft() - virtualTopLeft(), active.size()).intersected(rect());
    if (m_selection.isEmpty())
        return;

    m_state = State::Adjusting;
    showPaintBar();
    update();
}

void SnipOverlay::recordAbortedSnip()
{
    if (!ConfigManager::instance()->value(QStringLiteral("general/record_all_snips")).toBool())
        return;
    if (localSelection().isEmpty())
        return;

    const QImage image = composeResultImage();
    if (image.isNull())
        return;
    SnipHistory::instance()->add(image, globalSelection());
}

QImage SnipOverlay::postProcessForOutput(const QImage &image, const QString &path) const
{
    QImage out = image;
    if (out.isNull())
        return out;

    const int radius = effectiveCornerRadius();
    if (radius > 0)
        out = ImageEffects::applyRoundedCorners(out, radius);

    if (m_shadowPreview) {
        const int blur = ConfigManager::instance()
                             ->value(QStringLiteral("general/snip_shadow_blur"), 8).toInt();
        const int offset = ConfigManager::instance()
                               ->value(QStringLiteral("general/snip_shadow_offset"), 8).toInt();
        const int border = ConfigManager::instance()
                               ->value(QStringLiteral("general/snip_shadow_border"), 1).toInt();
        out = ImageEffects::applyShadow(out, blur, offset, border);
    }

    // JPEG/BMP 不支持 alpha：圆角外/阴影外必须拍平到白底
    if (!path.isEmpty()) {
        const QString suffix = path.section(QLatin1Char('.'), -1).toLower();
        if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")
            || suffix == QStringLiteral("bmp")) {
            out = ImageEffects::flattenOnWhite(out);
        }
    }

    return out;
}

void SnipOverlay::copyImageToClipboard(const QImage &image) const
{
    QClipboard *clip = QApplication::clipboard();
    if (!clip)
        return;

    if (!ConfigManager::instance()->value(QStringLiteral("general/copy_image_as_file")).toBool()) {
        clip->setImage(image);
        OperationToast::imageCopied();
        return;
    }

    // 同时放图片和文件路径：资源管理器粘贴会落成 .png 文件
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                        + QStringLiteral("/FCapture");
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(
        QStringLiteral("clip_%1.png")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"))));

    if (!image.save(path, "PNG")) {
        clip->setImage(image);
        OperationToast::imageCopied();
        return;
    }

    auto *mime = new QMimeData();
    mime->setImageData(image);
    mime->setUrls({QUrl::fromLocalFile(path)});

    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
        mime->setData(QStringLiteral("image/png"), f.readAll());

    clip->setMimeData(mime);
    OperationToast::imageCopied();
}

// 当前选区的成品图（含标注，已应用圆角/阴影）
QImage SnipOverlay::selectionImage() const
{
    if (localSelection().isEmpty())
        return {};
    return postProcessForOutput(composeResultImage());
}

// ---------------------------------------------------------------- P5 OCR

void SnipOverlay::restorePaintBarAfterAsync()
{
    if (!m_paintBar || !m_paintBarVisible)
        return;
    show();
    raise();
    m_paintBar->show();
    m_paintBar->raise();
    activateWindow();
    setFocus(Qt::OtherFocusReason);
}

void SnipOverlay::runOcr()
{
    const QImage image = selectionImage();
    if (image.isNull()) {
        m_paintBar->setTool(Tool::None);
        return;
    }

    // 先隐藏截图层，让用户看到"识别中"而不是一直卡在遮罩上
    if (m_paintBar)
        m_paintBar->hide();

    if (!m_ocr)
        m_ocr = new OcrEngine(this);

    const QStringList langs = OcrEngine::availableLanguages();
    if (langs.isEmpty()) {
        show();
        raise();
        activateWindow();
        setFocus(Qt::OtherFocusReason);
        if (m_paintBar) {
            m_paintBar->show();
            m_paintBar->raise();
        }
        return;
    }

    // 识别用的图先存一份：识别成功后截图层会关闭，重试时还要用
    const QImage sourceImage = image;

    QMetaObject::Connection *okConn = new QMetaObject::Connection;
    QMetaObject::Connection *errConn = new QMetaObject::Connection;

    *okConn = connect(m_ocr, &OcrEngine::finished, this,
                      [this, langs, sourceImage, okConn, errConn](const WinOcr::Result &result) {
                          disconnect(*okConn);
                          disconnect(*errConn);
                          delete okConn;
                          delete errConn;

                          // 识别成功：整个截图会话结束，只留结果对话框。
                          // 否则遮罩/选区还压在桌面上，对话框会被挡住或一闪而过。
                          releaseSessionImages();
                          if (m_paintBar)
                              m_paintBar->hide();
                          close();

                          // 不能以截图层为父窗口：SnipOverlay 挂着写死暗色的
                          // overlayStyleSheet，会沿父子链把 Ela 的亮/暗配色盖掉。
                          auto *dialog = new OcrResultDialog(result.text, langs,
                                                             result.language, nullptr);
                          dialog->setWindowFlag(Qt::WindowStaysOnTopHint, true);
                          connect(dialog, &OcrResultDialog::retryRequested, dialog,
                                  [this, sourceImage, dialog](const QString &lang) {
                                      m_ocr->setLanguage(lang);
                                      m_ocr->recognize(sourceImage);
                                  });
                          dialog->setAttribute(Qt::WA_DeleteOnClose);
                          dialog->show();

                          QApplication::clipboard()->setText(result.text);
                      });

    *errConn = connect(m_ocr, &OcrEngine::failed, this,
                       [this, okConn, errConn](const QString &error) {
                           disconnect(*okConn);
                           disconnect(*errConn);
                           delete okConn;
                           delete errConn;
                           Q_UNUSED(error);
                           // 识别失败：恢复截图层，用户可以继续框选重试
                           restorePaintBarAfterAsync();
                       });

    m_ocr->recognize(image);
}

void SnipOverlay::runBarcodeScan()
{
    m_paintBar->setTool(Tool::None);

    const QImage image = selectionImage();
    if (image.isNull()) {
        showCenterNotice(QStringLiteral("请先框选要识别的区域"));
        return;
    }

    // 每次识别先清掉上次的高亮框
    m_barcodeRects.clear();

    // 先在原图上扫一遍；扫不到且开了"努力模式"再试一次
    // （同步调用：选区图通常很小，几十毫秒内出结果）
    QVector<QrScanner::Result> results = QrScanner::scan(image, false);
    if (results.isEmpty())
        results = QrScanner::scan(image, true);

    if (results.isEmpty()) {
        showCenterNotice(QStringLiteral("未识别到二维码或条码"));
        update();
        return;
    }

    QVector<BarcodeResultDialog::Item> items;
    items.reserve(results.size());
    for (const auto &r : results) {
        items.append({r.text, r.format});

        // 结果位置是图片像素坐标：除以 dpr 换回逻辑坐标，再加上选区左上角
        const qreal dpr = m_snapshot.devicePixelRatio;
        const QRect sel = localSelection();
        const QRectF logical(r.position.x() / dpr, r.position.y() / dpr,
                             r.position.width() / dpr, r.position.height() / dpr);
        m_barcodeRects.append(logical.translated(sel.topLeft()));
    }
    update();

    // 不能以截图层为父窗口：SnipOverlay 挂着写死暗色的 overlayStyleSheet，
    // 会沿父子链把 Ela 的亮/暗配色盖掉（字看不清）
    auto *dialog = new BarcodeResultDialog(items, nullptr);
    dialog->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void SnipOverlay::showCenterNotice(const QString &text, int msec)
{
    m_centerNotice = text;
    m_centerNoticeTimer.restart();
    update();

    // 到点自动清除（定时器只负责重绘，时间判断在 drawCenterNotice 里做）
    QTimer::singleShot(msec, this, [this, msec]() {
        if (m_centerNoticeTimer.isValid() && m_centerNoticeTimer.elapsed() >= msec - 50) {
            m_centerNotice.clear();
            update();
        }
    });
}

void SnipOverlay::drawCenterNotice(QPainter &p)
{
    if (m_centerNotice.isEmpty())
        return;
    // 超过 2.6s 兜底清理（singleShot 可能因多次调用而错位）
    if (m_centerNoticeTimer.isValid() && m_centerNoticeTimer.elapsed() > 3200) {
        m_centerNotice.clear();
        return;
    }

    QFont f = Theme::appFont();
    f.setPointSize(11);
    const QFontMetrics fm(f);

    const int padH = 28;
    const int padV = 18;
    const int w = qMin(width() - 80, fm.horizontalAdvance(m_centerNotice) + padH * 2);
    const int h = fm.height() + padV * 2;

    const QRect box((width() - w) / 2, (height() - h) / 2, w, h);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 220));
    p.drawRoundedRect(box, 8, 8);

    p.setPen(QPen(QColor(0x3D, 0x8B, 0xFD), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(box.adjusted(0, 0, -1, -1), 8, 8);

    p.setFont(f);
    p.setPen(QColor(0xF2, 0xF2, 0xF2));
    p.drawText(box, Qt::AlignCenter, m_centerNotice);
    p.restore();
}

void SnipOverlay::sendToApplication()
{
    if (localSelection().isEmpty())
        return;

    const QImage image = postProcessForOutput(composeResultImage());
    if (image.isNull())
        return;

    const QVector<InstalledApp> apps = AppLauncher::installedApps();

    AppMenu::Menu menu(this);
    for (const InstalledApp &app : apps) {
        QAction *act = menu->addAction(app.name);
        connect(act, &QAction::triggered, this, [image, app]() {
            const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                                + QStringLiteral("/FCapture");
            QDir().mkpath(dir);
            const QString path = QDir(dir).filePath(
                QStringLiteral("send_%1.png")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"))));
            if (image.save(path, "PNG"))
                AppLauncher::openWith(app.executablePath, path);
        });
    }

    if (apps.isEmpty()) {
        QAction *act = menu->addAction(QStringLiteral("没有找到可用应用…"));
        act->setEnabled(false);
    }

    menu->addSeparator();
    QAction *choose = menu->addAction(QStringLiteral("选择程序…"));
    connect(choose, &QAction::triggered, this, [this, image]() {
        const QString exe = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择要发送到的程序"), QString(),
            QStringLiteral("可执行文件 (*.exe)"));
        if (exe.isEmpty())
            return;

        const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                            + QStringLiteral("/FCapture");
        QDir().mkpath(dir);
        const QString path = QDir(dir).filePath(
            QStringLiteral("send_%1.png")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"))));
        if (image.save(path, "PNG"))
            AppLauncher::openWith(exe, path);
    });

    menu->exec(QCursor::pos());
}
