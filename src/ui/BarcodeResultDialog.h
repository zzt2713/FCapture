#pragma once

#include "ElaDialog.h"

#include <QString>
#include <QVector>

class ElaPlainTextEdit;
class QListWidget;

// 二维码 / 条码识别结果对话框。
// 单个结果直接显示文本；多个结果左侧列表选择、右侧查看，
// 支持一键复制、识别内容若是 URL 可直接打开浏览器。
class BarcodeResultDialog : public ElaDialog
{
    Q_OBJECT

public:
    struct Item
    {
        QString text;
        QString format;
    };

    explicit BarcodeResultDialog(const QVector<Item> &items, QWidget *parent = nullptr);

    QString currentText() const;

private:
    QListWidget     *m_list = nullptr;
    ElaPlainTextEdit *m_edit = nullptr;
    QVector<Item>    m_items;
};
