#include "annotate/PaintBar.h"

#include "annotate/Palette.h"
#include "ui/AppMenu.h"
#include "ui/ElaColorPickerDialog.h"
#include "ui/ElaSliderFactory.h"
#include "ui/Theme.h"
#include "ui/ToolIcons.h"

#include <QContextMenuEvent>
#include <QCursor>
#include <QFileDialog>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSlider>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

namespace {

// 工具组：多个工具共用一个按钮（长按 / 右键展开），减少工具栏宽度。
// 每组第一个是默认成员。
struct ToolGroup
{
    Tool members[3];
    int  count;
};

const ToolGroup kToolGroups[] = {
    {{Tool::Rectangle, Tool::Ellipse,     Tool::None}, 2},   // 形状
    {{Tool::Line,      Tool::Arrow,       Tool::Polyline}, 3}, // 直线/箭头/折线
    {{Tool::Pencil,    Tool::Marker,      Tool::None}, 2},   // 手绘
    {{Tool::Text,      Tool::None,        Tool::None}, 1},
    {{Tool::Mosaic,    Tool::Blur,        Tool::None}, 2},   // 像素处理
    {{Tool::Eraser,    Tool::None,        Tool::None}, 1},   // 橡皮擦（独立）
    {{Tool::Number,    Tool::Magnify,     Tool::Lasso}, 3},  // 序号/放大镜/套索
};
constexpr int kToolGroupCount = int(sizeof(kToolGroups) / sizeof(kToolGroups[0]));

// 虚线样式按钮对应的 PenStyle，顺序与按钮组一致
constexpr Qt::PenStyle kPenStyles[] = {
    Qt::SolidLine, Qt::DashLine, Qt::DotLine, Qt::DashDotLine
};

// 箭头样式按钮的提示文字（与 LineShape::ArrowStyle 一一对应）
const char *kArrowStyleNames[] = {
    "单头", "双头", "锥形倒刺", "细长锥形倒刺", "深倒刺"
};

enum ActionId {
    ActUndo = 1,
    ActRedo,
    ActClear,
    ActCopy,
    ActSave,
    ActPin,
    ActSendTo,
    ActRecognize,   // 识别：OCR + 二维码/条码，二选一菜单
    ActClose,
    ActModeToggle = 100,
    ActColorCustom = 200
};

} // namespace

PaintBar::PaintBar(QWidget *parent)
    : QWidget(parent)
{
    // 作为截图层的子控件：不抢焦点、随截图层显示/销毁
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);

    Palette::instance()->load();
    ToolStyleStore::instance()->load();

    m_tool = Tool::None;
    // 每组默认成员
    for (int g = 0; g < kToolGroupCount; ++g)
        m_groupActive[g] = kToolGroups[g].members[0];

    rebuildLayout();
    resize(sizeHint());

    hide();
}

QColor PaintBar::currentColor() const
{
    return ToolStyleStore::instance()->style(m_tool).color;
}

int PaintBar::currentPenWidth() const
{
    return ToolStyleStore::instance()->style(m_tool).penWidth;
}

void PaintBar::setTool(Tool tool)
{
    m_tool = tool;
    syncColorHighlight();
    rebuildLayout();
    update();
    emit toolChanged(tool);
}

void PaintBar::toggleTool(Tool tool)
{
    if (m_tool == tool) {
        m_tool = Tool::None;
        rebuildLayout();
        update();
        emit toolCleared();
        return;
    }
    setTool(tool);
}

bool PaintBar::rectMode() const
{
    if (!ToolStyleStore::supportsRectMode(m_tool))
        return false;
    return ToolStyleStore::instance()->style(m_tool).rectMode;
}

void PaintBar::toggleRectMode()
{
    if (!ToolStyleStore::supportsRectMode(m_tool))
        return;

    const bool cur = rectMode();
    ToolStyleStore::instance()->setRectMode(m_tool, !cur);
    update();
}

void PaintBar::syncColorHighlight()
{
    if (m_tool == Tool::None) {
        m_activeColorIndex = -1;
        return;
    }

    const QColor cur = currentColor();
    const QVector<QColor> &colors = Palette::instance()->colors();
    int match = -1;
    for (int i = 0; i < colors.size(); ++i) {
        // 只比 RGB，忽略 alpha（马克笔的 alpha 是 100）
        if (colors.at(i).rgb() == cur.rgb()) {
            match = i;
            break;
        }
    }
    m_activeColorIndex = match;
}

void PaintBar::setCanUndoRedo(bool canUndo, bool canRedo)
{
    m_canUndo = canUndo;
    m_canRedo = canRedo;
    rebuildLayout();
    update();
}

bool PaintBar::colorAreaVisible() const
{
    return m_tool != Tool::None && ToolStyleStore::usesColor(m_tool);
}

bool PaintBar::penLevelRowVisible() const
{
    return m_tool != Tool::None && ToolStyleStore::usesPenWidth(m_tool)
           && m_tool != Tool::Text;
}

// 虚线样式：矩形/椭圆/线/箭头/折线/铅笔
bool PaintBar::penStyleVisible() const
{
    return ToolStyleStore::supportsPenStyle(m_tool);
}

// 箭头样式：仅箭头工具
bool PaintBar::arrowStyleVisible() const
{
    return m_tool == Tool::Arrow;
}

// ---------------------------------------------------------------- 布局

