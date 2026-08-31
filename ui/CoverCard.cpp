#include "CoverCard.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

namespace ui {
namespace {
const QColor kText(0xE8, 0xE8, 0xE8);
const QColor kSub(0x6E, 0x6E, 0x7A);
const QColor kPrimary(0xEC, 0x41, 0x41);

QColor blendedColor(const QColor &from, const QColor &to, qreal progress)
{
    const qreal t = qBound<qreal>(0.0, progress, 1.0);
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                            from.greenF() + (to.greenF() - from.greenF()) * t,
                            from.blueF() + (to.blueF() - from.blueF()) * t);
}
}

CoverCard::CoverCard(QWidget *parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    m_hoverAnimation = new QVariantAnimation(this);
    m_hoverAnimation->setDuration(200);
    connect(m_hoverAnimation, &QVariantAnimation::valueChanged, this, [this] { update(); });
}

void CoverCard::setCover(const QPixmap &cover)
{
    m_sourceCover = cover;
    updateCoverPixmap();
    update();
}

void CoverCard::setText(const QString &name, const QString &sub)
{
    m_name = name;
    m_sub = sub;
    setAccessibleName(name);
    update();
}

void CoverCard::setRound(bool round)
{
    m_round = round;
    update();
}

void CoverCard::setFixedCardSize(int width, int coverSize)
{
    m_cardWidth = width;
    m_coverSize = coverSize;
    setFixedSize(width, coverSize + 44);
    updateCoverPixmap();
    update();
}

void CoverCard::setFullCoverCard(bool enabled)
{
    if (m_fullCoverCard == enabled)
        return;
    m_fullCoverCard = enabled;
    if (enabled)
        setFixedSize(m_cardWidth, m_coverSize + 52);
    else
        setFixedSize(m_cardWidth, m_coverSize + 44);
    updateCoverPixmap();
    update();
}

void CoverCard::setNewPlaylistCard(bool enabled)
{
    m_newPlaylistCard = enabled;
    update();
}

void CoverCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_fullCoverCard) {
        const qreal lift = 8.0 * hoverProgress() - (m_pressed ? 2.0 : 0.0);
        p.translate(0, 8.0 - lift);
        const QRectF cardRect(0, 0, m_cardWidth, m_coverSize + 44);
        QPainterPath clip;
        clip.addRoundedRect(cardRect, 8, 8);
        p.setClipPath(clip);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(QStringLiteral("#1B1B24")));
        p.drawRoundedRect(cardRect, 8, 8);
        if (!m_newPlaylistCard && !m_cover.isNull())
            p.drawPixmap(cardRect.toRect(), m_cover);

        p.setClipping(false);
        const QRectF titlePlate(8, cardRect.bottom() - 31, cardRect.width() - 16, 23);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(QStringLiteral("#16161E")));
        p.drawRoundedRect(titlePlate, 6, 6);
        p.setPen(kText);
        p.drawText(titlePlate.adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(m_name, Qt::ElideRight,
                                              int(titlePlate.width()) - 16));

        if (m_newPlaylistCard) {
            const qreal progress = hoverProgress();
            const qreal circleSize = 24.0 + 4.0 * progress;
            const QRectF plusCircle(cardRect.right() - circleSize - 8.0, cardRect.top() + 8.0,
                                    circleSize, circleSize);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(QStringLiteral("#24242E")));
            p.drawEllipse(plusCircle);

            qreal plusScale;
            if (progress <= 0.65)
                plusScale = 0.92 + (1.18 - 0.92) * (progress / 0.65);
            else
                plusScale = 1.18 - 0.08 * ((progress - 0.65) / 0.35);
            if (m_pressed)
                plusScale = 0.9;
            const qreal lineLength = (10.0 + 4.0 * progress) * plusScale;
            const QPointF center = plusCircle.center();
            p.setPen(QPen(blendedColor(QColor(QStringLiteral("#9A9AA5")),
                                      QColor(QStringLiteral("#F04A4A")), progress),
                          1.8, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(center.x() - lineLength / 2.0, center.y()),
                       QPointF(center.x() + lineLength / 2.0, center.y()));
            p.drawLine(QPointF(center.x(), center.y() - lineLength / 2.0),
                       QPointF(center.x(), center.y() + lineLength / 2.0));
        }
        return;
    }

    if (m_hover)
        p.translate(0, -2);

    QRectF coverRect(0, 0, m_coverSize, m_coverSize);
    if (m_round) {
        const QPointF c = coverRect.center();
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawEllipse(c, m_coverSize / 2.0, m_coverSize / 2.0);
        QPainterPath clip;
        clip.addEllipse(c, m_coverSize / 2.0 - 1, m_coverSize / 2.0 - 1);
        p.setClipPath(clip);
    } else {
        QPainterPath clip;
        clip.addRoundedRect(coverRect, 8, 8);
        p.setClipPath(clip);
    }
    if (!m_cover.isNull())
        p.drawPixmap(coverRect.toRect(), m_cover);

    if (m_hover && !m_round) {
        p.setClipping(false);
        const int r = 32;
        QRectF btn(coverRect.right() - r - 8, coverRect.bottom() - r - 8, r, r);
        p.setPen(Qt::NoPen);
        p.setBrush(kPrimary);
        p.drawEllipse(btn);
        QPainterPath tri;
        tri.moveTo(btn.left() + 11, btn.top() + 9);
        tri.lineTo(btn.left() + 11, btn.bottom() - 9);
        tri.lineTo(btn.right() - 8, btn.center().y());
        tri.closeSubpath();
        p.setBrush(Qt::white);
        p.drawPath(tri);
    }
    p.setClipping(false);

    QRectF textRect(0, m_coverSize + 8, m_cardWidth, 16);
    p.setPen(m_hover ? QPen(kPrimary, 1) : QPen(kText, 1));
    p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
               p.fontMetrics().elidedText(m_name, Qt::ElideRight, m_cardWidth));
    if (!m_sub.isEmpty()) {
        QRectF subRect(0, m_coverSize + 25, m_cardWidth, 14);
        p.setPen(m_hover ? QPen(kPrimary, 1) : QPen(kSub, 1));
        QFont f = p.font();
        f.setPointSizeF(f.pointSizeF() - 0.5);
        p.setFont(f);
        p.drawText(subRect, Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(m_sub, Qt::ElideRight, m_cardWidth));
    }
}

void CoverCard::enterEvent(QEnterEvent *)
{
    m_hover = true;
    const qreal current = hoverProgress();
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(current);
    m_hoverAnimation->setEndValue(1.0);
    m_hoverAnimation->start();
    update();
}

void CoverCard::leaveEvent(QEvent *)
{
    m_hover = false;
    const qreal current = hoverProgress();
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(current);
    m_hoverAnimation->setEndValue(0.0);
    m_hoverAnimation->start();
    update();
}

void CoverCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        update();
    }
    QWidget::mousePressEvent(event);
}

void CoverCard::mouseReleaseEvent(QMouseEvent *event)
{
    const bool wasPressed = m_pressed;
    m_pressed = false;
    update();
    if (wasPressed && event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        emit clicked();
        event->accept();
    }
}

void CoverCard::updateCoverPixmap()
{
    if (m_sourceCover.isNull()) {
        m_cover = {};
        return;
    }
    const QSize target = m_fullCoverCard ? QSize(m_cardWidth, m_coverSize + 44)
                                         : QSize(m_coverSize, m_coverSize);
    m_cover = m_sourceCover.scaled(target, Qt::KeepAspectRatioByExpanding,
                                   Qt::SmoothTransformation);
    if (m_cover.size() != target) {
        const int x = qMax(0, (m_cover.width() - target.width()) / 2);
        const int y = qMax(0, (m_cover.height() - target.height()) / 2);
        m_cover = m_cover.copy(x, y, target.width(), target.height());
    }
}

qreal CoverCard::hoverProgress() const
{
    if (m_hoverAnimation->state() == QAbstractAnimation::Running)
        return m_hoverAnimation->currentValue().toReal();
    return m_hover ? 1.0 : 0.0;
}

} // namespace ui
