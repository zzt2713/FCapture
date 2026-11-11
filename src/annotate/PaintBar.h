#pragma once

#include "annotate/AnnotationTool.h"

#include <QPoint>
#include <QPointer>
#include <QVector>
#include <QWidget>

class QSlider;

class Palette;

class PaintBar : public QWidget
{
    Q_OBJECT

public:
    explicit PaintBar(QWidget *parent = nullptr);

    Tool currentTool() const { return m_tool; }
    QColor currentColor() const;
    int currentPenWidth() const;

    void setTool(Tool tool);
    // 再次点击同一工具 = 取消该工具（回到"无工具"，可拖动选区）
    void toggleTool(Tool tool);
    void setCanUndoRedo(bool canUndo, bool canRedo);

    // 根据当前工具的颜色刷新调色板高亮（切换工具时调用）
    void syncColorHighlight();

    // 马赛克/模糊/橡皮擦的"涂抹 ↔ 矩形"模式切换
    bool rectMode() const;
    void toggleRectMode();

    // 笔宽滑块（拖拽调整粗细）
    void setPenWidthDirect(int w);

    // 在选区下方/上方自动定位，不超出屏幕
    void positionNear(const QRect &selectionGlobalRect);

signals:
    void toolChanged(Tool tool);
    void toolCleared();                        // 再次点击同一工具触发
    void colorChanged(const QColor &color);
    void penWidthChanged(int width);
    void undoRequested();
    void redoRequested();
    void clearRequested();
    void copyRequested();
    void saveRequested();
    void pinRequested();
    void sendToRequested();
    void ocrRequested();
    void barcodeRequested();
    void closeRequested();
    void resetNumberRequested(int groupId);
    // 线型/箭头样式变更（工具栏改虚线、箭头样式时发出，
    // 让 SnipOverlay 把它同步到已选中的标注上）
    void styleChanged();
    // 鼠标进入/离开工具栏：SnipOverlay 据此隐藏放大镜与取色面板。
    // 工具栏是子控件，鼠标进入后父窗口收不到 mouseMove，
    // 放大镜会一直挂在最后一次的位置（用户看到"图里还显示像素采集"）。
    void pointerEntered();
    void pointerLeft();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    // 鼠标悬停提示文字（自绘，不依赖系统 QToolTip）
    QString hoverText() const;
    void drawHoverTooltip(QPainter &p);

    struct Button
    {
        enum class Kind { Tool, ToolMember, Action, PenSlider, ColorDot, PenLevel, PenStyle, ArrowStyle } kind = Kind::Tool;
        Tool    tool = Tool::None;
        QRect   rect;
        bool    enabled = true;
        int     actionId = 0;
        int     index = -1;      // 色点索引 / 笔宽档位索引
        int     group = -1;      // 工具组索引（-1 = 独立工具）；组内可下拉切换
    };

    // 笔宽滑块 → 粗细值
    int  penWidthFromSliderX(int x) const;
    void updatePenSliderDrag(int localX);
    // 粗细滑块：真正的 ElaSlider 子控件（自绘滑块已废弃）
    void ensurePenSlider();
    void syncPenSliderGeometry();
    void drawPenSlider(QPainter &p, const QRect &rect);
    QPointer<QSlider> m_penSlider;
    bool m_syncingSlider = false;
    void drawPenLevels(QPainter &p);
    void drawArrowStyleGlyph(QPainter &p, const QRect &r, int styleIndex,
                             const QColor &color) const;

    QRect toolButtonRect(int toolIndex) const;
    void  rebuildLayout();
    int   hitButton(const QPoint &pos) const;
    // editPaletteDot = true 时（右键「编辑当前颜色」）才回写调色板色点；
    // 点彩虹圆选自定义颜色只改当前工具色，不动调色板
    void  openColorDialog(bool editPaletteDot = false);
    void  applyWheelPenWidth(int delta);
    void  applyWheelOnColorDot(int delta);
    void  applyWheelOnPenLevel(int level, int delta);