void PaintBar::rebuildLayout()
{
    // 按钮列表重建后旧索引全部失效，必须清零，
    // 否则 drawPenLevels / drawHoverTooltip 里按旧索引取按钮会越界崩溃
    m_buttons.clear();
    m_hoverIndex = -1;
    m_pressedIndex = -1;
    m_draggingSlider = false;

    // ---- 第一行：工具 + 动作按钮 ----
    const int rowY = kPadding;
    int x = kPadding;

    // 每组只占一个按钮：显示该组当前选中的工具
    for (int g = 0; g < kToolGroupCount; ++g) {
        Button b;
        b.kind = Button::Kind::Tool;
        b.group = g;
        b.tool = m_groupActive[g];
        b.rect = QRect(x, rowY, kButtonSize, kButtonSize);
        m_buttons.append(b);
        x += kButtonSize + kGap;
    }

    const int toolsRowWidth = x - kGap + kPadding;

    m_expandedGroup = activeToolGroup();
    m_memberRowHeight = 0;   // 成员条已并入第二行，不再单独占一行

    struct Act { int id; bool enabled; };
    // 最右一组固定为 关闭 / 钉子 / 保存 / 复制（从左到右，复制贴右缘）
    const Act acts[] = {
        {ActUndo, m_canUndo},
        {ActRedo, m_canRedo},
        {ActClear, true},
        {ActSendTo, true},
        {ActRecognize, true},
        {ActClose, true},
        {ActPin, true},
        {ActSave, true},
        {ActCopy, true},
    };
    int actWidth = 0;
    for (int i = 0; i < int(sizeof(acts) / sizeof(acts[0])); ++i)
        actWidth += kButtonSize + kGap;
    actWidth += kSeparatorGap;
    actWidth += kSeparatorGap - kGap;   // 关闭按钮左侧的分隔间距

    // ---- 行可见性 ----
    const bool hasProps = (m_tool != Tool::None);
    m_propRowVisible = hasProps;
    m_modeButtonVisible = ToolStyleStore::supportsRectMode(m_tool);
    m_sliderVisible = hasProps && ToolStyleStore::usesPenWidth(m_tool)
                      && m_tool != Tool::Magnify && m_tool != Tool::Lasso;
    m_penLevelRowVisible = penLevelRowVisible();
    m_penStyleVisible = penStyleVisible();
    m_arrowStyleVisible = arrowStyleVisible();

    // ---- 第二行宽度：色点 + 滑块 + 模式按钮 ----
    const bool showColors = colorAreaVisible();
    const int colorCount = Palette::instance()->count();

    int propWidth = kPadding;
    if (hasProps) {
        // 成员条宽度（与下面布局保持一致）
        int memberW = 0;
        const int eg = activeToolGroup();
        if (eg >= 0 && groupHasMembers(eg)) {
            int cnt = 0;
            for (int i = 0; i < kToolGroups[eg].count; ++i)
                if (kToolGroups[eg].members[i] != Tool::None)
                    ++cnt;
            memberW = cnt * (kMemberSize + kGap) + (kSeparatorGap - kGap);
        }
        propWidth += memberW;
        if (showColors && colorCount > 0)
            propWidth += colorCount * (kColorDotSize + kGap) + 18 + kGap + kSeparatorGap;
        if (m_penStyleVisible)
            propWidth += kPenStyleCount * (kStyleBtnSize + kGap) + (kSeparatorGap - kGap);
        if (m_arrowStyleVisible)
            propWidth += kArrowStyleCount * (kStyleBtnSize + kGap) + (kSeparatorGap - kGap);
        if (m_sliderVisible)
            propWidth += kSliderWidth + kSliderValueWidth + kGap + kSeparatorGap;
        if (m_modeButtonVisible)
            propWidth += kButtonSize + kGap + (kSeparatorGap - kGap);
        propWidth += kPadding - kGap;
    }

    // ---- 第三行宽度：5 档笔宽 + 当前像素值 ----
    int levelWidth = kPadding;
    if (m_penLevelRowVisible) {
        levelWidth += kPenLevels * (kPenDotSize + kGap) + 8 + 30 + kPadding;
    }

    const int totalWidth = qMax(qMax(toolsRowWidth + actWidth, propWidth), levelWidth);

    // 动作按钮（第一行最右）
    int ax = totalWidth - kPadding;
    for (int i = int(sizeof(acts) / sizeof(acts[0])) - 1; i >= 0; --i) {
        ax -= kButtonSize;
        Button b;
        b.kind = Button::Kind::Action;
        b.actionId = acts[i].id;
        b.enabled = acts[i].enabled;
        b.rect = QRect(ax, rowY, kButtonSize, kButtonSize);
        m_buttons.append(b);
        ax -= (acts[i].id == ActClose) ? kSeparatorGap : kGap;
    }

    if (hasProps) {
        int px = kPadding;
        const int propY = kPadding + kButtonSize + kRowGap;

        // 组成员条放在第二行最左侧（不再单独占一行）
        int memberBlockWidth = 0;
        if (m_expandedGroup >= 0 && groupHasMembers(m_expandedGroup)) {
            const ToolGroup &g = kToolGroups[m_expandedGroup];
            const int memberY = propY + (kButtonSize - kMemberSize) / 2;
            for (int i = 0; i < g.count; ++i) {
                if (g.members[i] == Tool::None)
                    continue;
                Button mb;
                mb.kind = Button::Kind::ToolMember;
                mb.group = m_expandedGroup;
                mb.tool = g.members[i];
                mb.rect = QRect(px, memberY, kMemberSize, kMemberSize);
                m_buttons.append(mb);
                px += kMemberSize + kGap;
            }
            memberBlockWidth = px - kPadding;
            px += kSeparatorGap - kGap;   // 成员条与颜色区之间留间隔
        }

        if (showColors && colorCount > 0) {
            const QVector<QColor> &colors = Palette::instance()->colors();
            for (int i = 0; i < colors.size(); ++i) {
                Button b;
                b.kind = Button::Kind::ColorDot;
                b.index = i;
                b.rect = QRect(px, propY + (kButtonSize - kColorDotSize) / 2,
                               kColorDotSize, kColorDotSize);
                m_buttons.append(b);
                px += kColorDotSize + kGap;
            }

            Button big;
            big.kind = Button::Kind::ColorDot;
            big.actionId = ActColorCustom;
            big.index = -1;
            big.rect = QRect(px, propY + (kButtonSize - 18) / 2, 18, 18);
            m_buttons.append(big);
            px += 18 + kGap + kSeparatorGap - kGap;
        }

        // 线条样式：实线 / 虚线 / 点线 / 点划线
        if (m_penStyleVisible) {
            for (int i = 0; i < kPenStyleCount; ++i) {
                Button b;
                b.kind = Button::Kind::PenStyle;
                b.index = i;
                b.rect = QRect(px, propY + (kButtonSize - kStyleBtnSize) / 2,
                               kStyleBtnSize, kStyleBtnSize);
                m_buttons.append(b);
                px += kStyleBtnSize + kGap;
            }
            px += kSeparatorGap - kGap;
        }

        // 箭头样式：单头 / 双头 / 细单 / 细双 / 大单
        if (m_arrowStyleVisible) {
            for (int i = 0; i < kArrowStyleCount; ++i) {
                Button b;
                b.kind = Button::Kind::ArrowStyle;
                b.index = i;
                b.rect = QRect(px, propY + (kButtonSize - kStyleBtnSize) / 2,
                               kStyleBtnSize, kStyleBtnSize);
                m_buttons.append(b);
                px += kStyleBtnSize + kGap;
            }
            px += kSeparatorGap - kGap;
        }

        if (m_sliderVisible) {
            px += kSeparatorGap - kGap;   // 与前面颜色/线型区拉开距离
            Button b;
            b.kind = Button::Kind::PenSlider;
            b.rect = QRect(px, propY + (kButtonSize - 16) / 2, kSliderWidth, 16);
            m_buttons.append(b);
            px += kSliderWidth + kSliderValueWidth + kGap;
        }

        if (m_modeButtonVisible) {
            px += kSeparatorGap - kGap;
            Button b;
            b.kind = Button::Kind::Action;
            b.actionId = ActModeToggle;
            b.enabled = true;
            b.rect = QRect(px, propY, kButtonSize, kButtonSize);
            m_buttons.append(b);
        }
    }

    // ---- 第三行：5 档笔宽 ----
    if (m_penLevelRowVisible) {
        int px = kPadding;
        const int levelY = kPadding + kButtonSize + kRowGap + m_memberRowHeight
                           + (m_propRowVisible ? kButtonSize + kRowGap : 0);
        for (int i = 0; i < kPenLevels; ++i) {
            Button b;
            b.kind = Button::Kind::PenLevel;
            b.index = i;
            b.rect = QRect(px, levelY + (kButtonSize - kPenDotSize) / 2,
                           kPenDotSize, kPenDotSize);
            m_buttons.append(b);
            px += kPenDotSize + kGap;
        }
    }

    const int rows = (m_propRowVisible ? 1 : 0) + (m_penLevelRowVisible ? 1 : 0);
    const int totalHeight = kPadding * 2 + kButtonSize + m_memberRowHeight
                            + rows * (kRowGap + kButtonSize)
                            + kTooltipArea;

    setFixedSize(totalWidth, totalHeight);
    syncPenSliderGeometry();
}

void PaintBar::positionNear(const QRect &selectionLocalRect)
{
    if (!parentWidget())
        return;

    const QSize hint = size();
    const QRect avail = parentWidget()->rect();
    const QRect sel = selectionLocalRect;

    constexpr int kSelGap = 8;

    int x = sel.center().x() - hint.width() / 2;
    x = qBound(avail.left() + 4, x, avail.right() - hint.width() - 4);

    int y = sel.bottom() + kSelGap;
    if (y + hint.height() <= avail.bottom() - 4) {
        move(x, y);
        return;
    }

    const int aboveY = sel.top() - hint.height() - kSelGap;
    if (aboveY >= avail.top() + 4) {
        move(x, aboveY);
        return;
    }

    y = avail.bottom() - hint.height() - 12;
    if (y < sel.top())
        y = sel.top() + 12;
    move(x, y);
}

// ---------------------------------------------------------------- 事件

int PaintBar::hitButton(const QPoint &pos) const
{
    for (int i = 0; i < m_buttons.size(); ++i) {
        if (m_buttons.at(i).rect.contains(pos))
            return i;
    }
    return -1;
}

void PaintBar::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;

    const QPoint pos = e->position().toPoint();
    const int idx = hitButton(pos);
    if (idx < 0)
        return;

    const Button &b = m_buttons.at(idx);

    if (b.kind == Button::Kind::PenSlider) {
        // 滑块是真实子控件，鼠标事件由它自己处理
        return;
    }

    // 色点：按下先记录位置，移动超过阈值才算拖拽排序（否则松手仍按单击处理）
    if (b.kind == Button::Kind::ColorDot && b.index >= 0) {
        m_colorDragFrom = b.index;
        m_colorDragTo = b.index;
        m_colorPressPos = pos;
        m_colorDragMoved = false;
    }

    m_pressedIndex = idx;
    update();
}

