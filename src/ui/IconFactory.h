#pragma once

#include <QIcon>
#include <QPixmap>
#include <QString>

namespace IconFactory {

// 全部图标用 QPainter 自绘，不依赖外部 SVG 资源
QIcon appIcon();
QIcon trayIcon();

QPixmap captureIcon(int size);
QPixmap saveIcon(int size);
QPixmap copyIcon(int size);
QPixmap pinIcon(int size);
QPixmap closeIcon(int size);
QPixmap settingsIcon(int size);
QPixmap helpIcon(int size);
QPixmap exitIcon(int size);

} // namespace IconFactory
