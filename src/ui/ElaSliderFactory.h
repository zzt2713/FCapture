#pragma once

#include <QSlider>

class QWidget;

// Ela 滑块的工厂。
// 业务层（annotate/）不能直接 include Ela 控件头，这里返回 QSlider*，
// 调用方按 QSlider 的 API 使用，实际对象是 ElaSlider（Ela 自绘外观）。
namespace ElaSliderFactory {

QSlider *create(Qt::Orientation orientation, QWidget *parent);

} // namespace ElaSliderFactory
