#include "AuroraBackground.h"

#include <QPainter>
#include <QtMath>

namespace ui {
namespace {

constexpr qreal kPi = 3.14159265358979323846;

struct Blob
{
    QColor color;
    qreal baseX;   // 相对宽度的基准位置
    qreal baseY;
    qreal size;    // 相对宽度的直径
    qreal speed;   // 周期(秒)
    qreal phase;
    qreal ampX;    // 漂移幅度(相对宽度)
    qreal ampY;
};

const Blob kBlobs[] = {
    { QColor(46, 64, 128), 0.18, 0.10, 0.62, 26.0, 0.0, 0.10, 0.09 },
    { QColor(88, 44, 128), 0.86, 0.18, 0.55, 32.0, 2.1, -0.09, 0.11 },
    { QColor(24, 90, 110), 0.42, 0.92, 0.48, 38.0, 4.2, 0.07, -0.10 }
};

} // namespace

AuroraBackground::AuroraBackground(QWidget *parent)
    : QWidget(parent)
{
    m_timer.setInterval(30);
    connect(&m_timer, &QTimer::timeout, this, [this] { update(); });
}

void AuroraBackground::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x0E, 0x0E, 0x14));
    p.setRenderHint(QPainter::Antialiasing);

    const qreal w = qMax<qreal>(1.0, width());
    const qreal h = qMax<qreal>(1.0, height());
    const qreal t = m_clock.isValid() ? m_clock.elapsed() / 1000.0 : 0.0;

    for (const Blob &b : kBlobs) {
        const qreal cx = w * (b.baseX + b.ampX * qSin(2 * kPi * t / b.speed + b.phase));
        const qreal cy = h * (b.baseY + b.ampY * qCos(2 * kPi * t / b.speed * 0.8 + b.phase));
        const qreal radius = w * b.size / 2.0;
        QRadialGradient g(cx, cy, radius);
        QColor c0 = b.color;
        c0.setAlpha(110);
        QColor c1 = b.color;
        c1.setAlpha(36);
        g.setColorAt(0.0, c0);
        g.setColorAt(0.6, c1);
        g.setColorAt(1.0, Qt::transparent);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(QPointF(cx, cy), radius, radius);
    }
}

void AuroraBackground::showEvent(QShowEvent *event)
{
    m_clock.restart();
    m_timer.start();
    QWidget::showEvent(event);
}

void AuroraBackground::hideEvent(QHideEvent *event)
{
    m_timer.stop();
    QWidget::hideEvent(event);
}

} // namespace ui
