#pragma once

#include <QSize>
#include <QWidget>

class QLineEdit;

// 选区尺寸输入框（截图层的子控件）
//
// 点击尺寸标签后出现，可输入宽高；Tab 在宽高之间切换，Enter 确认，Esc 取消。
// 单位由调用方决定（px 传物理像素，dip 传逻辑像素）。
class SizeInputBox : public QWidget
{
    Q_OBJECT

public:
    explicit SizeInputBox(QWidget *parent);

    void startEditing(const QSize &currentSize, const QString &unit);

public slots:
    void commit();
    void cancel();

signals:
    void committed(const QSize &size);
    void cancelled();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *e) override;

private:
    QLineEdit *m_widthEdit = nullptr;
    QLineEdit *m_heightEdit = nullptr;
    bool m_done = false;
};
