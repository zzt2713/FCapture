#pragma once

#include "ElaDialog.h"

#include <QString>

class ElaComboBox;
class ElaPlainTextEdit;

// OCR 结果弹窗：可编辑、可复制、可切换语言重识别。
// 外观走 Ela（ElaDialog 自带标题栏与关闭按钮），本文件属于 ui 层。
class OcrResultDialog : public ElaDialog
{
    Q_OBJECT

public:
    explicit OcrResultDialog(const QString &text, const QStringList &languages,
                             const QString &currentLanguage, QWidget *parent = nullptr);

    QString text() const;
    QString selectedLanguage() const;

signals:
    void retryRequested(const QString &language);

private:
    ElaPlainTextEdit *m_edit = nullptr;
    ElaComboBox *m_languageBox = nullptr;
};