void PaintBar::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();

    if (m_draggingSlider) {
        updatePenSliderDrag(pos.x());
        update();
        return;
    }

    if (m_colorDragFrom >= 0) {
        if (!m_colorDragMoved) {
            if ((pos - m_colorPressPos).manhattanLength() < 5)
                return;              // 还没超过拖拽阈值
            m_colorDragMoved = true;
            m_draggingColor = true;
        }

        // 找鼠标落在哪个色点上
        int target = -1;
        for (int i = 0; i < m_buttons.size(); ++i) {
            const Button &b = m_buttons.at(i);
            if (b.kind == Button::Kind::ColorDot && b.index >= 0
                && b.rect.adjusted(-kGap, 0, kGap, 0).contains(pos)) {
                target = b.index;
                break;
            }
        }
        if (target >= 0 && target != m_colorDragTo) {
            m_colorDragTo = target;
            update();
        }
        return;
    }

    const int idx = hitButton(pos);
    if (idx != m_hoverIndex) {
        m_hoverIndex = idx;
        QToolTip::hideText();
        update();
    }
}

void PaintBar::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        m_pressedIndex = -1;
        m_draggingSlider = false;
        m_draggingColor = false;
        update();
        return;
    }

    if (m_draggingSlider) {
        m_draggingSlider = false;
        update();
        return;
    }

    if (m_colorDragFrom >= 0) {
        const bool moved = m_colorDragMoved;
        const int from = m_colorDragFrom;
        const int to = m_colorDragTo;
        m_draggingColor = false;
        m_colorDragMoved = false;
        m_colorDragFrom = -1;
        m_colorDragTo = -1;
        if (moved) {
            // 拖拽排序结束
            if (from >= 0 && to >= 0 && from != to)
                swapPaletteColors(from, to);
            m_pressedIndex = -1;
            update();
            return;
        }
        // 未移动 → 当作普通点击，继续往下走
    }

    const int idx = hitButton(e->position().toPoint());
    if (idx >= 0 && idx == m_pressedIndex) {
        const Button &b = m_buttons.at(idx);

        if (b.kind == Button::Kind::ToolMember) {
            setToolFromGroup(b.group, b.tool);
        } else if (b.kind == Button::Kind::Tool && b.tool != Tool::None) {
            toggleTool(b.tool);
        } else if (b.kind == Button::Kind::ColorDot) {
            if (b.actionId == ActColorCustom) {
                openColorDialog();
            } else if (b.index >= 0) {
                const QColor c = Palette::instance()->at(b.index);
                if (c.isValid()) {
                    m_activeColorIndex = b.index;
                    QColor withAlpha = c;
                    withAlpha.setAlpha(currentColor().alpha());
                    ToolStyleStore::instance()->setColor(m_tool, withAlpha);
                    emit colorChanged(withAlpha);
                    update();
                }
            }
        } else if (b.kind == Button::Kind::PenLevel) {
            ToolStyleStore::instance()->setPenLevel(m_tool, b.index);
            rebuildLayout();
            update();
            emit penWidthChanged(currentPenWidth());
        } else if (b.kind == Button::Kind::PenStyle) {
            ToolStyle ns = ToolStyleStore::instance()->style(m_tool);
            ns.penStyle = kPenStyles[b.index];
            ToolStyleStore::instance()->setStyle(m_tool, ns);
            emit styleChanged();
            update();
        } else if (b.kind == Button::Kind::ArrowStyle) {
            ToolStyle ns = ToolStyleStore::instance()->style(Tool::Arrow);
            ns.arrowStyle = b.index;
            ToolStyleStore::instance()->setStyle(Tool::Arrow, ns);
            emit styleChanged();
            update();
        } else if (b.kind == Button::Kind::Action && b.enabled) {
            switch (b.actionId) {
            case ActUndo:  emit undoRequested(); break;
            case ActRedo:  emit redoRequested(); break;
            case ActClear: emit clearRequested(); break;
            case ActCopy:  emit copyRequested(); break;
            case ActSave:  emit saveRequested(); break;
            case ActPin:   emit pinRequested(); break;
            case ActSendTo: emit sendToRequested(); break;
            case ActRecognize: showRecognizeMenu(QCursor::pos()); break;
            case ActClose: emit closeRequested(); break;
            case ActModeToggle: toggleRectMode(); break;
            default: break;
            }
        }
    }

    m_pressedIndex = -1;
    update();
}

void PaintBar::mouseDoubleClickEvent(QMouseEvent *e)
{
    // 双击不做任何弹窗动作。
    // 画图时连点两下档位/色点极易误触，弹出模态框会卡住整个截图会话；
    // 精确数值改由右键菜单显式触发（见 showPenLevelMenu / showPaletteContextMenu）。
    Q_UNUSED(e);
}

void PaintBar::contextMenuEvent(QContextMenuEvent *e)
{
    const int idx = hitButton(e->pos());
    if (idx < 0) {
        e->ignore();
        return;
    }

    const Button &b = m_buttons.at(idx);

    if (b.kind == Button::Kind::Tool && m_tool == Tool::None) {
        e->ignore();
        return;
    }

    if (b.kind == Button::Kind::Tool
        && (b.tool == Tool::Rectangle || b.tool == Tool::Ellipse))
        showShapeFillMenu(b.tool, e->globalPos());
    else if (b.kind == Button::Kind::Tool && b.tool == Tool::Arrow)
        showArrowStyleMenu(e->globalPos());
    else if (b.kind == Button::Kind::Tool && ToolStyleStore::supportsPenStyle(b.tool))
        showLineStyleMenu(e->globalPos());
    else if (b.kind == Button::Kind::Tool && b.tool == Tool::Number)
        showNumberGroupMenu(e->globalPos());
    else if (b.kind == Button::Kind::Tool && b.tool == Tool::Text)
        showTextStyleMenu(e->globalPos());
    else if (b.kind == Button::Kind::PenLevel)
        showPenLevelMenu(e->globalPos());
    else if (b.kind == Button::Kind::ColorDot)
        showPaletteContextMenu(e->globalPos());
    else
        e->ignore();
}

// 组内工具切换菜单（Ela 暗色/亮色自动跟随全局主题）
void PaintBar::showToolGroupMenu(int groupIndex, const QPoint &globalPos)
{
    if (groupIndex < 0 || groupIndex >= kToolGroupCount)
        return;

    const ToolGroup &g = kToolGroups[groupIndex];
    AppMenu::Menu menu(this);

    for (int i = 0; i < g.count; ++i) {
        const Tool t = g.members[i];
        if (t == Tool::None)
            continue;
        QAction *a = menu->addAction(ToolStyleStore::toolName(t));
        a->setCheckable(true);
        a->setChecked(m_groupActive[groupIndex] == t);
        connect(a, &QAction::triggered, this,
                [this, groupIndex, t]() { setToolFromGroup(groupIndex, t); });
    }

    menu->exec(globalPos);
}

int PaintBar::activeToolGroup() const
{
    if (m_tool == Tool::None)
        return -1;
    for (int g = 0; g < kToolGroupCount; ++g) {
        for (int i = 0; i < kToolGroups[g].count; ++i) {
            if (kToolGroups[g].members[i] == m_tool)
                return g;
        }
    }
    return -1;
}

bool PaintBar::groupHasMembers(int groupIndex) const
{
    return groupIndex >= 0 && groupIndex < kToolGroupCount
           && kToolGroups[groupIndex].count > 1;
}

void PaintBar::showRecognizeMenu(const QPoint &globalPos)
{
    AppMenu::Menu menu(this);
    QAction *ocr = menu->addAction(QStringLiteral("识别文字（OCR）"));
    connect(ocr, &QAction::triggered, this, [this]() { emit ocrRequested(); });

    QAction *code = menu->addAction(QStringLiteral("识别二维码 / 条码"));
    connect(code, &QAction::triggered, this, [this]() { emit barcodeRequested(); });

    menu->exec(globalPos);
}

void PaintBar::setToolFromGroup(int groupIndex, Tool tool)
{
    if (groupIndex < 0 || groupIndex >= kToolGroupCount)
        return;
    m_groupActive[groupIndex] = tool;
    setTool(tool);   // setTool 内部会重建布局并按新工具重画
}

void PaintBar::enterEvent(QEnterEvent *e)
{
    QWidget::enterEvent(e);
    emit pointerEntered();
}

void PaintBar::leaveEvent(QEvent *e)
{
    Q_UNUSED(e);
    m_hoverIndex = -1;
    update();
    emit pointerLeft();
}

