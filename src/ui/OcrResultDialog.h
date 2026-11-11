#pragma once

#include "ElaDialog.h"
#include "platform/win/WinOcr.h"

#include <QImage>
#include <QSet>
#include <QString>

class ElaComboBox;
class ElaPlainTextEdit;
class ElaPushButton;
class OcrImagePane;

// OCR 结果弹窗（D1 左图右文，snow 识别窗口同款交互）：
//   - 左侧原图叠加每行文字框，hover 图上文字框 ↔ 文本行双向高亮
//   - 单击文字框选中该行，Shift/Ctrl 增减；文本区可字符级选中
//   - 复制选中 / 复制全文；「文字高亮视图」压暗非文字区只留文字框
//   - 可切换语言重新识别
// 外观走 Ela（ElaDialog 自带标题栏与关闭按钮），本文件属于 ui 层。
class OcrResultDialog : public ElaDialog
{
    Q_OBJECT

public:
    explicit OcrResultDialog(const WinOcr::Result &result, const QStringList &languages,
                             const QString &currentLanguage, const QImage &sourceImage,
                             QWidget *parent = nullptr);

    QString text() const;
    QString selectedLanguage() const;

    // 重新识别的结果回填：刷新图片文字框、文本内容与选中状态。
    // 没有这个入口时，点「重新识别」只有请求发出去、结果被丢弃，
    // 对话框一直显示旧文本（用户看到的"重识别没反应"）。
    void setResult(const WinOcr::Result &result, const QImage &sourceImage);
    void setRetryBusy(bool busy);

signals:
    void retryRequested(const QString &language);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setHoverLine(int index);
    void refreshHighlights();
    void copySelected();
    QString selectedLinesText() const;

    ElaPlainTextEdit *m_edit = nullptr;
    ElaComboBox *m_languageBox = nullptr;
    ElaPushButton *m_retryButton = nullptr;
    OcrImagePane *m_imagePane = nullptr;
    QVector<WinOcr::Line> m_lines;
    QSet<int> m_selectedLines;
    int m_selectionAnchor = -1;
    int m_hoverLine = -1;
    bool m_dimView = false;
};
