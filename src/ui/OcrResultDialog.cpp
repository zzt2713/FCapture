#include "ui/OcrResultDialog.h"

#include "ui/AppMenu.h"

#include "ElaComboBox.h"
#include "ElaPlainTextEdit.h"
#include "ElaPushButton.h"
#include "ElaText.h"
#include "ElaTheme.h"

#include <QApplication>
#include <QFontMetrics>
#include <QClipboard>
#include <QHBoxLayout>
#include <QPalette>
#include <QVBoxLayout>

OcrResultDialog::OcrResultDialog(const QString &text, const QStringList &languages,
                                 const QString &currentLanguage, QWidget *parent)
    : ElaDialog(parent)
{
    setWindowTitle(QStringLiteral("识别文字"));
    setMinimumSize(640, 440);
    resize(720, 500);
    setWindowButtonFlag(ElaAppBarType::MaximizeButtonHint, false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 12, 20, 16);
    layout->setSpacing(12);

    // ---- 顶部：语言选择 + 重新识别 + 复制 ----
    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(10);
    topRow->addWidget(new ElaText(QStringLiteral("语言"), this));

    m_languageBox = new ElaComboBox(this);
    m_languageBox->addItems(languages);
    if (!currentLanguage.isEmpty()) {
        const int idx = m_languageBox->findText(currentLanguage);
        if (idx >= 0)
            m_languageBox->setCurrentIndex(idx);
    }
    m_languageBox->setMinimumWidth(180);
    topRow->addWidget(m_languageBox);

    topRow->addStretch();

    // ElaPushButton 的 minimumSizeHint 不含图标宽度，不显式给最小宽度会把图标裁掉
    const auto makeIconButton = [this](const QString &text, ElaIconType::IconName icon) {
        auto *btn = new ElaPushButton(text, this);
        btn->setElaIcon(icon);
        const QFontMetrics fm(btn->font());
        btn->setMinimumWidth(fm.horizontalAdvance(text) + 64);
        btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        return btn;
    };

    auto *retry = makeIconButton(QStringLiteral("重新识别"), ElaIconType::ArrowsRotate);
    connect(retry, &ElaPushButton::clicked, this, [this]() {
        emit retryRequested(selectedLanguage());
    });
    topRow->addWidget(retry);

    auto *copy = makeIconButton(QStringLiteral("复制"), ElaIconType::Copy);
    connect(copy, &ElaPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(this->text());
    });
    topRow->addWidget(copy);

    layout->addLayout(topRow);

    // ---- 正文编辑区 ----
    m_edit = new ElaPlainTextEdit(this);
    m_edit->setPlainText(text);
    layout->addWidget(m_edit, 1);
}

QString OcrResultDialog::text() const
{
    return m_edit ? m_edit->toPlainText() : QString();
}

QString OcrResultDialog::selectedLanguage() const
{
    return m_languageBox ? m_languageBox->currentText() : QString();
}