void PaintBar::wheelEvent(QWheelEvent *e)
{
    const QPoint pos = e->position().toPoint();
    const int idx = hitButton(pos);

    if (idx >= 0) {
        const Button &b = m_buttons.at(idx);
        if (b.kind == Button::Kind::ColorDot && b.index >= 0) {
            applyWheelOnColorDot(e->angleDelta().y());
            e->accept();
            return;
        }
        if (b.kind == Button::Kind::PenLevel) {
            applyWheelOnPenLevel(b.index, e->angleDelta().y());
            e->accept();
            return;
        }
    }

    applyWheelPenWidth(e->angleDelta().y());
    e->accept();
}

void PaintBar::applyWheelPenWidth(int delta)
{
    if (m_tool == Tool::None)
        return;
    const int step = delta > 0 ? 1 : -1;
    int w = qBound(kPenMin, currentPenWidth() + step, kPenMax);
    ToolStyleStore::instance()->setPenWidth(m_tool, w);
    ToolStyleStore::instance()->setLevelPenWidth(m_tool,
                                                 ToolStyleStore::instance()->style(m_tool).penLevel, w);
    update();
    emit penWidthChanged(w);
}

void PaintBar::applyWheelOnColorDot(int delta)
{
    if (m_tool == Tool::None || m_activeColorIndex < 0)
        return;
    QColor c = currentColor();
    int a = qBound(0, c.alpha() + (delta > 0 ? 8 : -8), 255);
    c.setAlpha(a);
    ToolStyleStore::instance()->setColor(m_tool, c);
    m_alphaOverlayIndex = m_activeColorIndex;
    m_alphaOverlayValue = a;
    update();
    QTimer::singleShot(1000, this, [this]() {
        m_alphaOverlayIndex = -1;
        update();
    });
    emit colorChanged(c);
}

void PaintBar::applyWheelOnPenLevel(int level, int delta)
{
    // 悬停在档位圆点上滚轮 → 微调该档像素值
    if (level < 0 || level >= kPenLevels || m_tool == Tool::None)
        return;

    const ToolStyle s = ToolStyleStore::instance()->style(m_tool);
    const int px = qBound(kPenMin, s.penSizes.at(level) + (delta > 0 ? 1 : -1), kPenMax);
    ToolStyleStore::instance()->setLevelPenWidth(m_tool, level, px);
    if (s.penLevel == level)
        emit penWidthChanged(px);
    update();
}

int PaintBar::penWidthFromSliderX(int x) const
{
    for (const Button &b : m_buttons) {
        if (b.kind == Button::Kind::PenSlider) {
            const qreal t = qBound(0.0, qreal(x - b.rect.left()) / qMax(1, b.rect.width()), 1.0);
            return kPenMin + qRound(t * (kPenMax - kPenMin));
        }
    }
    return currentPenWidth();
}

void PaintBar::updatePenSliderDrag(int localX)
{
    setPenWidthDirect(penWidthFromSliderX(localX));
}

void PaintBar::setPenWidthDirect(int w)
{
    if (m_tool == Tool::None)
        return;
    w = qBound(kPenMin, w, kPenMax);
    ToolStyleStore::instance()->setPenWidth(m_tool, w);
    ToolStyleStore::instance()->setLevelPenWidth(m_tool,
                                                 ToolStyleStore::instance()->style(m_tool).penLevel, w);
    update();
    emit penWidthChanged(w);
}

// ---------------------------------------------------------------- 右键菜单

void PaintBar::showShapeFillMenu(Tool tool, const QPoint &pos)
{
    AppMenu::Menu menu(this);
    const ToolStyle style = ToolStyleStore::instance()->style(tool);
    const bool isRect = (tool == Tool::Rectangle);

    if (isRect) {
        QAction *corner = menu.get()->addAction(QStringLiteral("圆角半径…"));
        QObject::connect(corner, &QAction::triggered, this, [this]() {
            showCornerMenu(QPoint());
        });
        menu.get()->addSeparator();
    }

    QAction *hollow = menu.get()->addAction(QStringLiteral("空心（只有边框）"));
    hollow->setCheckable(true);
    hollow->setChecked(!style.filled);
    QAction *filled = menu.get()->addAction(QStringLiteral("实心（内部填充）"));
    filled->setCheckable(true);
    filled->setChecked(style.filled);

    QAction *chosen = menu.get()->exec(pos);
    if (!chosen)
        return;

    ToolStyle ns = ToolStyleStore::instance()->style(tool);
    ns.filled = (chosen == filled);
    ToolStyleStore::instance()->setStyle(tool, ns);
    update();
}

void PaintBar::showCornerMenu(const QPoint &pos)
{
    Q_UNUSED(pos);
    const ToolStyle s = ToolStyleStore::instance()->style(Tool::Rectangle);
    bool ok = false;
    const int r = QInputDialog::getInt(this, QStringLiteral("圆角矩形"),
                                       QStringLiteral("圆角半径（0~50）："),
                                       qRound(s.cornerRadius), 0, 50, 1, &ok);
    if (ok) {
        ToolStyle ns = ToolStyleStore::instance()->style(Tool::Rectangle);
        ns.cornerRadius = r;
        ToolStyleStore::instance()->setStyle(Tool::Rectangle, ns);
        update();
    }
}

void PaintBar::showArrowStyleMenu(const QPoint &pos)
{
    AppMenu::Menu menu(this);
    const ToolStyle s = ToolStyleStore::instance()->style(Tool::Arrow);

    struct Item { const char *name; int style; };
    const Item items[] = {
        { "单头箭头  →", 0 },
        { "双头箭头  ↔", 1 },
        { "锥形倒刺箭头", 2 },
        { "细长锥形倒刺箭头", 3 },
        { "深倒刺箭头", 4 }
    };

    for (const Item &it : items) {
        QAction *a = menu->addAction(QString::fromUtf8(it.name));
        a->setCheckable(true);
        a->setChecked(s.arrowStyle == it.style);
        connect(a, &QAction::triggered, this, [this, it]() {
            ToolStyle ns = ToolStyleStore::instance()->style(Tool::Arrow);
            ns.arrowStyle = it.style;
            ToolStyleStore::instance()->setStyle(Tool::Arrow, ns);
            update();
        });
    }

    menu->exec(pos);
}

void PaintBar::showLineStyleMenu(const QPoint &pos)
{
    AppMenu::Menu menu(this);
    const ToolStyle s = ToolStyleStore::instance()->style(m_tool);

    struct Item { const char *name; Qt::PenStyle style; };
    const Item items[] = {
        { "实线", Qt::SolidLine },
        { "虚线", Qt::DashLine },
        { "点线", Qt::DotLine },
        { "点划线", Qt::DashDotLine }
    };

    for (const Item &it : items) {
        QAction *a = menu->addAction(QString::fromUtf8(it.name));
        a->setCheckable(true);
        a->setChecked(s.penStyle == it.style);
        connect(a, &QAction::triggered, this, [this, it]() {
            ToolStyle ns = ToolStyleStore::instance()->style(m_tool);
            ns.penStyle = it.style;
            ToolStyleStore::instance()->setStyle(m_tool, ns);
            update();
        });
    }

    menu->addSeparator();
    QAction *ellipse = menu->addAction(QStringLiteral("椭圆笔头"));
    ellipse->setCheckable(true);
    ellipse->setChecked(s.ellipseBrush);
    ellipse->setEnabled(ToolStyleStore::supportsEllipseBrush(m_tool));
    connect(ellipse, &QAction::triggered, this, [this]() {
        ToolStyle ns = ToolStyleStore::instance()->style(m_tool);
        ns.ellipseBrush = !ns.ellipseBrush;
        ToolStyleStore::instance()->setStyle(m_tool, ns);
        update();
    });

    menu->exec(pos);
}

