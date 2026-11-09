#include "ui/BarcodeResultDialog.h"

#include "ElaPlainTextEdit.h"
#include "ElaPushButton.h"
#include "ElaText.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QListWidget>
#include <QUrl>
#include <QVBoxLayout>

BarcodeResultDialog::BarcodeResultDialog(const QVector<Item> &items, QWidget *parent)
    : ElaDialog(parent)
    , m_items(items)
{
    setWindowTitle(QStringLiteral("识别二维码 / 条码"));
    setMinimumSize(620, 420);
    resize(680, 460);
    setWindowButtonFlag(ElaAppBarType::MaximizeButtonHint, false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 12, 20, 16);
    layout->setSpacing(12);

    // ---- 顶部：结果数量 + 复制 / 打开链接 ----
    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(10);

    auto *countText = new ElaText(
        QStringLiteral("共识别到 %1 个结果").arg(items.size()), this);
    countText->setTextStyle(ElaTextType::Caption);
    topRow->addWidget(countText);
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

    auto *openBtn = makeIconButton(QStringLiteral("打开链接"), ElaIconType::Globe);
    connect(openBtn, &ElaPushButton::clicked, this, [this]() {
        const QString t = currentText().trimmed();
        const QUrl url(t);
        if (url.isValid() && (t.startsWith(QStringLiteral("http://"))
                              || t.startsWith(QStringLiteral("https://")))) {
            QDesktopServices::openUrl(url);
        }
    });
    topRow->addWidget(openBtn);

    auto *copyBtn = makeIconButton(QStringLiteral("复制"), ElaIconType::Copy);
    connect(copyBtn, &ElaPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(currentText());
    });
    topRow->addWidget(copyBtn);
    layout->addLayout(topRow);

    // ---- 正文：单结果直接显示；多结果左侧列表 + 右侧内容 ----
    auto *bodyRow = new QHBoxLayout;
    bodyRow->setSpacing(12);

    if (m_items.size() > 1) {
        m_list = new QListWidget(this);
        m_list->setFixedWidth(190);
        for (int i = 0; i < m_items.size(); ++i) {
            const Item &it = m_items.at(i);
            const QString preview = it.text.left(40).replace(QLatin1Char('\n'), QLatin1Char(' '));
            m_list->addItem(QStringLiteral("[%1] %2").arg(it.format, preview));
        }
        bodyRow->addWidget(m_list);
    }

    m_edit = new ElaPlainTextEdit(this);
    layout->addLayout(bodyRow);
    bodyRow->addWidget(m_edit, 1);

    if (!m_items.isEmpty())
        m_edit->setPlainText(m_items.first().text);

    if (m_list) {
        connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
            if (row >= 0 && row < m_items.size())
                m_edit->setPlainText(m_items.at(row).text);
        });
        m_list->setCurrentRow(0);
    }
}

QString BarcodeResultDialog::currentText() const
{
    return m_edit ? m_edit->toPlainText() : QString();
}
