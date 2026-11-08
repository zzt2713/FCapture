#pragma once

#include "ElaDialog.h"

#include <QVector>

class QScrollArea;
class GroupList;
class ThumbnailGrid;

// 分组管理：左侧分组列表，右侧当前组的贴图缩略图。
//
// 列表和缩略图都是自绘控件：
//   - ElaListView 的图标只能走 QIcon 位图，高 DPI 下会糊；自绘时按 dpr 取像素，
//     用 ElaAwesome 矢量字体画 Ela 图标，任意缩放下都清晰。
//   - ElaListView 在 IconMode 下不显示模型里的缩略图，换成自绘网格后
//     "截图贴图不显示"的问题一并解决。
class GroupManagerDialog : public ElaDialog
{
    Q_OBJECT

public:
    explicit GroupManagerDialog(QWidget *parent = nullptr);

protected:
    // 浏览分组时 PinManager 会隐藏其它组的贴图；关窗必须切回原分组，
    // 否则用户只是"看一眼分组"，结果桌面上所有贴图都不见了。
    void closeEvent(QCloseEvent *event) override;

private:
    void reloadGroups();
    void reloadThumbnails();
    QString currentGroup() const;

    GroupList *m_groupList = nullptr;
    ThumbnailGrid *m_thumbnailGrid = nullptr;
    QScrollArea *m_thumbnailScroll = nullptr;
    QString m_entryGroup;   // 打开窗口时的活动分组，关闭时切回
};
