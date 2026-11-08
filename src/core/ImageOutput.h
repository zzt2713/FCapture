#pragma once

#include <QString>

class QImage;

namespace ImageOutput {

int configuredQuality();
bool save(const QImage &image, const QString &path);

} // namespace ImageOutput
