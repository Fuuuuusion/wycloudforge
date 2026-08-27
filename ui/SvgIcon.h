#pragma once

#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace ui {

// 用 Qt6Svg 库直接渲染 SVG(QIcon 走图像插件不可靠,改用库级渲染,任何环境都能显示)
inline QIcon makeSvgIcon(const QString &path, int size = 18)
{
    QSvgRenderer renderer(path);
    if (!renderer.isValid())
        return {};
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    renderer.render(&p, QRectF(0, 0, size, size));
    p.end();
    return QIcon(pm);
}

} // namespace ui

