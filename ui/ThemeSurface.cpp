#include "ThemeSurface.h"
#include "ui/ThemeManager.h"

#include <QPainter>

namespace ui {
ThemeSurface::ThemeSurface(QWidget *parent)
    : QWidget(parent)
{
}

void ThemeSurface::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), themeColor(ThemeColor::PageBackground));
}

} // namespace ui
