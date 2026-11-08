#pragma once

#include "annotate/AnnotationTool.h"

#include <QPoint>
#include <QVector>
#include <QWidget>

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

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    // 鼠标悬停提示文字（自绘，不依赖系统 QToolTip）
    QString hoverText() const;
    void drawHoverTooltip(QPainter &p);

    struct Button
    {
        enum class Kind { Tool, Action, PenSlider, ColorDot, PenLevel, PenStyle, ArrowStyle } kind = Kind::Tool;
        Tool    tool = Tool::None;
        QRect   rect;
        bool    enabled = true;
        int     actionId = 0;
        int     index = -1;      // 色点索引 / 笔宽档位索引
    };

    // 笔宽滑块 → 粗细值
    int  penWidthFromSliderX(int x) const;
    void updatePenSliderDrag(int localX);
    void drawPenSlider(QPainter &p, const QRect &rect);
    void drawPenLevels(QPainter &p);
    void drawArrowStyleGlyph(QPainter &p, const QRect &r, int styleIndex,
                             const QColor &color) const;

    QRect toolButtonRect(int toolIndex) const;
    void  rebuildLayout();
    int   hitButton(const QPoint &pos) const;
    void  openColorDialog();
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

    Tool m_tool = Tool::None;
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
    static constexpr int kToolCount = 14;
    static constexpr int kTooltipArea = 24;
    static constexpr int kSliderWidth = 76;
    static constexpr int kRowGap = 2;
    static constexpr int kPenDotSize = 18;
    static constexpr int kStyleBtnSize = 26;
    static constexpr int kPenStyleCount = 4;
    static constexpr int kArrowStyleCount = 5;

    static constexpr int kPenMin = 1;
    static constexpr int kPenMax = 40;
};