void PaintBar::showNumberGroupMenu(const QPoint &pos)
{
    AppMenu::Menu menu(this);
    const ToolStyle s = ToolStyleStore::instance()->style(Tool::Number);

    for (int g = 1; g <= 9; ++g) {
        QAction *a = menu->addAction(QStringLiteral("第 %1 组").arg(g));
        a->setCheckable(true);
        a->setChecked(s.numberGroup == g);
        connect(a, &QAction::triggered, this, [this, g]() {
            ToolStyle ns = ToolStyleStore::instance()->style(Tool::Number);
            ns.numberGroup = g;
            ToolStyleStore::instance()->setStyle(Tool::Number, ns);
            update();
        });
    }

    menu->addSeparator();
    QAction *reset = menu->addAction(QStringLiteral("重置本组序号  Ctrl+Shift+R"));
    connect(reset, &QAction::triggered, this, [this]() {
        const ToolStyle ns = ToolStyleStore::instance()->style(Tool::Number);
        emit resetNumberRequested(ns.numberGroup);
    });

    menu->exec(pos);
}

void PaintBar::showTextStyleMenu(const QPoint &pos)
{
    AppMenu::Menu menu(this);
    ToolStyle s = ToolStyleStore::instance()->style(Tool::Text);

    QAction *bg = menu->addAction(QStringLiteral("背景填充"));
    bg->setCheckable(true);
    bg->setChecked(s.textBg.alpha() > 0);
    connect(bg, &QAction::triggered, this, [this]() {
        ToolStyle ns = ToolStyleStore::instance()->style(Tool::Text);
        if (ns.textBg.alpha() > 0) {
            ns.textBg = Qt::transparent;
        } else {
            const QColor c = ElaColorPickerDialog::getColor(QColor(0, 0, 0, 160), this,
                                                          QStringLiteral("文字背景色"), true);
            if (!c.isValid())
                return;
            ns.textBg = c;
        }
        ToolStyleStore::instance()->setStyle(Tool::Text, ns);
        update();
    });

    QAction *outline = menu->addAction(QStringLiteral("文字描边"));
    outline->setCheckable(true);
    outline->setChecked(s.textOutline);
    connect(outline, &QAction::triggered, this, [this]() {
        ToolStyle ns = ToolStyleStore::instance()->style(Tool::Text);
        if (ns.textOutline) {
            ns.textOutline = false;
        } else {
            const QColor c = ElaColorPickerDialog::getColor(ns.textOutlineColor, this,
                                                          QStringLiteral("描边颜色"));
            if (!c.isValid())
                return;
            ns.textOutline = true;
            ns.textOutlineColor = c;
        }
        ToolStyleStore::instance()->setStyle(Tool::Text, ns);
        update();
    });

    menu->addSeparator();
    QAction *fontSize = menu->addAction(QStringLiteral("字号…"));
    connect(fontSize, &QAction::triggered, this, [this]() {
        ToolStyle ns = ToolStyleStore::instance()->style(Tool::Text);
        bool ok = false;
        const int px = QInputDialog::getInt(this, QStringLiteral("文字字号"),
                                            QStringLiteral("字号（6~400 px）："),
                                            ns.penWidth, 6, 400, 1, &ok);
        if (ok) {
            ns.penWidth = px;
            ToolStyleStore::instance()->setStyle(Tool::Text, ns);
            update();
        }
    });

    QAction *width = menu->addAction(QStringLiteral("描边宽度…"));
    width->setEnabled(s.textOutline);
    connect(width, &QAction::triggered, this, [this]() {
        ToolStyle ns = ToolStyleStore::instance()->style(Tool::Text);
        bool ok = false;
        const int w = QInputDialog::getInt(this, QStringLiteral("文字描边宽度"),
                                           QStringLiteral("宽度（1~10）："),
                                           ns.textOutlineWidth, 1, 10, 1, &ok);
        if (ok) {
            ns.textOutlineWidth = w;
            ToolStyleStore::instance()->setStyle(Tool::Text, ns);
            update();
        }
    });

    menu->exec(pos);
}

void PaintBar::showPaletteContextMenu(const QPoint &pos)
{
    AppMenu::Menu menu(this);

    QAction *edit = menu->addAction(QStringLiteral("编辑当前颜色…"));
    connect(edit, &QAction::triggered, this, [this]() { openColorDialog(true); });

    // 编辑鼠标指向的那个色点（双击编辑已移除，防误触）
    if (m_activeColorIndex >= 0) {
        QAction *editDot = menu->addAction(QStringLiteral("编辑色点 %1…").arg(m_activeColorIndex + 1));
        connect(editDot, &QAction::triggered, this, [this]() {
            const int idx = m_activeColorIndex;
            const QColor chosen = ElaColorPickerDialog::getColor(Palette::instance()->at(idx), this,
                                                               QStringLiteral("编辑颜色"), true);
            if (!chosen.isValid())
                return;
            Palette::instance()->setColorAt(idx, chosen);
            ToolStyleStore::instance()->setColor(m_tool, chosen);
            syncColorHighlight();
            emit colorChanged(chosen);
            update();
        });
    }

    QAction *add = menu->addAction(QStringLiteral("添加颜色…"));
    connect(add, &QAction::triggered, this, [this]() {
        const QColor c = ElaColorPickerDialog::getColor(Qt::white, this, QStringLiteral("添加颜色"), true);
        if (c.isValid()) {
            Palette::instance()->insertColor(Palette::instance()->count(), c);
            rebuildLayout();
            update();
        }
    });

    const bool canRemove = m_activeColorIndex >= 0 && Palette::instance()->count() > 1;
    QAction *remove = menu->addAction(QStringLiteral("删除当前颜色"));
    remove->setEnabled(canRemove);
    connect(remove, &QAction::triggered, this, [this]() {
        if (m_activeColorIndex < 0)
            return;
        Palette::instance()->removeColorAt(m_activeColorIndex);
        syncColorHighlight();
        rebuildLayout();
        update();
    });

    menu->addSeparator();

    QAction *importAct = menu->addAction(QStringLiteral("导入调色板…"));
    connect(importAct, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入调色板"),
                                                          QString(), QStringLiteral("调色板 (*.json)"));
        if (path.isEmpty())
            return;
        if (Palette::instance()->importFromFile(path)) {
            syncColorHighlight();
            rebuildLayout();
            update();
        }
    });

    QAction *exportAct = menu->addAction(QStringLiteral("导出调色板…"));
    connect(exportAct, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出调色板"),
                                                          QStringLiteral("palette.json"),
                                                          QStringLiteral("调色板 (*.json)"));
        if (!path.isEmpty())
            Palette::instance()->exportToFile(path);
    });

    QAction *reset = menu->addAction(QStringLiteral("恢复默认"));
    connect(reset, &QAction::triggered, this, [this]() {
        Palette::instance()->resetToDefault();
        syncColorHighlight();
        rebuildLayout();
        update();
    });

    menu->exec(pos);
}

void PaintBar::showPenLevelMenu(const QPoint &pos)
{
    AppMenu::Menu menu(this);
    const ToolStyle s = ToolStyleStore::instance()->style(m_tool);

    for (int i = 0; i < kPenLevels; ++i) {
        QAction *a = menu->addAction(QStringLiteral("档 %1：%2 px").arg(i + 1).arg(s.penSizes.at(i)));
        a->setCheckable(true);
        a->setChecked(s.penLevel == i);
        connect(a, &QAction::triggered, this, [this, i]() {
            ToolStyleStore::instance()->setPenLevel(m_tool, i);
            rebuildLayout();
            emit penWidthChanged(currentPenWidth());
            update();
        });
    }

    // 精确像素值只在这里改：双击档位会误触，已不再弹框
    menu->addSeparator();
    QAction *exact = menu->addAction(QStringLiteral("设定档 %1 的精确像素…").arg(s.penLevel + 1));
    connect(exact, &QAction::triggered, this, [this]() {
        ToolStyle ns = ToolStyleStore::instance()->style(m_tool);
        bool ok = false;
        const int px = QInputDialog::getInt(this, QStringLiteral("笔宽档位"),
                                            QStringLiteral("档 %1 的像素值（1~40）：")
                                                .arg(ns.penLevel + 1),
                                            ns.penSizes.at(ns.penLevel), 1, 40, 1, &ok);
        if (!ok)
            return;
        ToolStyleStore::instance()->setLevelPenWidth(m_tool, ns.penLevel, px);
        ToolStyleStore::instance()->setPenWidth(m_tool, px);
        rebuildLayout();
        emit penWidthChanged(px);
        update();
    });

    menu->exec(pos);
}

void PaintBar::swapPaletteColors(int from, int to)
{
    Palette::instance()->moveColor(from, to);
    syncColorHighlight();
    rebuildLayout();
    update();
}

