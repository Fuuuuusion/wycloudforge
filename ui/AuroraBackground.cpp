#include "AuroraBackground.h"

#include <QPainter>

namespace ui {
AuroraBackground::AuroraBackground(QWidget *parent)
    : QWidget(parent)
{
}

void AuroraBackground::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x0E, 0x0E, 0x14));
}

} // namespace ui
