#pragma once

#include "paste/PinWindow.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVector>

class PinManager : public QObject
{
    Q_OBJECT

public:
    static PinManager *instance();

    // --- 创建 ---
    PinWindow *pasteFromClipboard();                       // F3：先恢复关闭的，再从剪贴板
    // 剪贴板/拖拽里是网页图片 URL 时异步下载后贴出
    bool pasteFromWebImageUrls(const QStringList &urls);
    // imageDpr：截图传屏幕 dpr（物理像素 → 逻辑尺寸），普通图片传 1.0
    // useExactPos：true 时严格按 globalPos 放置（截图原位）；false 时用光标位置并轻微错开
    PinWindow *pasteImage(const QImage &img,
                          const QPoint &globalPos = QPoint(),
                          const QString &sourceText = QString(),
                          qreal imageDpr = 1.0,
                          bool useExactPos = false);
    QVector<PinWindow *> pasteImageSet(const QVector<QImage> &images);

    // --- 查询 ---
    QVector<PinWindow *> windows() const { return m_windows; }
    QVector<PinWindow *> visibleWindows() const;
    QVector<PinWindow *> selectedWindows() const;
    int windowCount() const { return m_windows.size(); }
    bool allVisible() const;

    // --- 显示控制 ---
    void showAll();
    void hideAll();
    void toggleAll();
    void soloWindow(PinWindow *w);
    void restoreSolo();

    // --- 关闭 / 销毁 ---
    void closeWindow(PinWindow *w);
    void destroyWindow(PinWindow *w);
    void closeAll();
    void destroyAll();

    // --- 选择 ---
    void selectAllVisible();
    void clearSelection();
    void toggleSelection(PinWindow *w);
    void setSelection(PinWindow *w, bool selected);

    // --- 分组 ---
    QStringList groups() const;
    QString activeGroup() const { return m_activeGroup; }
    void createGroup(const QString &name);
    void removeGroup(const QString &name);
    void renameGroup(const QString &oldName, const QString &newName);
    void switchGroup(const QString &name);
    void switchToNextGroup();
    void moveToGroup(PinWindow *w, const QString &group);
    void emptyActiveGroup();

    // --- 其他 ---
    void disableClickThrough();
    void toggleClickThroughUnderCursor();
    void applyBackgroundMode(PinWindow::BackgroundMode mode);

signals:
    void windowsChanged();
    void selectionChanged();
    void groupChanged(const QString &name);
    void visibilityChanged(bool anyVisible);

private:
    explicit PinManager(QObject *parent = nullptr);
    ~PinManager() override;

    // 虚拟桌面切换时同步贴图可见性（公开接口没有桌面切换通知，只能轮询）
    void syncDesktopVisibility();

    void attachWindow(PinWindow *w);
    void detachWindow(PinWindow *w);
    void placementPosition(PinWindow *w, const QPoint &requested, bool useExactPos);

    QVector<PinWindow *> m_windows;
    QVector<PinWindow *> m_selection;
    QVector<QImage>      m_closedStack;      // 关闭后可恢复
    QVector<PinWindow *> m_soloHidden;       // solo 前隐藏的窗口

    QStringList m_groups;
    QString     m_activeGroup = QStringLiteral("Default");
    QString     m_previousGroup;

    class QTimer *m_desktopTimer = nullptr;
    QUuid        m_lastDesktopId;
};