void PaintBar::openColorDialog(bool editPaletteDot)
{
    const QColor initial = currentColor();
    const QColor chosen = ElaColorPickerDialog::getColor(
        initial, this, QStringLiteral("选择颜色"), true);
    if (!chosen.isValid())
        return;

    // 没有激活工具时（画完一个图形后会自动复位成 None），
    // 自定义颜色要写到所有会用到颜色的工具上，否则下一次画仍是旧色
    if (m_tool == Tool::None) {
        for (Tool t : {Tool::Rectangle, Tool::Ellipse, Tool::Line, Tool::Arrow,
                       Tool::Polyline, Tool::Pencil, Tool::Marker, Tool::Text,
                       Tool::Number}) {
            ToolStyleStore::instance()->setColor(t, chosen);
        }
    } else {
        ToolStyleStore::instance()->setColor(m_tool, chosen);
    }

    // 只有「编辑当前颜色」（右键入口）才回写调色板色点。
    // 点彩虹圆选自定义颜色时不动调色板：那只是给当前工具换个颜色，
    // 覆盖掉用户调色板里的颜色是错的。
    if (editPaletteDot && m_activeColorIndex >= 0
        && m_activeColorIndex < Palette::instance()->count()) {
        Palette::instance()->setColorAt(m_activeColorIndex, chosen);
    }
    if (editPaletteDot) {
        syncColorHighlight();
    } else {
        // 自定义颜色不对应任何色点
        m_activeColorIndex = -1;
        rebuildLayout();
    }
    emit colorChanged(chosen);
    update();
}

// ---------------------------------------------------------------- 绘制

// 在按钮内画一个迷你箭头，直观区分 5 种箭头样式：
//   0 单头 / 1 双头 = 等宽笔杆 + 实心三角
//   2 标准 / 3 细长 / 4 深倒刺 = 锥形倒刺闭合路径
void PaintBar::drawArrowStyleGlyph(QPainter &p, const QRect &r, int styleIndex,
                                   const QColor &color) const
{
    if (r.width() < 4 || r.height() < 4)
        return;

    const qreal y = r.center().y();
    const qreal x1 = r.left();
    const qreal x2 = r.right();

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);

    if (styleIndex >= 2) {
        // 锥形倒刺：缩略图里用固定比例画闭合路径（起点细 → 渐粗 → 倒刺 → 尖）
        qreal headF, barbF, shaftF, backF;
        if (styleIndex == 3) {        // 细长
            headF = 0.90; barbF = 0.30; shaftF = 0.10; backF = 0.06;
        } else if (styleIndex == 4) { // 深倒刺
            headF = 0.70; barbF = 0.34; shaftF = 0.12; backF = 0.04;
        } else {                      // 标准
            headF = 0.72; barbF = 0.32; shaftF = 0.13; backF = 0.07;
        }
        const qreal head = r.height() * headF;
        const qreal barb = r.height() * barbF;
        const qreal shaft = r.height() * shaftF;
        const qreal back = r.height() * backF;

        const qreal tipX = x2;
        const qreal barbX = tipX - head;
        const qreal shaftEndX = barbX + back;

        QPainterPath path;
        path.moveTo(x1, y - 0.4);
        path.lineTo(shaftEndX, y - shaft);
        path.lineTo(barbX, y - barb);
        path.lineTo(tipX, y);
        path.lineTo(barbX, y + barb);
        path.lineTo(shaftEndX, y + shaft);
        path.lineTo(x1, y + 0.4);
        path.closeSubpath();
        p.drawPath(path);
        p.restore();
        return;
    }

    // 旧档位：等宽笔杆 + 实心三角
    const bool twoHeads = (styleIndex == 1);
    const qreal head = r.height() * 0.44;
    const qreal halfW = head * 0.55;

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(QPointF(x1 + (twoHeads ? head : 0), y), QPointF(x2 - head, y));

    p.setPen(Qt::NoPen);
    p.setBrush(color);

    auto headPoly = [&](bool pointingRight, qreal baseX) {
        QPolygonF poly;
        if (pointingRight)
            poly << QPointF(baseX + head, y) << QPointF(baseX, y - halfW) << QPointF(baseX, y + halfW);
        else
            poly << QPointF(baseX - head, y) << QPointF(baseX, y - halfW) << QPointF(baseX, y + halfW);
        return poly;
    };

    p.drawPolygon(headPoly(true, x2 - head));
    if (twoHeads)
        p.drawPolygon(headPoly(false, x1 + head));

    p.restore();
}

void PaintBar::drawGroupCornerMark(QPainter &p, const QRect &r, bool active) const
{
    // 右下角 3px 小三角，颜色与图标一致（激活时白，否则浅灰）
    const QColor c = active ? QColor(Qt::white) : QColor(0xB0, 0xB0, 0xB0);
    const int s = 3;
    const QPointF br(r.right() - 3.5, r.bottom() - 3.5);

    QPolygonF tri;
    tri << QPointF(br.x() - s, br.y()) << br << QPointF(br.x(), br.y() - s);

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPolygon(tri);
    p.restore();
}

void PaintBar::ensurePenSlider()
{
    if (m_penSlider)
        return;

    m_penSlider = ElaSliderFactory::create(Qt::Horizontal, this);
    m_penSlider->setRange(kPenMin, kPenMax);
    m_penSlider->setSingleStep(1);
    m_penSlider->setPageStep(4);
    m_penSlider->setFocusPolicy(Qt::NoFocus);
    m_penSlider->setAttribute(Qt::WA_TranslucentBackground);

    connect(m_penSlider, &QSlider::valueChanged, this, [this](int v) {
        if (!m_penSlider || m_syncingSlider)
            return;
        setPenWidthDirect(v);
    });
}

// ElaSlider 是子控件，位置/宽度必须跟着布局走；不可见时隐藏
void PaintBar::syncPenSliderGeometry()
{
    if (!m_sliderVisible) {
        if (m_penSlider)
            m_penSlider->hide();
        return;
    }

    QRect sliderRect;
    for (const Button &b : m_buttons) {
        if (b.kind == Button::Kind::PenSlider) {
            sliderRect = b.rect;
            break;
        }
    }
    if (sliderRect.isEmpty()) {
        if (m_penSlider)
            m_penSlider->hide();
        return;
    }

    ensurePenSlider();
    // 严格使用布局矩形，不再向外扩展（之前 -kPadding 会压到左侧竖分隔线）。
    // 高度给足，Ela 滑块自绘轨道需要垂直空间。
    m_penSlider->setGeometry(sliderRect.adjusted(0, -5, 0, 5));

    if (m_penSlider->value() != currentPenWidth()) {
        m_syncingSlider = true;
        m_penSlider->setValue(currentPenWidth());
        m_syncingSlider = false;
    }
    m_penSlider->setEnabled(m_tool != Tool::None);
    m_penSlider->show();
    m_penSlider->raise();
}

void PaintBar::drawPenSlider(QPainter &p, const QRect &rect)
{
    // 滑块本体是 ElaSlider 子控件（见 syncPenSliderGeometry），
    // 这里只画右侧的数值文字，并和「3px」字号保持一致（pointSize 9）
    const bool active = (m_tool != Tool::None);

    QFont f = font();
    f.setPointSize(9);
    p.save();
    p.setFont(f);
    p.setPen(QColor(active ? 0x3A3A3A : 0xA0A0A0));
    p.drawText(QRect(rect.right() + 10, rect.top(), kSliderValueWidth - 10, rect.height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               active ? QStringLiteral("%1 px").arg(currentPenWidth())
                      : QStringLiteral("—"));
    p.restore();
}

void PaintBar::drawPenLevels(QPainter &p)
{
    if (!m_penLevelRowVisible)
        return;

    const ToolStyle s = ToolStyleStore::instance()->style(m_tool);

    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    for (const Button &b : m_buttons) {
        if (b.kind != Button::Kind::PenLevel)
            continue;

        const int px = s.penSizes.at(b.index);
        const bool active = (s.penLevel == b.index);

        if (active) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::kAccent);
            p.drawRoundedRect(b.rect.adjusted(-2, -2, 2, 2), 4, 4);
        } else if (m_hoverIndex >= 0 && m_hoverIndex < m_buttons.size()
                   && m_buttons.at(m_hoverIndex).kind == Button::Kind::PenLevel
                   && m_buttons.at(m_hoverIndex).index == b.index) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(kHoverLight));
            p.drawRoundedRect(b.rect.adjusted(-2, -2, 2, 2), 4, 4);
        }

        // 圆点大小按档位像素值缩放（1~40 → 2~14）
        const qreal dotR = qBound<qreal>(2.0, 1.5 + px * 0.3, 7.0);
        p.setPen(Qt::NoPen);
        p.setBrush(active ? QColor(Qt::white) : QColor(kIcon));
        p.drawEllipse(QPointF(b.rect.center()), dotR, dotR);
    }

    // 当前像素值
    const int valueX = kPadding + kPenLevels * (kPenDotSize + kGap) + 8;
    QFont f = font();
    f.setPointSize(7);
    p.setFont(f);
    p.setPen(QColor(0x3A3A3A));
    f.setPointSize(9);
    p.setFont(f);
    p.drawText(QRect(valueX, kPadding + kButtonSize + kRowGap + m_memberRowHeight
                                + (m_propRowVisible ? kButtonSize + kRowGap : 0),
                     44, kButtonSize),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1 px").arg(s.penWidth));

    p.restore();
}

