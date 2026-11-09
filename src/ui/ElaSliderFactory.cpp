#include "ui/ElaSliderFactory.h"

#include "ElaSlider.h"

namespace ElaSliderFactory {

QSlider *create(Qt::Orientation orientation, QWidget *parent)
{
    return new ElaSlider(orientation, parent);
}

} // namespace ElaSliderFactory