    // 右键菜单：矩形圆角 / 虚线 / 箭头样式 / 文字背景描边 / 序号分组 / 调色板
    void showPaletteContextMenu(const QPoint &pos);
    void showArrowStyleMenu(const QPoint &pos);
    void showLineStyleMenu(const QPoint &pos);
    void showEllipseBrushMenu(const QPoint &pos);
    void showCornerMenu(const QPoint &pos);
    // 矩形 / 椭圆的右键菜单：圆角（仅矩形）+ 空心/实心
    void showShapeFillMenu(Tool tool, const QPoint &pos);
    void showNumberGroupMenu(const QPoint &pos);
    void showTextStyleMenu(const QPoint &pos);
    void showPenLevelMenu(const QPoint &pos);
    void swapPaletteColors(int from, int to);

    // 当前工具是否显示色点、笔宽、档位行
    bool colorAreaVisible() const;
    bool penLevelRowVisible() const;
    bool penStyleVisible() const;
    bool arrowStyleVisible() const;

    // 工具组：组内成员共享一个按钮，点按钮用 m_groupActive[组]，长按/右键展开菜单。
    // 目标是收窄工具栏（14 个工具 → 8 个按钮），和 Snipaste 一致。
    void showToolGroupMenu(int groupIndex, const QPoint &globalPos);
    void setToolFromGroup(int groupIndex, Tool tool);
    // 识别按钮：OCR 与二维码/条码共用一个入口，点开二选一
    void showRecognizeMenu(const QPoint &globalPos);
    // 多成员组按钮右下角画一个小三角，提示"这里可以下拉切换"
    void drawGroupCornerMark(QPainter &p, const QRect &r, bool active) const;
    // 当前工具所属的组（-1 = 独立工具/无工具）
    int activeToolGroup() const;
    bool groupHasMembers(int groupIndex) const;

    Tool m_tool = Tool::None;
    // 每个工具组当前选中的工具（由 kToolGroups 初始化）
    Tool m_groupActive[8] = {};
    // 当前展开成员条的组（工具栏第二行显示该组成员）
    int  m_expandedGroup = -1;
    int  m_memberRowHeight = 0;
    QVector<Button> m_buttons;

    int  m_hoverIndex = -1;
    int  m_pressedIndex = -1;
    bool m_draggingSlider = false;
    bool m_draggingColor = false;
    bool m_colorDragMoved = false;
    QPoint m_colorPressPos;
    int  m_colorDragFrom = -1;
    int  m_colorDragTo = -1;

    int  m_activeColorIndex = -1;      // -1 = 自定义色/无
    bool m_sliderVisible = false;
    bool m_modeButtonVisible = false;
    bool m_propRowVisible = false;
    bool m_penLevelRowVisible = false;
    bool m_penStyleVisible = false;
    bool m_arrowStyleVisible = false;
    int  m_alphaOverlayIndex = -1;     // 正在显示 alpha 百分比的色点
    int  m_alphaOverlayValue = 0;
    bool m_canUndo = false;
    bool m_canRedo = false;

    // 静态布局尺寸
    static constexpr int kPadding = 6;
    static constexpr int kButtonSize = 28;
    static constexpr int kGap = 2;
    static constexpr int kSeparatorGap = 9;
    static constexpr int kColorDotSize = 16;
    static constexpr int kTooltipArea = 24;
    static constexpr int kSliderWidth = 76;
    static constexpr int kSliderValueWidth = 40;   // 滑块右侧数值文字区宽度
    static constexpr int kRowGap = 2;
    static constexpr int kPenDotSize = 18;
    static constexpr int kStyleBtnSize = 26;
    static constexpr int kMemberSize = 22;   // 组成员条的小按钮
    static constexpr int kPenStyleCount = 4;
    static constexpr int kArrowStyleCount = 5;

    static constexpr int kPenMin = 1;
    static constexpr int kPenMax = 40;

    // 工具栏配色（浅色版：白底 + 黑图标）
    static constexpr QRgb kBarBg        = 0xF7F7F7;
    static constexpr QRgb kBarBorder    = 0xD8D8D8;
    static constexpr QRgb kBarSep       = 0xE4E4E4;
    static constexpr QRgb kBarSepStrong = 0xCFCFCF;
    static constexpr QRgb kIcon         = 0x2B2B2B;
    static constexpr QRgb kIconDisabled = 0xA8A8A8;
    static constexpr QRgb kHoverLight   = 0xE8E8E8;
    static constexpr QRgb kPressLight   = 0xDCDCDC;
};