QString PaintBar::hoverText() const
{
    if (m_hoverIndex < 0 || m_hoverIndex >= m_buttons.size())
        return {};

    const Button &b = m_buttons.at(m_hoverIndex);

    if (b.kind == Button::Kind::ToolMember)
        return ToolStyleStore::toolName(b.tool);

    if (b.kind == Button::Kind::Tool && b.tool != Tool::None) {
        const ToolStyle s = ToolStyleStore::instance()->style(b.tool);
        QString extra;
        if (b.tool == Tool::Number)
            extra = QStringLiteral("，第 %1 组").arg(s.numberGroup);
        return QStringLiteral("%1  笔宽 %2（右键更多）%3")
            .arg(ToolStyleStore::toolName(b.tool))
            .arg(s.penWidth)
            .arg(extra);
    }

    if (b.kind == Button::Kind::PenSlider)
        return QStringLiteral("拖动调整粗细（也可滚轮）");
    if (b.kind == Button::Kind::PenLevel)
        return QStringLiteral("笔宽档 %1：%2 px（滚轮微调，双击输入）")
            .arg(b.index + 1)
            .arg(ToolStyleStore::instance()->style(m_tool).penSizes.at(b.index));

    if (b.actionId == ActColorCustom)
        return QStringLiteral("自定义颜色（可选透明度）");
    if (b.kind == Button::Kind::ColorDot && b.index >= 0) {
        const QColor c = Palette::instance()->at(b.index);
        return QStringLiteral("颜色 %1（滚轮调透明度，长按拖动排序，双击编辑）")
            .arg(c.name(QColor::HexRgb));
    }

    if (b.kind == Button::Kind::PenStyle) {
        static const char *names[] = { "实线", "虚线", "点线", "点划线" };
        return QString::fromUtf8(names[b.index]);
    }
    if (b.kind == Button::Kind::ArrowStyle) {
        return QStringLiteral("箭头样式：%1").arg(QString::fromUtf8(kArrowStyleNames[b.index]));
    }

    switch (b.actionId) {
    case ActUndo: return QStringLiteral("撤销  Ctrl+Z");
    case ActRedo: return QStringLiteral("重做  Ctrl+Y");
    case ActClear: return QStringLiteral("清除所有标注  Ctrl+Shift+Z");
    case ActCopy: return QStringLiteral("复制到剪贴板  Enter");
    case ActSave: return QStringLiteral("保存到文件  Ctrl+S");
    case ActPin: return QStringLiteral("贴到屏幕  Ctrl+T");
    case ActSendTo: return QStringLiteral("发送到其他应用");
    case ActRecognize: return QStringLiteral("识别文字 / 二维码");
    case ActClose: return QStringLiteral("关闭  Esc");
    case ActModeToggle: return QStringLiteral("切换涂抹 / 矩形选区");
    default: break;
    }
    return {};
}

void PaintBar::drawHoverTooltip(QPainter &p)
{
    const QString text = hoverText();
    if (text.isEmpty() || m_hoverIndex < 0 || m_hoverIndex >= m_buttons.size())
        return;

    QFont f = font();
    f.setPointSize(8);
    p.setFont(f);

    const QFontMetrics fm(f);
    const int padH = 7, padV = 4;
    const int w = fm.horizontalAdvance(text) + padH * 2;
    const int h = fm.height() + padV * 2;

    int x = m_buttons.at(m_hoverIndex).rect.center().x() - w / 2;
    x = qBound(2, x, width() - w - 2);

    const int rows = (m_propRowVisible ? 1 : 0) + (m_penLevelRowVisible ? 1 : 0);
    const int y = kPadding * 2 + kButtonSize + m_memberRowHeight
                  + rows * (kRowGap + kButtonSize) + 2;
    const QRect box(x, y, w, h);

    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    p.setPen(QPen(QColor(kBarBorder), 1));
    p.setBrush(QColor(kBarBg));
    p.drawRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);

    p.setPen(QColor(kIcon));
    p.drawText(box, Qt::AlignCenter, text);

    p.restore();
}

