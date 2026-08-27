#include "AuroraBackground.h"

#include <QPainter>
#include <QtMath>
#include <cmath>

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
    { QColor(46, 64, 128), 0.18, 0.10, 0.66, 26.0, 0.0, 0.18, 0.16 },
    { QColor(88, 44, 128), 0.88, 0.18, 0.60, 32.0, 2.1, -0.16, 0.20 },
    { QColor(24, 90, 110), 0.40, 0.94, 0.52, 38.0, 4.2, 0.14, -0.18 }
};

constexpr qreal kSweepPeriod = 14.0;
constexpr qreal kBreathPeriod = 9.0;

} // namespace

void AuroraBackground::renderScene(QPainter *p, const QRectF &rect, qreal seconds)
{
    p->fillRect(rect, QColor(0x0E, 0x0E, 0x14));
    p->setRenderHint(QPainter::Antialiasing);

    const qreal w = qMax<qreal>(1.0, rect.width());
    const qreal h = qMax<qreal>(1.0, rect.height());
    const qreal t = seconds;
    const qreal breath = 0.82 + 0.18 * qSin(2 * kPi * t / kBreathPeriod);

    for (const Blob &b : kBlobs) {
        const qreal cx = rect.left() + w * (b.baseX + b.ampX * qSin(2 * kPi * t / b.speed + b.phase));
        const qreal cy = rect.top() + h * (b.baseY + b.ampY * qCos(2 * kPi * t / b.speed * 0.8 + b.phase));
        const qreal radius = w * b.size / 2.0;
        QRadialGradient g(cx, cy, radius);
        QColor c0 = b.color;
        c0.setAlpha(qRound(150 * breath));
        QColor c1 = b.color;
        c1.setAlpha(qRound(55 * breath));
        g.setColorAt(0.0, c0);
        g.setColorAt(0.6, c1);
        g.setColorAt(1.0, Qt::transparent);
        p->setPen(Qt::NoPen);
        p->setBrush(g);
        p->drawEllipse(QPointF(cx, cy), radius, radius);
    }

    const qreal bandW = w * 0.34;
    const qreal cycle = std::fmod(t, kSweepPeriod) / kSweepPeriod;
    const qreal bandX = rect.left() + cycle * (w + bandW * 2.0) - bandW;
    p->save();
    p->translate(bandX, rect.top() + h / 2.0);
    p->rotate(16.0);
    QLinearGradient lg(-bandW / 2.0, 0, bandW / 2.0, 0);
    lg.setColorAt(0.0, QColor(180, 200, 255, 0));
    lg.setColorAt(0.45, QColor(200, 215, 255, 24));
    lg.setColorAt(0.55, QColor(220, 230, 255, 30));
    lg.setColorAt(1.0, QColor(180, 200, 255, 0));
    p->setPen(Qt::NoPen);
    p->setBrush(lg);
    p->drawRect(QRectF(-bandW / 2.0, -h * 0.8, bandW, h * 1.6));
    p->restore();
}

AuroraBackground::AuroraBackground(QWidget *parent)
    : QWidget(parent)
{
    m_timer.setInterval(30);
    connect(&m_timer, &QTimer::timeout, this, [this] { update(); });
    m_clock.start();
}

void AuroraBackground::paintEvent(QPaintEvent *)
{
    const qreal t = m_clock.isValid() ? m_clock.elapsed() / 1000.0 : 0.0;
    QPainter p(this);
    renderScene(&p, rect(), t);
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
