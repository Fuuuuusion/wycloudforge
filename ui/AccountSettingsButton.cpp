#include "AccountSettingsButton.h"

#include <QEasingCurve>
#include <QEnterEvent>
#include <QPainter>

namespace ui {
namespace {

QColor blendColor(const QColor &from, const QColor &to, qreal progress)
{
    const qreal t = qBound(0.0, progress, 1.0);
    return QColor(qRound(from.red() + (to.red() - from.red()) * t),
                  qRound(from.green() + (to.green() - from.green()) * t),
                  qRound(from.blue() + (to.blue() - from.blue()) * t),
                  qRound(from.alpha() + (to.alpha() - from.alpha()) * t));
}

} // namespace

AccountSettingsButton::AccountSettingsButton(QWidget *parent)
    : QPushButton(parent)
{
    setObjectName(QStringLiteral("accountSettings"));
    setFixedSize(28, 28);
    setFlat(true);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("设置"));
    setAccessibleName(QStringLiteral("设置"));
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);
    m_animation.setEasingCurve(QEasingCurve::InOutCubic);
    connect(&m_animation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) {
        m_progress = value.toReal();
        update();
    });
}

void AccountSettingsButton::enterEvent(QEnterEvent *event)
{
    animateTo(1.0);
    QPushButton::enterEvent(event);
}

void AccountSettingsButton::leaveEvent(QEvent *event)
{
    animateTo(0.0);
    QPushButton::leaveEvent(event);
}

void AccountSettingsButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor track = blendColor(QColor(0xB8, 0xB8, 0xC4),
                              QColor(0xE8, 0xE8, 0xE8), m_progress);
    if (!isEnabled())
        track = QColor(0x6E, 0x6E, 0x7A);
    constexpr qreal left = 6.0;
    constexpr qreal right = 22.0;
    constexpr qreal ys[] = { 8.0, 14.0, 20.0 };
    constexpr qreal idlePositions[] = { 0.28, 0.72, 0.28 };
    constexpr qreal hoverPositions[] = { 0.72, 0.28, 0.72 };
    painter.setPen(QPen(track, 2.0, Qt::SolidLine, Qt::RoundCap));
    for (qreal y : ys)
        painter.drawLine(QPointF(left, y), QPointF(right, y));
    painter.setPen(Qt::NoPen);
    for (int i = 0; i < 3; ++i) {
        const qreal position = idlePositions[i]
            + (hoverPositions[i] - idlePositions[i]) * m_progress;
        painter.setBrush(track);
        painter.drawEllipse(QPointF(left + (right - left) * position, ys[i]), 3.0, 3.0);
    }
}

void AccountSettingsButton::animateTo(qreal target)
{
    const qreal distance = qAbs(target - m_progress);
    if (distance < 0.001)
        return;
    m_animation.stop();
    m_animation.setStartValue(m_progress);
    m_animation.setEndValue(target);
    m_animation.setDuration(qMax(1, qRound(140.0 * distance)));
    m_animation.start();
}

} // namespace ui
