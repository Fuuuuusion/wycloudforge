#include "CoverCard.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace ui {
namespace {
const QColor kText(0xE8, 0xE8, 0xE8);
const QColor kSub(0x6E, 0x6E, 0x7A);
const QColor kPrimary(0xEC, 0x41, 0x41);
}

CoverCard::CoverCard(QWidget *parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
}

void CoverCard::setCover(const QPixmap &cover)
{
    m_cover = cover;
    update();
}

void CoverCard::setText(const QString &name, const QString &sub)
{
    m_name = name;
    m_sub = sub;
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
    update();
}

void CoverCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

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
    update();
}

void CoverCard::leaveEvent(QEvent *)
{
    m_hover = false;
    update();
}

void CoverCard::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        emit clicked();
        event->accept();
    }
}

} // namespace ui
