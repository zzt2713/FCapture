#include "snip/SnipOverlay.h"

#include "annotate/AnnotationModel.h"
#include "annotate/PaintBar.h"
#include "annotate/TextStyleBar.h"
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

#include "platform/win/HotkeyHook.h"
#include "platform/win/QrScanner.h"
#include "snip/MagnifierPanel.h"
#include "snip/LongShotBar.h"
#include "snip/LongShotPreview.h"
#include "snip/LongShotPreviewBar.h"
#include "snip/ShortcutTipsWidget.h"

#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QTextEdit>

#include <QApplication>
#include <QClipboard>
#include <QCursor>

#include <algorithm>
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
    // 鼠标进出工具栏：隐藏/恢复放大镜与取色面板。
    // 工具栏是子控件，鼠标在上面时 SnipOverlay 收不到 mouseMove，
    // 不在这里主动关掉的话放大镜会一直挂在最后一次的位置。
    connect(m_paintBar, &PaintBar::pointerEntered, this, [this]() {
        m_magnifierOn = false;
        if (m_magnifierPanel)
            m_magnifierPanel->hidePanel();
        update();
    });
    connect(m_paintBar, &PaintBar::pointerLeft, this, [this]() {
        updateMagnifier();
        update();
    });
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
    connect(m_paintBar, &PaintBar::pinWindowRequested, this, &SnipOverlay::pinSelectedWindow);
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

    // 文字浮动样式工具栏：选中单个文字标注时显示在其上方
    m_textStyleBar = new TextStyleBar(this);
    connect(m_textStyleBar, &TextStyleBar::styleChanged, this, [this]() {
        // 样式改动后同步回 ToolStyleStore，供下次新建文字使用
        if (m_selectedShapes.size() == 1) {
            if (auto *txt = dynamic_cast<TextShape *>(m_selectedShapes.front().get())) {
                ToolStyle ns = ToolStyleStore::instance()->style(Tool::Text);
                ns.color = txt->color();
                ns.penWidth = txt->fontSize();
                ns.textBg = txt->backgroundColor();
                ns.textOutline = txt->outline();
                ns.textOutlineColor = txt->outlineColor();
                ToolStyleStore::instance()->setStyle(Tool::Text, ns);
            }
        }
        update();
    });

    // 元素检测：结果在 500ms 内回来才用，超时静默丢弃
    connect(ElementDetector::instance(), &ElementDetector::detected, this,
            [this](quint64 id, const QVector<ElementInfo> &hierarchy) {
                if (id != m_elementRequestId)
                    return;
                m_elementPending = false;
                m_refining = false;
                m_elementHierarchy = hierarchy;
                if (m_elementHierarchy.isEmpty()) {
                    // 没检测到元素：待切层数作废（否则会带到下一个窗口）
                    m_pendingLevelDelta = 0;
                    const QRect win = WindowDetector::bestWindowRectAt(m_elementRequestPos);
                    m_hoverWindow = win.isEmpty()
                                        ? QRect()
                                        : QRect(win.topLeft() - virtualTopLeft(), win.size())
                                              .intersected(rect());
                    update();
                } else {
                    // 基础层级：光标下最深的那个块（"指哪块框哪块"）。
                    // 若滚轮在重查期间累计过待切层数，叠加应用 —— 这样
                    // "移动鼠标后立刻滚轮"不会丢事件（snow 的重查+切换同时做）。
                    const int baseLevel = 0;
                    const int total = baseLevel + m_pendingLevelDelta;
                    m_pendingLevelDelta = 0;
                    applyElementLevel(total);
                }

                // 流水线：结果回来时鼠标可能已经移走了，
                // 立刻用**最新位置**再发一次，保证视觉上始终跟手。
                if (m_state == State::Idle && m_detectMode != DetectMode::Free
                    && (m_pendingCursor - m_elementRequestPos).manhattanLength() >= 3) {
                    requestElementDetection();
                    return;
                }

                // 指针没动：安排 80ms 后的精化（snow 的 scheduleRefinement 同款）。
                // 首次结果可能因为预算用完只到容器层，精化会把懒加载树
                // 沿命中路径再展开几层，拿到更具体的控件。
                if (m_state == State::Idle && m_detectMode != DetectMode::Free
                    && !hierarchy.isEmpty()) {
                    if (!m_refineGate) {
                        m_refineGate = new QTimer(this);
                        m_refineGate->setSingleShot(true);
                        m_refineGate->setTimerType(Qt::PreciseTimer);
                        m_refineGate->setInterval(80);
                        connect(m_refineGate, &QTimer::timeout, this, [this]() {
                            if (m_state != State::Idle
                                || m_detectMode == DetectMode::Free
                                || m_refining)
                                return;
                            // 指针又移动过就放弃这次精化
                            if ((m_pendingCursor - m_refineAt).manhattanLength() >= 3)
                                return;
                            m_refining = true;
                            ElementDetector::instance()->requestRefinement(
                                m_refineAt, m_refineHwnd);
                        });
                    }
                    m_refineAt = m_elementRequestPos;
                    m_refineHwnd = topWindowHwndAtCached(m_refineAt);
                    m_refineGate->start();
                }
            });
    // 快照就绪：缓存下来，之后鼠标移动全部走本地命中测试
    connect(ElementDetector::instance(), &ElementDetector::snapshotReady, this,
            [this](quint64, const QVector<ElementInfo> &elements) {
                m_snapshotElements = elements;
                qInfo() << "Element snapshot ready:" << elements.size() << "elements";
                // 立刻用当前光标位置刷新一次高亮
                if (m_detectMode != DetectMode::Free && m_state == State::Idle) {
                    const QVector<ElementInfo> chain = hitTestSnapshot(QCursor::pos());
                    if (!chain.isEmpty()) {
                        m_elementHierarchy = chain;
                        applyElementLevel(0);
                    }
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

    // 自动滚动：每 350ms 向下滚一格（120 单位 = 标准滚轮一格）。
    // 关键：滚动后要等页面动画稳定再抓帧，否则抓到滚到一半的模糊帧
    // 或被截断的帧，拼进长图就是"重影"。所以这里停 120ms 再抓。
    m_autoScrollTimer = new QTimer(this);
    m_autoScrollTimer->setInterval(350);
    connect(m_autoScrollTimer, &QTimer::timeout, this, [this]() {
        if (m_longShotState != LongShotState::Capturing || m_longShotRect.isEmpty())
            return;

        // 滚轮发给捕获区域中心下方的窗口（选区中心，避开工具栏）
        const QPoint center = m_longShotRect.center();
        WinUtil::forwardWheel(winId(), -120, center);   // 负值 = 向下滚动

        // 等滚动动画结束再抓帧（浏览器平滑滚动通常 100~200ms）
        QTimer::singleShot(160, this, [this]() {
            if (m_longShotState == LongShotState::Capturing)
                grabLongShotFrame();
        });
    });
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
    delete m_longShotHoverPreview;
    m_longShotHoverPreview = nullptr;

    m_textStyleBar = nullptr;   // 子控件，由 Qt 父子关系销毁


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
    // 默认开启自动检测：配置项缺失时 value() 返回空、toBool() = false，
    // 会让截图层以自由选区启动、完全不做元素识别（表现为"识别不了"）。
    // Snipaste 的手感是指哪块框哪块，所以缺省值必须是 true。
    m_windowDetectionOn = ConfigManager::instance()
                              ->value(QStringLiteral("snip/auto_detect_window"), true)
                              .toBool();
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
    m_cornerRadiusDragCorner = -1;
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
    // 压过任务栏/桌面宠物这类 WS_EX_TOPMOST 窗口：同在置顶带时 z 序由
    // 先后决定，raise() 不够——盖不住它们时任务栏上既不压暗也看不到高亮
    //（"识别不了任务栏"的另一半原因）
    WinUtil::forceTopmost(winId());

    // 进入截图时抓一次全量元素快照（异步，不阻塞界面）。
    // 之后鼠标移动全部走本地命中测试，不再跨进程调 UIA。
    // 不再抓全量元素快照：ControlView 会把具体控件裁掉大半（Chrome 实测
    // 1015 → 200），快照里只剩大容器 → "只识别到大框"。
    // 改为鼠标停下时按点实时下钻（CacheRequest 预取，见 deepestElementAt）。
    m_snapshotElements.clear();
    m_lastChain.clear();
    m_lastChainPos = QPoint();
    m_topWinCacheHwnd = 0;
    m_topWinCacheTimer.invalidate();
    m_refining = false;
    if (m_refineGate)
        m_refineGate->stop();
    // 新会话：懒加载树缓存作废（窗口结构可能已变）
    ElementDetector::instance()->clearCache();

    // 会话期持续压住置顶带：任务栏/桌面宠物是 WS_EX_TOPMOST，explorer 还会
    // 周期性重断言置顶，单次压制会被抢回去——盖不住时任务栏上既不压暗
    // 也看不到高亮（"识别不了任务栏"的另一半原因）
    if (!m_topmostGuardTimer) {
        m_topmostGuardTimer = new QTimer(this);
        m_topmostGuardTimer->setInterval(300);
        connect(m_topmostGuardTimer, &QTimer::timeout, this, [this]() {
            // 长截图期间完全停手：工具栏/预览栏要浮在遮罩之上，抢 z 序会把
            // 它们压没。平时只在真被外进程置顶窗压住时才推一把——无谓的
            // SetWindowPos 会让 DWM 反复重排置顶带，反而添周期性微卡
            if (m_longShotState != LongShotState::Off)
                return;
            // 有任何弹出菜单/弹窗时别抢置顶：遮罩会盖到菜单上面，
            // 表现为"菜单一闪而过"或根本看不见。
            // 用 activePopupWidget 而不是只看 showSnipMenu 的标志位：
            // 工具栏的「发送到应用」「识别」等菜单也都走 AppMenu::Menu，
            // 逐个加标志容易漏。
            if (m_menuOpen || QApplication::activePopupWidget())
                return;
            if (!WinUtil::isForeignWindowAbove(winId()))
                return;
            WinUtil::forceTopmost(winId());
            // 遮罩被推到置顶带最前后，本进程自己的浮窗（取色面板/放大镜、
            // 工具栏、长截图预览栏）会一起被压到它下面——鼠标移动时
            // showPanel 里的 raise() 能救回来，鼠标一停就没人救，
            // 表现为"鼠标不动了，取色面板就消失"。这里统一补一次 raise。
            if (m_magnifierPanel)
                m_magnifierPanel->raise();
            if (m_paintBar && m_paintBar->isVisible())
                m_paintBar->raise();
            if (m_longShotPreviewBar && m_longShotPreviewBar->isVisible())
                m_longShotPreviewBar->raise();
            if (m_longShotBar && m_longShotBar->isVisible())
                m_longShotBar->raise();
        });
    }
    m_topmostGuardTimer->start();
    WinUtil::forceTopmost(winId());

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
    repaintSelectionOutline();
}

// 选区轮廓变化后的重绘入口。
//
// 这里**不做脏区优化**：脏区一旦漏算任何一块绘制内容（尺寸标签、锚点、
// 提示框、参考线…），旧像素就留在原处形成残影。之前为了省几毫秒改脏区，
// 结果拖拽时标签和提示框重影不断，得不偿失。
// 现在整屏重绘实测 4~5ms（放大镜/锚点在拖拽时已停用），60fps 绰绰有余。
void SnipOverlay::repaintSelectionOutline()
{
    m_outlinePaintRect = localSelection();
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
    repaintSelectionOutline();
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
        //   0. 右键落在**已选中的标注**上 → 弹标注菜单（置顶/置底/删除）
        //   1. 有选中标注但点在别处 → 取消选中
        //   2. 有激活工具 → 取消工具
        //   3. 都没有 → 才是取消截图（保持原有语义）
        if (!m_selectedShapes.empty()) {
            const QPointF localRight = QPointF(e->position().toPoint());
            bool onSelected = false;
            for (const auto &shape : m_selectedShapes) {
                if (shape && shape->hitTest(localRight, 4.0)) {
                    onSelected = true;
                    break;
                }
            }
            if (onSelected) {
                // 不设 suppress，并打上"这次右键是标注菜单"的标记：
                // contextMenuEvent 里靠它决定弹哪个菜单，避免重复判命中。
                m_annotationMenuPending = true;
                update();
                return;
            }
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
        // 圆角手柄优先级最高：它画在四角内侧，和缩放手柄位置可能重叠，
        // 但用户是按"看起来像圆角把手"去点的，应该先认它。
        int radiusCorner = -1;
        std::shared_ptr<Annotation> radiusShape;
        if (hitTestCornerRadiusHandle(local, &radiusCorner, &radiusShape)) {
            m_cornerRadiusDragCorner = radiusCorner;
            m_movingShape = radiusShape;
            m_resizeShapeHandleIndex = -1;
            m_activeAnchor = Anchor::None;
            m_state = State::Resizing;
            m_dragStart = local;
            return;
        }

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

    // ---- 快速选择（snow 的 quick_selection_enabled）----
    //
    // 即使某个绘制工具处于激活状态，鼠标点到**已有标注**上也应该先选中它，
    // 而不是在它上面叠画一个新图形。之前缺少这一步，导致"鼠标悬停有虚线框
    // 提示可以选中，但一点就开始画新东西"，用户会理解成标注无法选中。
    //
    // snow 在 policy.rs 里对除两种橡皮以外所有工具都开了
    // quick_selection_enabled，这里对齐：只有"矩形/涂抹类橡皮"不参与。
    if (m_state == State::Adjusting && !localSelection().isEmpty()
        && m_currentTool != Tool::Eraser && m_currentTool != Tool::Lasso
        && m_model->hitTestShape(QPointF(local), 4.0)) {
        // Ctrl 点按 additive 语义切换；普通点击替换选中
        selectShapeAt(local, ctrl);
        if (!m_selectedShapes.empty())
            return;
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
            // 有选中标注时，点在选区空白处 = 取消选中。
            // 否则用户想"退出选中状态"就会变成拖动整个选区，
            // 表现上像"点了没反应"，只能右键或按 Esc 才能退出。
            if (!m_selectedShapes.empty()) {
                clearSelection();
                update();
                return;
            }
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
    // 只在提示框可能显示的阶段才算几何——拖拽/调整中每帧都算纯浪费
    if (m_state == State::Idle && localSelection().isEmpty() && !m_refreshing) {
        const bool over = shortcutTipsRect().contains(local);
        if (over != m_hoveringTips) {
            m_hoveringTips = over;
            update();
        }
    } else if (m_hoveringTips) {
        m_hoveringTips = false;
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
        } else if (m_cornerRadiusDragCorner >= 0 && m_movingShape) {
            auto *rectShape = dynamic_cast<RectShape *>(m_movingShape.get());
            if (rectShape) {
                const QRectF r = m_movingShape->rawBoundingRect();
                // 该手柄对应的那个几何角
                const bool rightSide = (m_cornerRadiusDragCorner == 1
                                        || m_cornerRadiusDragCorner == 2);
                const bool bottomSide = (m_cornerRadiusDragCorner == 2
                                         || m_cornerRadiusDragCorner == 3);
                const QPointF corner(rightSide ? r.right() : r.left(),
                                     bottomSide ? r.bottom() : r.top());

                // 鼠标离这个角越近 → 内缩越小 → 圆角越小；
                // 离得越远 → 圆角越大。手感上等于"把弧往里推"。
                const qreal dist = QLineF(corner, QPointF(local)).length();
                const qreal maxRadius = maxCornerRadiusFor(m_movingShape);
                const qreal radius = qBound<qreal>(0.0, dist, maxRadius);
                rectShape->setCornerRadius(radius);
                m_movingShape->invalidateCache();
                update();
            }
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
        repaintSelectionOutline();
        break;
    }
    case State::Adjusting:
    case State::Idle: {
        // 已选中标注的形状手柄永远优先：画完图形会自动选中且工具仍激活，
        // 不先判手柄的话会一路落到"工具激活＝十字"，拖拽点永远是十字。
        if (!m_selectedShapes.empty() && m_state == State::Adjusting) {
            if (hitTestCornerRadiusHandle(local, nullptr, nullptr)) {
                // 圆角手柄：snow 用 CursorStyle::CornerRadius，Qt 没有对应形状，
                // 用 SizeAllCursor 表示"可拖拽调整"
                setCursor(Qt::SizeAllCursor);
            } else if (hitTestRotationHandle(local, nullptr)) {
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
                        case Anchor::None: setCursor(toolCursor());      break;
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
            // 选中工具后，整个画布（含选区内部）统一用该工具的"落笔光标"：
            // 此时鼠标的唯一用途就是落笔，在选区里显示抓手会误导。
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
            case Anchor::None: setCursor(toolCursor());      break;
            }
        } else if (m_state == State::Adjusting && !localSelection().isEmpty()) {
            // 未选工具：选区锚点给缩放光标，选区内给移动光标，其余箭头
            // （对齐 snow 的 Select 工具默认光标）
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
                // snow 的交互：鼠标经过某个标注时显示移动光标，
                // 提示"这里可以点选/拖动"。光标和虚线候选框一起出现。
                if (m_model->hitTestShape(QPointF(local), 4.0))
                    setCursor(Qt::SizeAllCursor);
                else
                    setCursor(hitTestSelection(local) ? Qt::SizeAllCursor : toolCursor());
                break;
            }
        } else {
            setCursor(toolCursor());
        }

        updateHoverWindow();
        updateMagnifier();
        updateHoveredShape(local);
        hoverRepaint();
        break;
    }
    }

    // Dragging/Resizing/Moving 的刷新由各分支自己按脏区发起（updateSelectionDrag
    // 只刷选区轮廓）。这里再补一次整屏 update() 会把脏区扩大成整屏，
    // 每帧多花约 4~5ms 整屏 blit —— 拖拽卡顿的主因。只有绘制形状
    // （Drawing 的支路没有自己刷）才需要兜底全量刷新。
    if (m_state == State::Drawing) {
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

    // 圆角拖动结束
    if (m_state == State::Resizing && m_cornerRadiusDragCorner >= 0) {
        m_cornerRadiusDragCorner = -1;
        m_state = State::Adjusting;
        m_movingShape.reset();
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
        // 用户手动滚过：重新计时，并在滚动动画结束后补抓一帧，
        // 否则手动滚动的内容永远不会进长图
        if (m_autoScrollTimer && m_autoScrollTimer->isActive()) {
            m_autoScrollTimer->start();
            QTimer::singleShot(160, this, [this]() {
                if (m_longShotState == LongShotState::Capturing)
                    grabLongShotFrame();
            });
        }
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
        // 切换复制格式（HEX ↔ RGB）：取色面板的高亮行和下一次复制的格式
        // 都跟着变。按住不放会因自动重复疯狂切换，所以只认非重复的按下。
        if (!e->isAutoRepeat()) {
            m_hexFormat = !m_hexFormat;
            showCenterNotice(m_hexFormat ? QStringLiteral("复制格式：HEX")
                                         : QStringLiteral("复制格式：RGB"));
            update();
        }
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
// 从快照里查光标下的元素链（纯本地，微秒级）。
//
// 对齐 Snow Shot 的三条不变式（cache.rs 的 query）：
//   1. 先按 Z 序定窗口，只在这棵树里找 —— 重叠窗口的元素绝不混入
//   2. 同层取"最上层"命中（他们 hit_before 用 rev()，后兄弟在上）
//   3. 结果单调收敛：同一位置的高亮只允许变小，绝不来回跳
QVector<ElementInfo> SnipOverlay::hitTestSnapshot(const QPoint &globalPos)
{
    // 位置没变：只允许"更精确"的链替换旧链，否则原样返回。
    // 快照是分批到达的，每批都重建链会让高亮跳来跳去（"扫到别处再恢复"）。
    const bool samePos = (globalPos == m_lastChainPos && !m_lastChain.isEmpty());

    // 归属按 HWND 判定：只认光标下最上层窗口（Z 序）的元素。
    // 以前用"元素中心落在最上层窗口矩形内"的几何判定，被遮挡窗口的元素
    // 混进来抢高亮（"上边的窗口无法正确识别"）；光标在桌面（没有最上层
    // 窗口）时更是完全不过滤，最小化窗口的元素全冒出来。
    const quintptr topHwnd = topWindowHwndAtCached(globalPos);
    if (topHwnd == 0) {
        if (samePos)
            return m_lastChain;
        m_lastChainPos = globalPos;
        m_lastChain.clear();
        return {};
    }

    // 快照来自"每窗口网格采样的逐层下钻"：每个元素带 UIA 真实深度
    // （同一次下钻里 depth 越小越靠内层；不同采样点的链各自独立）。
    // 选链规则：
    //   1. 只取包含光标的元素
    //   2. 同一"链"内按 depth 排成 内→外（用矩形是否互相包含判定同链）
    //   3. 最终取包含光标且面积最小的那条链（= 光标所在位置最具体的元素）
    QVector<ElementInfo> hits;
    for (const ElementInfo &e : m_snapshotElements) {
        if (e.nativeHandle != topHwnd || !e.logicalRect.contains(globalPos))
            continue;
        hits.append(e);
    }
    if (hits.isEmpty()) {
        if (samePos)
            return m_lastChain;
        m_lastChainPos = globalPos;
        m_lastChain.clear();
        return {};
    }

    // 面积升序：最内层元素（面积最小）排最前
    std::stable_sort(hits.begin(), hits.end(),
                     [](const ElementInfo &a, const ElementInfo &b) {
                         const qreal aa = a.logicalRect.width() * a.logicalRect.height();
                         const qreal bb = b.logicalRect.width() * b.logicalRect.height();
                         if (!qFuzzyCompare(aa, bb))
                             return aa < bb;
                         return a.depth < b.depth;
                     });

    // 贪心包含链：从面积最小的命中往外扩，只接受"完整包含上一个"的元素。
    // 这是网格采样下唯一可靠的组链方式（同一点下钻的父子必然互相包含）；
    // 相比以前的"面积排序后随便接"，关键区别是**必须完整包含**才接，
    // 交叉/同面积兄弟不会被误连。
    QVector<ElementInfo> chain;
    for (const ElementInfo &e : hits) {
        if (chain.isEmpty()) {
            chain.append(e);
            continue;
        }
        const QRectF &last = chain.last().logicalRect;
        if (e.logicalRect.contains(last) && e.logicalRect != last)
            chain.append(e);
    }

    if (samePos) {
        // 单调收敛：新链的头必须是旧链头的"内部"，否则保持旧链。
        // 这样扫描批次陆续到达时高亮只会越来越准，不会乱跳。
        const QRectF oldHead = m_lastChain.first().logicalRect;
        const QRectF newHead = chain.first().logicalRect;
        const bool deeper = oldHead.contains(newHead)
                            && (newHead.width() < oldHead.width()
                                || newHead.height() < oldHead.height());
        if (!deeper)
            return m_lastChain;
    }

    m_lastChainPos = globalPos;
    m_lastChain = chain;
    return chain;
}

void SnipOverlay::requestElementDetection()
{
    const QPoint cursor = QCursor::pos();

    // 窗口层永远先同步显示：UIA 请求要 100~300ms 才回来，
    // 这段时间里用户必须已经看到"鼠标下是哪个窗口"，否则会以为检测失灵。
    const QRect win = windowRectAtCached(cursor);
    QRect localWin = win.isEmpty() ? QRect()
                                   : QRect(win.topLeft() - virtualTopLeft(), win.size())
                                         .intersected(rect());
    // 整屏矩形不当窗口级高亮（与 applyElementLevel 同规则）：否则进截图
    // 先闪一下"整屏亮"再缩到元素。最大化窗口、全屏悬浮层都会触发。
    if (!localWin.isEmpty() && localWin.width() >= width() - 2
        && localWin.height() >= height() - 2)
        localWin = QRect();
    // ---- 窗口模式：纯本地，零 UIA ----
    //
    // snow 的 HitTestMode::Window 直接从已缓存的窗口空间索引返回矩形：
    //     crate::HitTestMode::Window => (vec![window.bounds], StopReason::Complete)
    // 完全不碰 UIA，所以是毫秒级。我们以前窗口模式也走 requestElementDetection()
    // → UIA 查询 → 再取整窗，白等一次跨进程往返（100~300ms），
    // 表现为"窗口识别又慢又不准"。
    if (m_detectMode == DetectMode::Window) {
        if (!m_elementPending) {
            if (localWin != m_hoverWindow) {
                m_hoverWindow = localWin;
                hoverRepaint();
            }
        }
        return;
    }

    if (!m_elementPending && m_elementHierarchy.isEmpty() && localWin != m_hoverWindow) {
        m_hoverWindow = localWin;
        hoverRepaint();
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
    // ---- snow 式实时下钻（防抖）----
    //
    // 放弃"进入截图时全量抓树"：ControlView 会把 Chrome 这类应用的具体控件
    // 裁掉大半（实测 1015 → 200），快照里只剩大容器，必然"只识别到大框"。
    //
    // 改为鼠标**停下**后对光标点实时下钻一次（ElementFromHandle → 逐层取
    // 包含光标的最后一个兄弟 → 上溯成链）。移动过程中一个请求都不发，
    // 所以不会像"每帧实时请求"那样卡。
    // ---- snow 式实时下钻（流水线并发）----
    //
    // 不做防抖：防抖会让"快速划过"永远等不到结果（每移动一次就重置计时）。
    // 改为"有请求在飞就只记录最新位置；结果回来时若鼠标已移动，立刻用
    // 最新位置补发"。延迟 = 一次 UIA 往返（~50ms），且结果永远对应最新位置。
    m_pendingCursor = cursor;

    // 指针一动就取消未触发的精化（snow 的 cancelRefinement 同款）：
    // 精化很贵，只在指针停稳后做，否则会和前台查询抢 UIA。
    if (m_refineGate && m_refineGate->isActive()) {
        m_refineGate->stop();
        m_refining = false;
    }

    if (m_elementPending)
        return;
    // 同一帧内不重复发（16ms ≈ 一帧）
    if (m_elementThrottle.isValid() && m_elementThrottle.elapsed() < 16)
        return;
    m_elementThrottle.restart();

    m_elementRequestPos = cursor;
    m_elementProcess = WindowDetector::processNameAt(cursor);
    m_elementPending = true;

    // 必须把遮罩下面那个真实窗口的 HWND 传进去：
    // UIA 的 ElementFromPoint 会命中我们自己的全屏遮罩，不传窗口句柄
    // 就永远只能识别"整屏"，表现为元素检测完全失灵。
    const quintptr targetHwnd = topWindowHwndAtCached(cursor);
    m_elementRequestId =
        ElementDetector::instance()->request(cursor, m_elementProcess, targetHwnd);
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
        hoverRepaint();
        return;
    }

    // 窗口模式（snow 的 ScreenshotIntelligentSelectionTarget::Window）：
    // 永远锁在最外层窗口，滚轮不参与（snow 的 setIndex 里直接强制 maximumIndex）。
    // 元素模式才允许在 元素 → 父容器 → 窗口 之间滚动切换。
    // 以前两种模式都走同一套 level 逻辑，表现完全一样 —— 用户感觉
    // "元素和窗口识别没区别"就是这个原因。
    const bool windowMode = (m_detectMode == DetectMode::Window);
    const int requestedLevel = windowMode
                                   ? int(m_elementHierarchy.size() - 1)
                                   : qBound(0, level, int(m_elementHierarchy.size() - 1));
    m_elementLevel = requestedLevel;

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

    for (int level2 = requestedLevel; level2 < m_elementHierarchy.size(); ++level2) {
        QRect candidate = localOf(m_elementHierarchy.at(level2).logicalRect);
        if (candidate.isEmpty() || isFullScreen(candidate))
            continue;
        // 链顶端 = 整窗层：换成 DWM 可见边框，做到和 Snipaste 一样严丝合缝。
        // 只在基本覆盖整个窗口时才换，避免链被截断时把大面板误当成整窗。
        if (level2 == m_elementHierarchy.size() - 1) {
            const QRect win = windowRectAtCached(QCursor::pos());
            const QRect localWin = win.isEmpty() ? QRect() : localOf(QRectF(win));
            if (!localWin.isEmpty() && !isFullScreen(localWin)
                && coversMostOf(QRectF(candidate), QRectF(localWin)))
                candidate = localWin;
        }
        // 注意：这里**不改 m_elementLevel**，它保持用户滚到的位置。
        // 显示跳过的层只影响这一次渲染，不影响下一次滚轮的起点。
        m_hoverWindow = candidate;
        hoverRepaint();
        return;
    }

    // 整条 UIA 链都是整屏：退回顶层窗口矩形；窗口也是整屏就彻底不显示
    const QRect win = windowRectAtCached(QCursor::pos());
    const QRect localWin = win.isEmpty() ? QRect() : localOf(QRectF(win));
    m_hoverWindow = (localWin.isEmpty() || isFullScreen(localWin)) ? QRect() : localWin;
    hoverRepaint();
}

void SnipOverlay::changeElementLevel(int delta)
{
    // 窗口模式锁死最外层，滚轮不切层级（snow 同款：Window 模式下
    // setIndex 强制 maximumIndex）。只有元素模式才逐层切换。
    if (m_detectMode == DetectMode::Window)
        return;

    // snow 的滚轮处理是"重查 + 切换"同时做：先按当前光标位置重新命中，
    // 紧接着在新链上按 index+steps 切层级。它的命中是同步的，所以能一气呵成。
    //
    // 我们是异步 UIA 查询，没法在同一帧拿到新链。做法：
    //   1. 记下这次滚轮的**待切层数**（m_pendingLevelDelta）
    //   2. 若光标已移动，发起重查；结果回来重建链时再把待切层数应用上去
    //   3. 光标没动（链就是当前位置的）→ 直接切
    // 这样滚轮永远不会被吞掉，且切换总是基于最新位置的链。

    const QPoint cursor = QCursor::pos();
    const bool moved = (cursor - m_elementRequestPos).manhattanLength() >= 3;

    if (m_elementHierarchy.isEmpty() || moved) {
        // 累计待切层数：连续滚动多次，结果回来时一次切到位
        m_pendingLevelDelta += delta;
        if (!m_elementPending)
            requestElementDetection();
        return;
    }

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

    // 提示框是子控件，内容变化要显式同步一次（Tab 行显示当前模式）
    syncShortcutTips();
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
    // 有弹出菜单（右键菜单/工具按钮菜单）在跑时，放大镜/取色面板一律收起：
    // menu->exec() 期间事件循环还在转，任何重绘都会把面板重新拉起来，
    // 所以必须在**显示入口**判断，而不是只在弹菜单前 hide 一次。
    // 菜单关闭后下一次重绘自动恢复（用户要求"菜单关了再出现"）。
    if (QApplication::activePopupWidget()) {
        m_magnifierOn = false;
        if (m_magnifierPanel)
            m_magnifierPanel->hidePanel();
        return;
    }

    if (m_state == State::Dragging || m_state == State::Resizing || m_state == State::Moving) {
        m_magnifierOn = false;
        return;
    }

    // 鼠标移到工具栏上时隐藏放大镜与取色面板：
    // 工具栏是子控件，鼠标进入后 SnipOverlay 收不到 mouseMove，
    // 放大镜会停在最后一次的位置一直挂着（用户看到"图里还显示像素采集"）。
    if (m_paintBarVisible && m_paintBar
        && QRect(m_paintBar->mapTo(this, QPoint(0, 0)), m_paintBar->size())
               .contains(m_mouseLocal)) {
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
    // 长截图有拼接内容：保存/复制输出的是长图（与选区/标注无关）。
    // 预览栏上的琥珀色手柄可裁掉头/尾（固定导航栏等），这里应用裁剪。
    if (m_longShotState != LongShotState::Off && !m_stitcher.isEmpty()) {
        const QImage full = m_stitcher.canvas();
        const int top = qBound(0, m_longShotTrimTop, full.height());
        const int bottom = qBound(0, m_longShotTrimBottom, full.height() - top);
        const int keep = full.height() - top - bottom;
        if (top == 0 && bottom == 0)
            return full;
        return full.copy(0, top, full.width(), qMax(1, keep));
    }

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
    result.pinTargetWindow = m_pinTargetWindow;

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
    case SnipOutput::PinWindow:
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

// 钉住窗口：用户想"实时"看到某个窗口，而不是截一张静态图。
// 取选区中心下面的顶层窗口，把它交给 AppController 建成 Live 贴图。
void SnipOverlay::pinSelectedWindow()
{
    const QRect sel = localSelection();
    if (sel.isEmpty())
        return;

    // 选区中心在屏幕上的位置（遮罩自己会被 WindowDetector 排除）
    const QPoint globalCenter = mapToGlobal(sel.center());
    const WindowInfo target = WindowDetector::windowFromPoint(globalCenter);
    if (!target.hwnd) {
        showCenterNotice(QStringLiteral("没有找到可钉住的窗口"));
        return;
    }

    m_pinTargetWindow = target.hwnd;
    finishSnip(SnipOutput::PinWindow);
}

void SnipOverlay::releaseSessionImages()
{
    m_snapshot.pixmap = QPixmap();
    m_snapshot.cursorPixmap = QPixmap();
    m_snapshotImage = QImage();
    m_snapshotLayer = QPixmap();
    PixelShape::clearSourceImage();
    invalidateBackgroundCache();

    // 取色面板 / 放大镜是**独立顶层窗口**，不随截图层隐藏或关闭。
    // 会话一结束必须在这里收掉，否则它们会一直挂在桌面上，
    // 例如识别文字后仍能看到一个写着 "#000000" 的取色小窗。
    if (m_magnifierPanel)
        m_magnifierPanel->hidePanel();
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
        syncShortcutTips();   // 子控件：内容/位置/显隐
        return;
    }

    rebuildBackgroundCache();
    rebuildMaskedCache();
    // 只画脏区：Qt 已按 update(region) 限制可见区域，这里再显式 clip，
    // 让光栅引擎只处理脏矩形（拖拽时是几十像素级，不是整屏）。
    //
    // 注意：**不能**给 drawPixmap 传源矩形。pixmap 带 devicePixelRatio，
    // 它的源矩形是设备像素坐标，而这里拿到的是逻辑坐标——高 DPI 下
    // （1707 逻辑 / 2560 物理，dpr=1.5）只取到 1/1.5 的源区域再拉伸铺满，
    // 画面就会整体放大 1.5 倍。只设 clip、用整图绘制即自动换算。
    p.setClipRect(e->region().boundingRect());
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

    drawHoveredShapeFrame(p);    // 悬停候选框（snow 的 SelectionCandidateFrame）
    drawSelectionHandles(p);     // 选中框 + 锚点
    drawLassoPreview(p);
    drawAuxiliaryLines(p);
    drawSelectionFrame(p);
    drawAnchors(p);
    drawSizeLabel(p);
    drawMagnifier(p);
    drawHint(p);
    drawCenterNotice(p);
    syncShortcutTips();
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
// 只在选区静止时合成；拖拽中改走直绘快路径（见下）。
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

    // 选区正在被拖动（拖拽建区/锚点改大小/整块移动）：形状每帧都在变，
    // 合成缓存毫无复用价值，而"每帧新建整屏 pixmap + 拷贝 + 填充"正是
    // 拖拽卡顿的主因。清空缓存让 paintEvent 走"1:1 blit + 4 个 fillRect"
    // 直绘快路径；松手后下一帧自动重建缓存，静止阶段继续整块 blit。
    if (m_state == State::Dragging || m_state == State::Resizing
        || m_state == State::Moving) {
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

// ---------------------------------------------------------------- 高亮过渡动画
//
// 借鉴 Snow Shot 的 ScreenshotSmartSelectionTransition：
//   - 101ms 二次缓出（progress * (2 - progress)）—— 快、不拖泥带水
//   - 目标和当前几乎重合（0.4px 容差）时直接跳，避免微小抖动触发整段动画
//   - 中途目标变了就从"当前显示位置"重新起步，动线连续不跳帧
// 对齐 snow 的 SelectionTransitionAnimation：
//   Disabled(0) → 瞬间切换；Soft(32) → 101ms；Fast(72) → 67ms
int SnipOverlay::hoverAnimDurationMs() const
{
    const int mode = ConfigManager::instance()
                         ->value(QStringLiteral("snip/selection_transition_animation"), 32)
                         .toInt();
    switch (mode) {
    case 0:  return 0;     // 关闭
    case 72: return 67;    // 快速
    default: return 101;   // 柔和
    }
}

void SnipOverlay::tickHoverAnim()
{
    const QRect target = m_hoverWindow;
    const int durationMs = hoverAnimDurationMs();

    // 无高亮：直接清空（不做淡出，保持简单）
    if (target.isEmpty()) {
        if (!m_hoverAnimRect.isEmpty()) {
            m_hoverAnimRect = QRectF();
            hoverRepaint();
        }
        return;
    }

    // 关闭动画：瞬间切到目标，不做任何插值
    if (durationMs <= 0) {
        if (m_hoverAnimTimer)
            m_hoverAnimTimer->stop();
        m_animFrom = QRectF();
        m_hoverAnimClock.invalidate();
        if (m_hoverAnimRect != QRectF(target)) {
            m_hoverAnimRect = QRectF(target);
            hoverRepaint();
        }
        return;
    }

    // 首次显示 / 空起点：直接就位
    if (m_hoverAnimRect.isEmpty()) {
        m_hoverAnimRect = QRectF(target);
        hoverRepaint();
        return;
    }

    const QRectF targetF(target);
    if (m_hoverAnimRect == targetF)
        return;

    // 0.4px 容差内直接贴合（Snow Shot 的 snap tolerance）：
    // 鼠标小幅抖动时高亮不跟着抖
    const QRectF d = m_hoverAnimRect.normalized();
    const QRectF t = targetF.normalized();
    if (qAbs(d.left() - t.left()) < 0.5 && qAbs(d.top() - t.top()) < 0.5
        && qAbs(d.width() - t.width()) < 0.5 && qAbs(d.height() - t.height()) < 0.5) {
        m_hoverAnimRect = targetF;
        hoverRepaint();
        return;
    }

    if (!m_hoverAnimTimer) {
        m_hoverAnimTimer = new QTimer(this);
        m_hoverAnimTimer->setInterval(16);   // 60fps
        connect(m_hoverAnimTimer, &QTimer::timeout, this, [this]() {
            tickHoverAnim();
        });
    }

    if (!m_hoverAnimClock.isValid())
        m_hoverAnimClock.start();

    // 关键：必须启动定时器，否则动画只在"鼠标移动触发重绘"时被推进一帧，
    // 元素之间切换时框是**跳**过去的（用户感受到的"元素切换不流畅"）。
    // 之前这里只创建+connect，从未 start()，整个过渡动画等于没生效。
    if (!m_hoverAnimTimer->isActive())
        m_hoverAnimTimer->start();

    // 二次缓出：progress * (2 - progress)，起快收慢
    const qreal progress = qMin<qreal>(1.0, qreal(m_hoverAnimClock.elapsed()) / durationMs);
    const qreal eased = progress * (2.0 - progress);

    if (!m_animFrom.isValid()) {
        m_animFrom = m_hoverAnimRect;
        m_animTarget = targetF;
    } else if (m_animTarget != targetF) {
        // 中途改目标：从当前显示位置重新起步（动线连续）
        m_animFrom = m_hoverAnimRect;
        m_animTarget = targetF;
        m_hoverAnimClock.restart();
    }

    m_hoverAnimRect = QRectF(
        m_animFrom.x() + (m_animTarget.x() - m_animFrom.x()) * eased,
        m_animFrom.y() + (m_animTarget.y() - m_animFrom.y()) * eased,
        m_animFrom.width() + (m_animTarget.width() - m_animFrom.width()) * eased,
        m_animFrom.height() + (m_animTarget.height() - m_animFrom.height()) * eased);

    if (progress >= 1.0) {
        m_hoverAnimRect = targetF;
        m_animFrom = QRectF();
        m_hoverAnimClock.invalidate();
        if (m_hoverAnimTimer)
            m_hoverAnimTimer->stop();
    }

    hoverRepaint();
}

QRect SnipOverlay::hoverAnimRect() const
{
    return m_hoverAnimRect.normalized().toRect();
}

// ---------------------------------------------------------------- 重绘
//
// 曾经用"脏区局部重绘"省时间，但脏区只要漏算任何一块绘制内容
// （高亮框、尺寸标签、提示框、参考线、光标）就会留下残影。
// 现在拖拽/悬停阶段放大镜与锚点已停用，整屏重绘实测 4~5ms，
// 60fps 完全够，可靠性比省这几毫秒重要得多。
void SnipOverlay::hoverRepaint()
{
    m_lastHoverPaintRect = hoverAnimRect();
    m_lastMousePaintPos = m_mouseLocal;
    update();
}

// 窗口矩形查询带缓存：鼠标在同一个窗口内连续移动时，
// 直接复用上次结果，不再每帧查 WindowDetector（内部 200ms 一次
// EnumWindows + 逐窗口取标题，每次几毫秒，会造成周期性顿挫）。
quintptr SnipOverlay::topWindowHwndAtCached(const QPoint &globalPos)
{
    // 同一像素在短时间内重复查询（鼠标未移动、或一帧内多处调用）直接命中。
    // 200ms 与窗口矩形缓存的失效周期保持一致，两者不会互相矛盾。
    if (m_topWinCacheTimer.isValid() && m_topWinCacheTimer.elapsed() < 200
        && m_topWinCachePos == globalPos) {
        return m_topWinCacheHwnd;
    }

    m_topWinCacheHwnd = WindowDetector::windowFromPoint(globalPos).hwnd;
    m_topWinCachePos = globalPos;
    m_topWinCacheTimer.restart();
    return m_topWinCacheHwnd;
}

QRect SnipOverlay::windowRectAtCached(const QPoint &globalPos)
{
    // 缓存 key 必须包含**具体坐标**：以前只用"点是否落在上次的矩形内"判断，
    // 重叠窗口/大窗口内换位置时会一直复用旧结果，表现为"窗口识别不准"。
    // 同一像素重复查询（一帧内多处调用）仍然命中缓存，开销可忽略。
    if (!m_winRectCache.isNull() && m_winRectCacheTimer.isValid()
        && m_winRectCacheTimer.elapsed() < 2000
        && m_winRectCachePos == globalPos) {
        return m_winRectCache;
    }
    m_winRectCache = WindowDetector::bestWindowRectAt(globalPos);
    m_winRectCachePos = globalPos;
    m_winRectCacheTimer.restart();
    return m_winRectCache;
}

void SnipOverlay::drawWindowHighlight(QPainter &p)
{
    tickHoverAnim();   // 推进高亮过渡动画（静止时零开销）

    const QRect hover = hoverAnimRect();
    if (hover.isEmpty())
        return;

    // 鼠标已经不在检测区域内（例如停在工具栏/选区外沿）就别再闪一个框
    if (m_mouseInside && !hover.contains(m_mouseLocal)
        && m_state == State::Adjusting) {
        return;
    }

    // 选区已确定：只画实线轮廓，和手动选区完全一致（Snipaste 也是实线）。
    // 不再叠主色填充：那会让整片发蓝，用户会看成"截图直接蓝屏"。
    if (m_state == State::Adjusting) {
        p.save();
        p.setPen(QPen(Theme::kAccent, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(hover.adjusted(0, 0, -1, -1));
        p.restore();
        return;
    }

    // 未选区（Idle）：把检测到的窗口/元素从黑色遮罩里"挖"出来，
    // 和 Snipaste 一致 —— 其余区域保持暗，只有这一块是原始画面。
    p.save();
    p.setClipRect(hover);
    const qreal dpr = m_snapshot.devicePixelRatio;
    const QRectF source(hover.x() * dpr, hover.y() * dpr,
                        hover.width() * dpr, hover.height() * dpr);
    p.drawImage(QRectF(hover), m_snapshotImage, source);

    // 元素级高亮再叠一层极淡的主色（2~4%）：白色/浅色页面上"挖亮"
    // 本身看起来就像一片白，不加这层会以为是在选白块而不是在识别元素
    if (m_detectMode == DetectMode::Element && !m_elementHierarchy.isEmpty()
        && hover.width() < width() - 2) {
        p.fillRect(hover, QColor(0x3D, 0x8B, 0xFD, 12));
    }

    p.setClipping(false);
    // 边框 2px 主色 + 内侧 1px 白线：暗背景和亮背景上都能看清边界
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Theme::kAccent, 2));
    p.drawRect(hover.adjusted(0, 0, -1, -1));

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

    const QRect target = sizeLabelRect();
    if (target.isEmpty())
        return;

    // 平滑跟随：标签位置向目标缓动（每帧靠拢 35%），
    // 拖拽选区时标签像"黏"在框上，而不是每帧硬跳。
    // 尺寸变化（文字宽度）直接生效，只有位置做插值。
    if (m_sizeLabelAnimRect.isNull()) {
        m_sizeLabelAnimRect = QRectF(target);
    } else {
        const QPointF cur = m_sizeLabelAnimRect.topLeft();
        const QPointF goal = QPointF(target.topLeft());
        const QPointF next = cur + (goal - cur) * 0.35;
        if (QLineF(cur, goal).length() < 0.5) {
            m_sizeLabelAnimRect.moveTopLeft(goal);
        } else {
            m_sizeLabelAnimRect.moveTopLeft(next);
            // 还没到位：请求下一帧继续插值。拖拽时鼠标事件本来就在刷，
            // 松手后靠这一次 update() 把最后几像素补完。
            QTimer::singleShot(16, this, [this]() { update(); });
        }
        m_sizeLabelAnimRect.setSize(QSizeF(target.size()));
    }
    const QRect box = m_sizeLabelAnimRect.toRect();

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

void SnipOverlay::drawAnchors(QPainter &p)
{
    const QRect sel = localSelection();
    if (sel.isEmpty() || !m_showAnchors)
        return;

    // 拖拽过程中不画锚点：锚点会跟着每一帧移动，既遮挡选区内容又制造
    // "选区在抖"的观感，还让每帧多画 8 个方块。松手（Adjusting）后恢复。
    if (m_state == State::Dragging || m_state == State::Resizing)
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

void SnipOverlay::drawMagnifier(QPainter &p)
{
    Q_UNUSED(p);

    // 放大镜/取色合并面板（Snow 的 ScreenshotCanvasColorSamplerWindow 同款）：
    // 左侧 7×7 像素网格放大预览 + 右侧 HEX/RGB 文本，单面板跟随光标。
    // 选择了绘制工具时不显示：此时鼠标是用来落笔的，取色/放大是干扰。
    // 拖拽建选区/改大小/移动选区时也不显示：快速拖动时没人看取样点。
    // 弹出菜单期间同样收起（exec 的事件循环里重绘会把它拉起来）。
    if (!m_magnifierOn || m_snapshotImage.isNull()
        || m_longShotState != LongShotState::Off
        || QApplication::activePopupWidget()
        || m_state == State::Dragging || m_state == State::Resizing
        || m_state == State::Moving
        || (m_currentTool != Tool::None && m_paintBarVisible)) {
        if (m_magnifierPanel)
            m_magnifierPanel->hidePanel();
        return;
    }

    // 以光标物理像素为中心取 11×11（Snow 的 kPreviewPickerSize=11）。
    // 必须逐像素拼：靠近边缘时要钳制到图内，直接 copy 会得到空矩形。
    const qreal dpr = m_snapshot.devicePixelRatio;
    const int cx = qRound(m_mouseLocal.x() * dpr);
    const int cy = qRound(m_mouseLocal.y() * dpr);
    if (cx < 0 || cy < 0 || cx >= m_snapshotImage.width() || cy >= m_snapshotImage.height()) {
        if (m_magnifierPanel)
            m_magnifierPanel->hidePanel();
        return;
    }

    constexpr int kPreviewPixels = 11;
    const int half = kPreviewPixels / 2;
    const int maxX = m_snapshotImage.width() - 1;
    const int maxY = m_snapshotImage.height() - 1;
    QImage preview(kPreviewPixels, kPreviewPixels, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < kPreviewPixels; ++y) {
        for (int x = 0; x < kPreviewPixels; ++x) {
            const int sx = qBound(0, cx + x - half, maxX);
            const int sy = qBound(0, cy + y - half, maxY);
            preview.setPixel(x, y, m_snapshotImage.pixel(sx, sy));
        }
    }

    if (!m_magnifierPanel)
        m_magnifierPanel = new MagnifierPanel();

    MagnifierPanel::Data panelData;
    panelData.sample = preview;
    panelData.color = m_snapshotImage.pixelColor(cx, cy).toRgb();
    // 坐标：与截图层一致的全局逻辑坐标
    panelData.position = m_mouseLocal + virtualTopLeft();
    // 当前复制格式：Shift 切换时色块文字跟着变
    panelData.hexFormat = m_hexFormat;

    QRect avoid;
    if (m_paintBarVisible && m_paintBar)
        avoid = QRect(m_paintBar->mapToGlobal(QPoint(0, 0)), m_paintBar->size());

    m_magnifierPanel->showPanel(panelData, mapToGlobal(m_mouseLocal), avoid);
}

QRect SnipOverlay::shortcutTipsRect() const
{
    // 已拆成子控件（ShortcutTipsWidget），几何直接问它
    if (m_shortcutTips)
        return m_shortcutTips->geometry();
    return QRect();
}

// 同步左下角提示浮层（子控件，不再画在 paintEvent 里）。
//
// 每次 overlay 重绘/鼠标移动时调用：更新行内容 → 重新定位 → 按遮挡情况
// 显示/隐藏。因为它是子 QWidget，Qt 的 backing store 会自动处理
// "旧位置清除 + 新位置绘制"，**不需要把它的矩形算进脏区**，
// 也就不会再有残影。
void SnipOverlay::syncShortcutTips()
{
    if (!m_shortcutTips) {
        m_shortcutTips = new ShortcutTipsWidget(this);
        connect(m_shortcutTips, &QObject::destroyed, this,
                [this]() { m_shortcutTips = nullptr; });
    }

    const bool longShot = m_longShotState != LongShotState::Off;
    const bool freeMode = !longShot && m_detectMode == DetectMode::Free;

    QVector<ShortcutTipsWidget::Row> rows;
    if (longShot) {
        rows = {
            { QStringLiteral("滚轮"), QStringLiteral("滚动页面，继续拼接") },
            { QStringLiteral("悬停"), QStringLiteral("在预览栏上查看已截内容") },
            { QStringLiteral("Esc"),  QStringLiteral("退出长截图") },
        };
    } else {
        // Tab 那行带上当前模式：用户按 Tab 后能立刻确认切到了哪个模式
        const QString modeText = m_detectMode == DetectMode::Element
                                     ? QStringLiteral("元素识别")
                                     : m_detectMode == DetectMode::Window
                                           ? QStringLiteral("窗口识别")
                                           : QStringLiteral("自由识别");
        rows = {
            { QStringLiteral("W A S D"),   QStringLiteral("将鼠标指针移动 1 像素") },
            { QStringLiteral("Tab"),       QStringLiteral("切换检测模式（当前：%1）").arg(modeText) },
            { QStringLiteral("Alt+Click"), QStringLiteral("直接选中整个窗口") },
            { QStringLiteral("Ctrl + A"),  QStringLiteral("选区扩展到整个屏幕") },
            { QStringLiteral("R"),         QStringLiteral("使用上一次截图区域") },
            { QStringLiteral(", ."),       QStringLiteral("回溯截图历史") },
            { QStringLiteral("`"),         QStringLiteral("显示 / 隐藏鼠标指针") },
        };
        if (!freeMode) {
            rows.insert(2, { QStringLiteral("滚轮"),
                             QStringLiteral("切换元素层级") });
        }
    }
    m_shortcutTips->setRows(rows);
    if (m_shortcutTips->isContentEmpty()) {
        m_shortcutTips->hide();
        return;
    }

    // 左下角定位（边距 16）
    const QSize size = m_shortcutTips->size();
    m_shortcutTips->move(16, height() - size.height() - 16);

    // 可见性：只做整体显隐（snow 同款），任一条件命中就隐藏
    bool visible = true;
    if (m_hoveringTips)
        visible = false;                                  // 光标压在提示框上
    if (m_longShotState != LongShotState::Off) {
        if (m_shortcutTips->geometry().intersects(localSelection()))
            visible = false;                              // 长截图不能截进长图
    } else if (m_state != State::Idle || !localSelection().isEmpty() || m_refreshing) {
        visible = false;                                  // 已开始框选
    }
    // 检测到的高亮框压住提示框 → 隐藏（同 snow 的 selectionOverlaps）
    const QRect hover = hoverAnimRect();
    if (!hover.isEmpty() && hover.intersects(m_shortcutTips->geometry()))
        visible = false;

    m_shortcutTips->setObscured(!visible);
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
    if (m_shortcutTips && m_shortcutTips->isVisible())
        return;

    const QString modeText = m_detectMode == DetectMode::Element
                                 ? QStringLiteral("元素识别")
                                 : m_detectMode == DetectMode::Window
                                       ? QStringLiteral("窗口识别")
                                       : QStringLiteral("自由识别");
    QString text = QStringLiteral("拖动鼠标选择截图区域   |   右键或 Esc 取消   |   Tab 切换检测模式（当前：%1）")
                       .arg(modeText);
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
    // 右键菜单弹出前收掉取色面板/放大镜：它们是独立置顶小窗，
    // 会盖在菜单上、也挡住用户看菜单的视线（用户明确要求）。
    // 菜单关掉后移动鼠标，updateMagnifier/取色会自然恢复。
    if (m_magnifierPanel)
        m_magnifierPanel->hidePanel();

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

    // 菜单打开期间必须暂停置顶守卫：它每 300ms 会把遮罩 SetWindowPos 到
    // HWND_TOPMOST 最前，而菜单也是置顶窗——遮罩被推到菜单之上，
    // 菜单就被盖住（用户看到的"一闪而过"）。
    m_menuOpen = true;
    const QAction *chosen = menu->exec(globalPos);
    m_menuOpen = false;
    Q_UNUSED(chosen);
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

    // 右键落在已选中的标注上（由 mousePressEvent 判定并留下标记）：
    // 弹标注菜单。否则弹选区菜单。
    // 压在下面的标注被别的东西盖住时，只能靠"置于顶层"把它捞出来。
    if (m_annotationMenuPending) {
        m_annotationMenuPending = false;
        showAnnotationMenu(e->globalPos());
        e->accept();
        return;
    }

    showSnipMenu(e->globalPos());
    e->accept();
}

// 选中标注的右键菜单
void SnipOverlay::showAnnotationMenu(const QPoint &globalPos)
{
    if (m_selectedShapes.empty())
        return;

    AppMenu::Menu menu(this);

    QAction *front = AppMenu::addIconAction(menu.get(), ElaIconType::LayerGroup,
                                            QStringLiteral("置于顶层"));
    connect(front, &QAction::triggered, this, [this]() {
        for (const auto &shape : m_selectedShapes)
            m_model->bringToFront(shape);
        update();
    });

    QAction *back = AppMenu::addIconAction(menu.get(), ElaIconType::LayerGroup,
                                           QStringLiteral("置于底层"));
    connect(back, &QAction::triggered, this, [this]() {
        for (const auto &shape : m_selectedShapes)
            m_model->sendToBack(shape);
        update();
    });

    menu->addSeparator();

    const QString delText = m_selectedShapes.size() > 1
                                ? QStringLiteral("删除选中的 %1 个标注\tDel")
                                      .arg(m_selectedShapes.size())
                                : QStringLiteral("删除标注\tDel");
    QAction *remove = AppMenu::addIconAction(menu.get(), ElaIconType::Trash, delText);
    connect(remove, &QAction::triggered, this, [this]() {
        const auto shapes = m_selectedShapes;
        for (const auto &shape : shapes)
            m_model->removeShape(shape);
        clearSelectionSilently();
        m_hoveredShape.reset();
        update();
    });

    menu->exec(globalPos);
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

    // 置顶面板是独立窗口，paintEvent 在长截图分支提前 return，
    // 不会再走到隐藏逻辑 —— 必须在这里主动收掉，否则会一直挂在屏幕上
    if (m_magnifierPanel)
        m_magnifierPanel->hidePanel();

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
    // 右侧常驻预览栏：位于选区之外，不会被截进长图
    if (!m_longShotPreviewBar) {
        m_longShotPreviewBar = new LongShotPreviewBar(this);
        connect(m_longShotPreviewBar, &LongShotPreviewBar::trimChanged, this,
                [this](int topRows, int bottomRows) {
                    m_longShotTrimTop = topRows;
                    m_longShotTrimBottom = bottomRows;
                });
        // 悬停即预览（Snow Shot 同款）：选区天窗里显示长图对应区段；
        // 预览期间暂停抓帧/自动滚动（预览窗盖着捕获区，抓了就是脏帧），
        // 移开恢复
        connect(m_longShotPreviewBar, &LongShotPreviewBar::hoverRegionChanged, this,
                [this](const QRect &canvasRect) {
                    if (canvasRect.isEmpty() || m_stitcher.isEmpty()) {
                        hideLongShotHoverPreview();
                        return;
                    }
                    if (!m_longShotHoverPreview) {
                        m_longShotHoverPreview = new LongShotPreview();
                        WinUtil::applyToolWindowStyle(m_longShotHoverPreview->winId());
                        WinUtil::applyNoActivateStyle(m_longShotHoverPreview->winId());
                        WinUtil::setClickThrough(m_longShotHoverPreview->winId(), true);
                    }
                    if (m_longShotState == LongShotState::Capturing) {
                        m_longShotTimer->stop();
                        if (m_autoScrollTimer)
                            m_autoScrollTimer->stop();
                        m_longShotHoverPaused = true;
                    }
                    m_longShotHoverPreview->showRegion(m_stitcher.canvas(), canvasRect,
                                                       m_longShotRect);
                    // 预览窗是置顶不透明窗、几何=整个选区：选区太高放不下工具栏时
                    // 工具栏本来就压在选区上，不提回最上层就被预览窗整个盖住
                    // （"工具栏消失了、点不到保存"就是这么来的）
                    if (m_longShotBar)
                        m_longShotBar->raise();
                    if (m_longShotPreviewBar)
                        m_longShotPreviewBar->raise();
                });
    }
    m_longShotTrimTop = 0;
    m_longShotTrimBottom = 0;
    m_longShotHoverPaused = false;
    // 悬停亮带 = 捕获视口大小的矩形（画布像素）
    const qreal dpr = m_snapshot.devicePixelRatio > 0.0 ? m_snapshot.devicePixelRatio : 1.0;
    m_longShotPreviewBar->setViewportSize(
        QSize(qRound(m_longShotRect.width() * dpr), qRound(m_longShotRect.height() * dpr)));
    m_longShotPreviewBar->setHighlightRows(0, 0);
    m_longShotPreviewBar->positionAt(localSelection());

    m_longShotBar->positionNear(m_longShotRect);
    m_longShotBar->show();
    m_longShotBar->raise();
    WinUtil::applyToolWindowStyle(m_longShotBar->winId());
    WinUtil::applyNoActivateStyle(m_longShotBar->winId());

    // 输入放行 + 焦点让给下层窗口（滚轮默认发给焦点窗口）
    WinUtil::setClickThrough(winId(), true);
    WinUtil::activateWindowAt(QCursor::pos());

    // 焦点已交给下层页面，截图层收不到按键 —— Esc 退出改用全局键盘钩子
    if (!m_longShotEscHook) {
        m_longShotEscHook = new KeyboardHook(this);
        connect(m_longShotEscHook, &KeyboardHook::escapePressed, this,
                [this]() {
                    if (m_longShotState != LongShotState::Off)
                        cancelLongShot();
                });
    }
    m_longShotEscHook->install();

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
    grabLongShotFrame();          // 先抓一帧打底
    m_longShotTimer->stop();      // 自动滚动模式下由滚动定时器驱动抓帧
    m_autoScrollTimer->start();   // 自动向上滚动页面
    syncLongShotUi();
}

void SnipOverlay::pauseLongCapture()
{
    if (m_longShotState != LongShotState::Capturing)
        return;
    m_longShotTimer->stop();
    if (m_autoScrollTimer)
        m_autoScrollTimer->stop();
    m_longShotState = LongShotState::Paused;
    syncLongShotUi();

    if (!m_stitcher.isEmpty())
        OperationToast::longShotStopped(m_stitcher.acceptedFrames(), m_stitcher.height());
}

// 关闭：直接退出整个截图会话（不返回普通截图）
void SnipOverlay::cancelLongShot()
{
    if (m_longShotState == LongShotState::Off)
        return;
    m_longShotTimer->stop();
    if (m_autoScrollTimer)
        m_autoScrollTimer->stop();
    m_stitcher.reset();
    m_longShotState = LongShotState::Off;
    hideLongShotHoverPreview();
    WinUtil::setClickThrough(winId(), false);
    if (m_longShotEscHook)
        m_longShotEscHook->uninstall();
    if (m_longShotPreviewBar) {
        m_longShotPreviewBar->hide();
        m_longShotPreviewBar->clearImage();
    }
    if (m_longShotBar)
        m_longShotBar->hide();
    abortSnip();
}

// 收起悬停预览：天窗恢复透出实时页面；若因预览暂停了抓帧则恢复
void SnipOverlay::hideLongShotHoverPreview()
{
    if (m_longShotHoverPreview)
        m_longShotHoverPreview->hide();
    if (m_longShotHoverPaused) {
        m_longShotHoverPaused = false;
        // 恢复自动滚动（抓帧由滚动定时器驱动）
        if (m_longShotState == LongShotState::Capturing && m_autoScrollTimer)
            m_autoScrollTimer->start();
    }
}

void SnipOverlay::saveLongShot()
{
    if (m_longShotState == LongShotState::Off || m_stitcher.isEmpty())
        return;
    pauseLongCapture();
    hideLongShotHoverPreview();
    if (m_longShotBar)
        m_longShotBar->hide();
    // 保存对话框期间暂停全局 Esc 钩子：对话框里按 Esc 是"取消对话框"，
    // 不能让钩子抢走它把整个长截图会话干掉（表现为"无法保存"）
    if (m_longShotEscHook)
        m_longShotEscHook->uninstall();
    if (!finishSnip(SnipOutput::FileDialog)) {
        // 用户取消了保存对话框：把工具栏放回来继续长截图
        if (m_longShotState != LongShotState::Off && m_longShotEscHook)
            m_longShotEscHook->install();
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
    hideLongShotHoverPreview();
    if (m_longShotBar)
        m_longShotBar->hide();
    if (!finishSnip(SnipOutput::Copy) && m_longShotBar) {
        m_longShotBar->show();
        m_longShotBar->raise();
    }
}

// 钉：把长图贴到屏幕（和普通截图的 Ctrl+T 同一条输出链路）
void SnipOverlay::pinLongShot()
{
    if (m_longShotState == LongShotState::Off || m_stitcher.isEmpty())
        return;
    pauseLongCapture();
    hideLongShotHoverPreview();
    if (m_longShotBar)
        m_longShotBar->hide();
    if (!finishSnip(SnipOutput::Pin) && m_longShotBar) {
        m_longShotBar->show();
        m_longShotBar->raise();
    }
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
    // 悬停预览窗盖着捕获区：绝不能抓帧（会把预览拍进长图）
    if (m_longShotHoverPreview && m_longShotHoverPreview->isVisible())
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
    // 右侧预览栏同理：万一布局兜底让它压到了捕获区域，抓帧时也要藏起来
    bool hidePreview = false;
    if (m_longShotPreviewBar && m_longShotPreviewBar->isVisible()) {
        const QRect pvRect(m_longShotPreviewBar->mapToGlobal(QPoint(0, 0)),
                           m_longShotPreviewBar->size());
        if (pvRect.intersects(m_longShotRect)) {
            m_longShotPreviewBar->hide();
            hidePreview = true;
        }
    }
    if (hidePreview)
        hideBar = true;   // 借用同一拍延迟，等 DWM 合成完再抓

    // 藏完等一拍让 DWM 合成后再抓（没藏就下一拍抓，逻辑统一）。
    // 不能在这里 processEvents 等合成：那会重入定时器/事件处理器，
    // 抓帧、拼接、隐藏全部乱套——这就是之前"越滚越卡、时好时坏"的来源。
    QTimer::singleShot(hideBar ? 40 : 0, this, [this, hideBar, hidePreview]() {
        // 悬停预览窗盖着捕获区：不抓帧（拍进去就是脏帧），先把控件放回来
        if (m_longShotHoverPreview && m_longShotHoverPreview->isVisible()) {
            if (hideBar && m_longShotBar) {
                m_longShotBar->show();
                m_longShotBar->raise();
            }
            if (hidePreview && m_longShotPreviewBar) {
                m_longShotPreviewBar->show();
                m_longShotPreviewBar->raise();
            }
            m_longShotGrabbing = false;
            return;
        }
        const QImage frame = ScreenCapturer::captureRegion(m_longShotRect);
        if (hideBar && m_longShotBar) {
            m_longShotBar->show();
            m_longShotBar->raise();
        }
        if (hidePreview && m_longShotPreviewBar) {
            m_longShotPreviewBar->show();
            m_longShotPreviewBar->raise();
        }
        m_longShotGrabbing = false;

        if (m_longShotState != LongShotState::Capturing || frame.isNull())
            return;

        const int heightBefore = m_stitcher.height();
        if (!m_stitcher.feed(frame)) {
            // 画布到顶自动暂停；其余情况（没滚动/匹配不可靠）静默丢弃该帧
            if (m_stitcher.isFull()) {
                pauseLongCapture();
                m_longShotStatus = QStringLiteral("已达最大高度，已自动暂停");
                update();
            }
            return;
        }

        if (m_longShotPreviewBar) {
            m_longShotPreviewBar->setImage(m_stitcher.canvas());
            // 最近捕获段高亮：本次新增的区段（哪一端 = 滚动方向）
            const int h1 = m_stitcher.height();
            const int grown = h1 - heightBefore;
            if (grown > 0) {
                if (m_stitcher.lastGrewTop())
                    m_longShotPreviewBar->setHighlightRows(0, grown);
                else
                    m_longShotPreviewBar->setHighlightRows(h1 - grown, h1);
            }
        }

        m_longShotStatus = QStringLiteral("已拼接 %1 帧 · 高 %2 px · 匹配 %3%")
                               .arg(m_stitcher.acceptedFrames())
                               .arg(m_stitcher.height())
                               .arg(qRound(m_stitcher.lastConfidence() * 100));
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
        showCenterNotice(QStringLiteral("请先框选要识别的区域"));
        return;
    }

    if (!m_ocr)
        m_ocr = new OcrEngine(this);

    // 已有识别在飞：给可见反馈并拒绝，不再静默排队
    //（排队 + 结果过期丢弃 = 识别几次后就"点了没反应"）
    if (m_ocr->isBusy()) {
        showCenterNotice(QStringLiteral("正在识别中，请稍候"));
        return;
    }

    // 立刻给"识别中"反馈：一次识别要 2~4 次 Windows OCR（几秒），
    // 没有反馈时用户会以为没点上、连着狂点 → 任务排队 → 更久没反应。
    showCenterNotice(QStringLiteral("正在识别文字…"), 10000);

    // 先隐藏截图层，让用户看到"识别中"而不是一直卡在遮罩上
    if (m_paintBar)
        m_paintBar->hide();

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
        showCenterNotice(QStringLiteral("没有可用的 OCR 语言包"));
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

                          // 识别成功（snow 同款）：
                          //   截图层隐藏但**不结束会话**，识别窗口浮在最前；
                          //   关掉识别窗口后截图层恢复，用户可继续框选/操作。
                          //
                          // 隐藏而不是关闭的原因：截图层是全屏置顶窗，
                          // 留着它就会把识别窗口盖住（用户看到的"弹窗不显示、
                          // 关掉选区才出现"）；而且选区高亮压在桌面上也碍事。
                          hide();
                          if (m_paintBar)
                              m_paintBar->hide();
                          // 会话仍在，但快照可能很大，先释放；继续操作需要时
                          // 由 refreshSnapshot 重新抓（这里不主动重抓，避免闪）
                          releaseSessionImages();
                          if (m_topmostGuardTimer)
                              m_topmostGuardTimer->stop();

                          // 不能以截图层为父窗口：SnipOverlay 挂着写死暗色的
                          // overlayStyleSheet，会沿父子链把 Ela 的亮/暗配色盖掉。
                          auto *dialog = new OcrResultDialog(result, langs,
                                                             result.language, sourceImage,
                                                             nullptr);
                          dialog->setWindowFlag(Qt::WindowStaysOnTopHint, true);
                          // 重新识别：必须把结果接回对话框。
                          // 以前这里只发请求不接结果（首次的连接已在回调里主动断开），
                          // 结果被丢弃 → 对话框一直显示旧文本，用户以为"点了没反应"。
                          //
                          // 注意 OcrEngine 的归属：SnipOverlay 在本回调里已经 close()，
                          // m_ocr 是它的子对象随时可能析构。对话框独立存在（可能比
                          // 截图层活得久），必须让它自己持有一个引擎，否则重识别
                          // 会打到已销毁的 m_ocr 上。
                          auto *dialogOcr = new OcrEngine(dialog);
                          connect(dialog, &OcrResultDialog::retryRequested, dialog,
                                  [dialogOcr, sourceImage, dialog](const QString &lang) {
                                      dialog->setRetryBusy(true);
                                      dialogOcr->setLanguage(lang);

                                      // 一次重识别一组连接，完成后自行断开。
                                      // 父对象是 dialog：对话框关闭时 Qt 会清理，
                                      // 不会泄漏，也不会回调到悬空对象。
                                      auto *okConn = new QMetaObject::Connection;
                                      auto *errConn = new QMetaObject::Connection;
                                      *okConn = connect(
                                          dialogOcr, &OcrEngine::finished, dialog,
                                          [dialog, okConn, errConn, sourceImage](
                                              const WinOcr::Result &r) {
                                              disconnect(*okConn);
                                              disconnect(*errConn);
                                              delete okConn;
                                              delete errConn;
                                              dialog->setResult(r, sourceImage);
                                              QApplication::clipboard()->setText(r.text);
                                          });
                                      *errConn = connect(
                                          dialogOcr, &OcrEngine::failed, dialog,
                                          [dialog, okConn, errConn](const QString &) {
                                              disconnect(*okConn);
                                              disconnect(*errConn);
                                              delete okConn;
                                              delete errConn;
                                              dialog->setRetryBusy(false);
                                          });
                                      if (!dialogOcr->recognize(sourceImage)) {
                                          // 被拒（上一次还没跑完）：立刻恢复按钮，
                                          // 不能让"识别中"卡死在禁用态
                                          disconnect(*okConn);
                                          disconnect(*errConn);
                                          delete okConn;
                                          delete errConn;
                                          dialog->setRetryBusy(false);
                                      }
                                  });
                          dialog->setAttribute(Qt::WA_DeleteOnClose);
                          dialog->show();
                          dialog->raise();
                          dialog->activateWindow();

                          // 识别窗口关闭后结束截图会话（恢复桌面）。
                          // 用 abortSnip 而不是自己 emit+close：它还会清理
                          // 文字框/尺寸框/连续刷新定时器等残留状态。
                          connect(dialog, &QObject::destroyed, this,
                                  [this]() { abortSnip(); });

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

    if (!m_ocr->recognize(image)) {
        // 理论上不会走到（前面刚查过 isBusy）：兜底恢复界面
        disconnect(*okConn);
        disconnect(*errConn);
        delete okConn;
        delete errConn;
        restorePaintBarAfterAsync();
    }
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

    // 二维码：识别完就结束截图会话，只留结果弹窗。
    // 截图层是全屏置顶窗，不关掉的话会把弹窗整个盖住（表现为"点了没反应"）。
    // 和 OCR 成功路径保持一致：释放快照 → 隐藏工具栏 → close()。
    releaseSessionImages();
    if (m_paintBar)
        m_paintBar->hide();
    close();

    // 不能以截图层为父窗口：SnipOverlay 挂着写死暗色的 overlayStyleSheet，
    // 会沿父子链把 Ela 的亮/暗配色盖掉（字看不清）
    auto *dialog = new BarcodeResultDialog(items, nullptr);
    dialog->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
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
