#pragma once

#include <QImage>
#include <QWidget>

// 长截图右侧预览栏（照抄 Snow Shot 的 ScreenshotScrollingThumbnailWidget 交互）：
//
//   - 整张长图缩略显示并整体压暗，最近捕获段画亮
//   - 悬停处显示"捕获视口大小"的蓝色亮带 = 长图里的对应区段，
//     持续发 hoverRegionChanged，选区天窗里同步显示该区段（悬停即预览）
//   - 上下两个琥珀色手柄：拖动裁掉长图头/尾（截图常见的固定导航栏等）
//
// 贴在捕获区域右侧外面，不会被截进长图。
class LongShotPreviewBar : public QWidget
{
    Q_OBJECT

public:
    explicit LongShotPreviewBar(QWidget *parent = nullptr);

    void setImage(const QImage &canvas);
    void clearImage();
    void positionAt(const QRect &selectionLocalRect);

    // 捕获视口在画布坐标里的尺寸（悬停亮带 = 视口大小的矩形）
    void setViewportSize(const QSize &canvasSize)
    {
        m_viewportSize = canvasSize.expandedTo(QSize(1, 1));
    }

    // 最近捕获段（画布行范围 y0..y1；y0 >= y1 时不高亮）
    void setHighlightRows(int y0, int y1)
    {
        m_highlightTop = y0;
        m_highlightBottom = y1;
        update();
    }

    // 裁剪（相对画布两端的行数；0 = 未裁）
    int trimTopRows() const { return m_trimTop; }
    int trimBottomRows() const { return m_trimBottom; }

signals:
    // 悬停的长图区段（画布坐标；空 = 无悬停 / 正在拖裁剪手柄）
    void hoverRegionChanged(const QRect &canvasRect);
    // 裁剪范围变化（头/尾各裁掉多少行）
    void trimChanged(int topRows, int bottomRows);

protected:
    void paintEvent(QPaintEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    // 缩略图整体（含留白）与长图 → 栏坐标的换算
    QRect imageRect() const;
    qreal imageScale() const;
    int  yToImage(int widgetY) const;      // 栏 y → 画布 y
    int  imageToY(int imageY) const;       // 画布 y → 栏 y
    // 悬停亮带在画布坐标里的区段（视口大小，中心跟随光标并钳制在图内）
    QRect hoverCanvasRect(const QPoint &pos) const;
    void  clearHover();
    // 命中：上/下手柄（±9px，Snow Shot 的 kHandleHitRadius）
    enum class Hit { None, Head, Tail, Body };
    Hit hitAt(const QPoint &pos) const;

    QImage m_canvas;
    QSize  m_viewportSize{1, 1};    // 捕获视口（画布像素）
    int    m_trimTop = 0;           // 头部裁掉的行数
    int    m_trimBottom = 0;        // 尾部裁掉的行数
    bool   m_hoverActive = false;   // 悬停亮带是否激活
    QRect  m_hoverCanvasRect;       // 当前悬停亮带对应的画布区段
    int    m_highlightTop = 0;      // 最近捕获段（画布行，[top, bottom)）
    int    m_highlightBottom = 0;
    Hit    m_dragging = Hit::None;

    // 视觉参数（对齐 Snow Shot 的实现）
    static constexpr int kBarWidth = 128;
    static constexpr int kPadding = 8;
    static constexpr int kHandleHitRadius = 9;
    static constexpr int kHandleThickness = 4;
    static constexpr int kHandleStrokeWidth = 2;
    static constexpr int kHandleTabExtent = 30;
    static constexpr int kMinTrimmed = 20;   // 裁剪后至少保留的行数
};
