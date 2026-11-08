#pragma once

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QUuid>
#include <QWidget>

class QTimer;

// 贴图窗口：置顶的悬浮图片
class PinWindow : public QWidget
{
    Q_OBJECT

public:
    enum class BackgroundMode {
        Transparent,         // 透明区域不可点击
        PseudoTransparent,   // 透明区域显示为透明但可点击
        DarkChessboard,
        LightChessboard
    };

    // imageDpr：源图像的像素密度。截图（物理像素）传屏幕 dpr；
    // 普通剪贴板图片/文件图片传 1.0（按图片自身像素 1:1 显示）。
    explicit PinWindow(const QImage &image, const QString &sourceText = QString(),
                       qreal imageDpr = 1.0, QWidget *parent = nullptr);
    ~PinWindow() override;

    // --- 状态查询 ---
    QImage originalImage() const { return m_original; }
    QImage displayImage() const { return m_display; }
    QString sourceText() const { return m_sourceText; }
    QString group() const { return m_group; }
    bool isSelected() const { return m_selected; }
    bool isClickThrough() const { return m_clickThrough; }
    bool isTopmost() const { return m_topmost; }
    QRect contentRect() const;

    // --- 编辑操作 ---
    void setGroup(const QString &g) { m_group = g; }
    void setSelected(bool sel);
    void setClickThrough(bool on);
    void setTopmost(bool on);
    void setBackgroundMode(BackgroundMode mode);
    BackgroundMode backgroundMode() const { return m_bgMode; }

    void replaceImage(const QImage &img);

    // 一次性调整：缩放、旋转、镜像、灰度、反色
    void zoomBy(qreal factor);
    void setScale(qreal s);
    qreal scale() const { return m_scale; }
    void rotateCW();
    void rotateCCW();
    void flipHorizontal();
    void flipVertical();
    void toggleGrayscale();
    void toggleInverted();
    void resetTransform();

    void setOpacityValue(qreal o);
    qreal opacityValue() const { return m_opacity; }

    void enterCropMode();
    bool isCropping() const { return m_cropping; }
    void applyCrop();
    void cancelCrop();

    // 由 AppController 提供给状态栏等使用
    QPixmap snapshotForRecovery() const;

    // 虚拟桌面归属：切换桌面时只显示在创建它的桌面上
    void setDesktopId(const QUuid &id) { m_desktopId = id; }
    QUuid desktopId() const { return m_desktopId; }
    bool isOnCurrentDesktop() const;

signals:
    void requestClose(PinWindow *self);      // 关闭（可恢复）
    void requestDestroy(PinWindow *self);    // 销毁（不可恢复）
    void clicked(PinWindow *self, Qt::MouseButton button, Qt::KeyboardModifiers mods);
    void moved(PinWindow *self);
    void contextMenuRequested(PinWindow *self, const QPoint &globalPos);
    void dragged(PinWindow *self, const QPoint &delta);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void changeEvent(QEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    void applyTopmost();
    // 四角缩放
    enum class Corner { None, TopLeft, TopRight, BottomLeft, BottomRight };

    Corner cornerAt(const QPoint &pos) const;
    void beginResize(Corner c, const QPoint &globalPos);
    void updateResize(const QPoint &globalPos);
    QSize baseLogicalSize() const;      // 100% 缩放时的逻辑尺寸

    void rebuildDisplay();
    void updateWindowSize();
    QSize scaledContentSize() const;
    QRect imageTargetRect() const;
    void drawBackground(QPainter &p);
    void drawShadow(QPainter &p, bool active);
    void drawGlow(QPainter &p);
    void drawResizeHandles(QPainter &p);
    void drawCropOverlay(QPainter &p);
    void drawScaleHud(QPainter &p);
    void showScaleHud();
    QImage transformed(const QImage &src) const;

    QImage  m_original;       // 原始图（裁剪前）
    QImage  m_display;        // 变换后的显示图
    QPixmap m_pixmap;
    QString m_sourceText;
    // 源图像的像素密度。截图存的是物理像素，必须按屏幕 dpr 换算成逻辑尺寸，
    // 否则会在高分屏上被放大（150% 屏 → 看起来大 1.5 倍）。
    qreal   m_imageDpr = 1.0;

    qreal   m_scale = 1.0;
    qreal   m_opacity = 1.0;
    bool    m_scaleHudVisible = false;
    QTimer *m_scaleHudTimer = nullptr;
    int     m_rotation = 0;
    bool    m_flipH = false;
    bool    m_flipV = false;
    bool    m_grayscale = false;
    bool    m_inverted = false;

    bool    m_clickThrough = false;
    bool    m_topmost = true;
    bool    m_selected = false;
    bool    m_hovered = false;
    bool    m_active = false;      // 窗口是否为当前活动窗口（决定是否画发光）

    BackgroundMode m_bgMode = BackgroundMode::PseudoTransparent;

    // 拖动
    bool    m_dragging = false;
    QPoint  m_dragStartGlobal;
    QPoint  m_dragStartPos;

    // 四角缩放
    bool    m_resizing = false;
    Corner  m_resizeCorner = Corner::None;
    QPoint  m_resizeStartGlobal;
    QRect   m_resizeStartContentGeo;
    qreal   m_resizeStartScale = 1.0;

    // 裁剪
    bool    m_cropping = false;
    QPoint  m_cropStart;
    QRect   m_cropRect;

    QString m_group = QStringLiteral("Default");
    QUuid   m_desktopId;          // 创建时所在虚拟桌面
};