void PaintBar::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 背景高度随可见行数变化（底部留白给悬停提示）
    const int rows = (m_propRowVisible ? 1 : 0) + (m_penLevelRowVisible ? 1 : 0);
    const int contentH = kPadding * 2 + kButtonSize + m_memberRowHeight
                         + rows * (kRowGap + kButtonSize);
    const QRectF bg = QRectF(0.5, 0.5, width() - 1.0, contentH - 1.0);
    p.setPen(QPen(QColor(kBarBorder), 1));
    p.setBrush(QColor(kBarBg));
    p.drawRoundedRect(bg, 6, 6);

    // 行分隔线
    auto drawRowSep = [&](int y) {
        p.setPen(QPen(QColor(kBarSep), 1));
        p.drawLine(QPoint(kPadding + 2, y), QPoint(width() - kPadding - 2, y));
    };
    if (m_propRowVisible)
        drawRowSep(kPadding + kButtonSize + kRowGap / 2 + m_memberRowHeight);
    if (m_penLevelRowVisible && m_propRowVisible)
        drawRowSep(kPadding + kButtonSize * 2 + kRowGap + kRowGap / 2 + m_memberRowHeight);

    // 第二行：颜色区与滑块之间的竖分隔线
    if (colorAreaVisible() && m_sliderVisible) {
        int sliderX = -1;
        for (const Button &b : m_buttons) {
            if (b.kind == Button::Kind::PenSlider) {
                sliderX = b.rect.left();
                break;
            }
        }
        if (sliderX > 0) {
            p.setPen(QPen(QColor(kBarSepStrong), 1));
            const int sx = sliderX - kSeparatorGap / 2;
            const int top = kPadding + kButtonSize + kRowGap + m_memberRowHeight;
            p.drawLine(QPoint(sx, top + 4), QPoint(sx, top + kButtonSize - 4));
        }
    }

    // 第一行：动作按钮组左侧、关闭按钮左侧的竖分隔线
    {
        int firstActionX = -1;
        int closeX = -1;
        for (const Button &b : m_buttons) {
            if (b.kind == Button::Kind::Action && b.rect.top() < kPadding + kButtonSize) {
                if (firstActionX < 0 || b.rect.left() < firstActionX)
                    firstActionX = b.rect.left();
                if (b.actionId == ActClose)
                    closeX = b.rect.left();
            }
        }
        if (firstActionX > 0 || closeX > 0)
            p.setPen(QPen(QColor(kBarSepStrong), 1));
        if (firstActionX > 0) {
            const int sx = firstActionX - kSeparatorGap / 2;
            p.drawLine(QPoint(sx, kPadding + 4), QPoint(sx, kPadding + kButtonSize - 4));
        }
        if (closeX > 0) {
            const int sx = closeX - kSeparatorGap / 2;
            p.drawLine(QPoint(sx, kPadding + 4), QPoint(sx, kPadding + kButtonSize - 4));
        }
    }

    // ---- 按钮 ----
    for (int i = 0; i < m_buttons.size(); ++i) {
        const Button &b = m_buttons.at(i);
        const bool hover = (i == m_hoverIndex);
        const bool pressed = (i == m_pressedIndex);

        if (b.kind == Button::Kind::Tool && b.tool != Tool::None) {
            if (m_tool == b.tool) {
                p.setPen(Qt::NoPen);
                p.setBrush(Theme::kAccent);
                p.drawRoundedRect(b.rect, 4, 4);
            } else if (pressed) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(kPressLight));
                p.drawRoundedRect(b.rect, 4, 4);
            } else if (hover) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(kHoverLight));
                p.drawRoundedRect(b.rect, 4, 4);
            }

            const QPixmap icon = ToolIcons::forTool(b.tool, 20,
                                                    m_tool == b.tool ? Qt::white
                                                                     : QColor(kIcon));
            p.drawPixmap(b.rect.center() - QPoint(qRound(icon.width() / icon.devicePixelRatio() / 2),
                                                  qRound(icon.height() / icon.devicePixelRatio() / 2)),
                         icon);

            // 多成员组：右下角小三角，提示可下拉切换
            if (b.group >= 0 && kToolGroups[b.group].count > 1)
                drawGroupCornerMark(p, b.rect, m_tool == b.tool);

            // 序号按钮角落显示组号
            if (b.tool == Tool::Number) {
                const ToolStyle s = ToolStyleStore::instance()->style(Tool::Number);
                QFont f = font();
                f.setPointSize(6);
                f.setBold(true);
                p.setFont(f);
                p.setPen(m_tool == b.tool ? QColor(Qt::white) : QColor(0x50, 0x50, 0x50));
                p.drawText(b.rect.adjusted(0, 0, -2, -2), Qt::AlignRight | Qt::AlignBottom,
                           QString::number(s.numberGroup));
            }
            continue;
        }

        if (b.kind == Button::Kind::ColorDot) {
            if (b.actionId == ActColorCustom) {
                // 自定义颜色：彩虹渐变圆
                QConicalGradient grad(b.rect.center(), 0);
                grad.setColorAt(0.0, QColor(0xFF, 0x00, 0x00));
                grad.setColorAt(0.25, QColor(0x00, 0xFF, 0x00));
                grad.setColorAt(0.5, QColor(0x00, 0x00, 0xFF));
                grad.setColorAt(0.75, QColor(0xFF, 0x00, 0xFF));
                grad.setColorAt(1.0, QColor(0xFF, 0x00, 0x00));
                p.setPen(QPen(QColor(0xFF, 0xFF, 0xFF), 1));
                p.setBrush(grad);
                p.drawEllipse(b.rect.adjusted(1, 1, -1, -1));
                continue;
            }

            const QColor c = Palette::instance()->at(b.index);
            const bool isActive = (m_activeColorIndex == b.index);

            // 选中色点：白底工具栏上画纯白环等于看不见，
            // 用「深色外环 + 白色内环」双环，浅底深底都清晰
            if (isActive) {
                p.setPen(QPen(QColor(0x2B2B2B), 1.5));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(b.rect.adjusted(-3, -3, 3, 3));
                p.setPen(QPen(Qt::white, 1.5));
                p.drawEllipse(b.rect.adjusted(-1, -1, 1, 1));
            }

            // 拖拽中的源色点收缩以提示
            QRect dotRect = b.rect.adjusted(1, 1, -1, -1);
            if (m_draggingColor && m_colorDragFrom == b.index)
                dotRect = dotRect.adjusted(3, 3, -3, -3);
            p.setPen(QPen(QColor(0xB0B0B0), 1));
            p.setBrush(c);
            p.drawEllipse(dotRect);

            // alpha 百分比浮层
            if (m_alphaOverlayIndex == b.index) {
                QFont f = font();
                f.setPointSize(7);
                p.setFont(f);
                const QString pct = QStringLiteral("%1%").arg(qRound(m_alphaOverlayValue / 255.0 * 100));
                p.setPen(QColor(kIcon));
                p.drawText(b.rect.adjusted(-14, -14, 14, 14), Qt::AlignCenter, pct);
            }
            continue;
        }

        if (b.kind == Button::Kind::PenStyle) {
            const ToolStyle st = ToolStyleStore::instance()->style(m_tool);
            const bool active = (st.penStyle == kPenStyles[b.index]);
            if (active) {
                p.setPen(Qt::NoPen);
                p.setBrush(Theme::kAccent);
                p.drawRoundedRect(b.rect, 4, 4);
            } else if (hover) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(kHoverLight));
                p.drawRoundedRect(b.rect, 4, 4);
            }
            const QPixmap sw = ToolIcons::penStyleSwatch(
                kPenStyles[b.index], b.rect.width() - 8, b.rect.height(),
                active ? QColor(Qt::white) : QColor(kIcon));
            p.drawPixmap(b.rect.center() - QPoint(qRound(sw.width() / sw.devicePixelRatio() / 2),
                                                  qRound(sw.height() / sw.devicePixelRatio() / 2)),
                         sw);
            continue;
        }

        if (b.kind == Button::Kind::ArrowStyle) {
            const ToolStyle st = ToolStyleStore::instance()->style(Tool::Arrow);
            const bool active = (st.arrowStyle == b.index);
            if (active) {
                p.setPen(Qt::NoPen);
                p.setBrush(Theme::kAccent);
                p.drawRoundedRect(b.rect, 4, 4);
            } else if (hover) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(kHoverLight));
                p.drawRoundedRect(b.rect, 4, 4);
            }
            drawArrowStyleGlyph(p, b.rect.adjusted(4, 7, -4, -7), b.index,
                                active ? QColor(Qt::white) : QColor(kIcon));
            continue;
        }

        if (b.kind == Button::Kind::PenSlider) {
            drawPenSlider(p, b.rect);
            continue;
        }

        if (b.kind == Button::Kind::PenLevel)
            continue;   // 第三行统一在 drawPenLevels 里画

        // 组成员条：当前组的各个工具，选中的高亮
        if (b.kind == Button::Kind::ToolMember) {
            if (m_tool == b.tool) {
                p.setPen(Qt::NoPen);
                p.setBrush(Theme::kAccent);
                p.drawRoundedRect(b.rect, 4, 4);
            } else if (hover) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(kHoverLight));
                p.drawRoundedRect(b.rect, 4, 4);
            }
            const QPixmap mi = ToolIcons::forTool(b.tool, 16,
                                                  m_tool == b.tool ? QColor(Qt::white)
                                                                   : QColor(kIcon));
            p.drawPixmap(b.rect.center() - QPoint(qRound(mi.width() / mi.devicePixelRatio() / 2),
                                                  qRound(mi.height() / mi.devicePixelRatio() / 2)),
                         mi);
            continue;
        }

        // 动作按钮
        if (pressed && b.enabled)
            p.fillRect(b.rect, QColor(kPressLight));
        else if (hover && b.enabled)
            p.fillRect(b.rect, QColor(kHoverLight));

        const QColor iconColor = b.enabled ? QColor(kIcon) : QColor(kIconDisabled);

        QPixmap icon;
        switch (b.actionId) {
        case ActModeToggle: {
            const bool isRect = rectMode();
            if (isRect) {
                p.setPen(Qt::NoPen);
                p.setBrush(Theme::kAccent);
                p.drawRoundedRect(b.rect, 4, 4);
            }
            icon = ToolIcons::forTool(isRect ? Tool::Rectangle : Tool::Pencil, 20,
                                      isRect ? QColor(Qt::white) : iconColor);
            break;
        }
        case ActUndo:  icon = ToolIcons::undoIcon(20, iconColor); break;
        case ActRedo:  icon = ToolIcons::redoIcon(20, iconColor); break;
        case ActClear: icon = ToolIcons::clearIcon(20, iconColor); break;
        case ActCopy:  icon = ToolIcons::copyIcon(20, iconColor); break;
        case ActSave:  icon = ToolIcons::saveIcon(20, iconColor); break;
        case ActPin:   icon = ToolIcons::pinIcon(20, iconColor); break;
        case ActSendTo: icon = ToolIcons::sendIcon(20, iconColor); break;
        case ActRecognize: icon = ToolIcons::ocrIcon(20, iconColor); break;
        case ActClose: icon = ToolIcons::closeIcon(20, iconColor); break;
        default: break;
        }
        if (!icon.isNull())
            p.drawPixmap(b.rect.center() - QPoint(qRound(icon.width() / icon.devicePixelRatio() / 2),
                                                  qRound(icon.height() / icon.devicePixelRatio() / 2)),
                         icon);
    }

    drawPenLevels(p);
    drawHoverTooltip(p);
}
