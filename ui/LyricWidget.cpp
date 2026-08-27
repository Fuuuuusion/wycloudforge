#include "LyricWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

namespace ui {
namespace {
const QColor kIdle(0x8F, 0x8F, 0x9C);
const QColor kActive(0xEC, 0x41, 0x41);
}

LyricWidget::LyricWidget(QWidget *parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    m_anim = new QTimer(this);
    m_anim->setInterval(30);
    connect(m_anim, &QTimer::timeout, this, &LyricWidget::animateStep);
}

void LyricWidget::setLyrics(const QList<core::LyricLine> &lines, const QList<core::LyricLine> &secondary)
{
    m_lines = lines;
    m_secondary = secondary;
    m_current = -1;
    m_offset = 0;
    m_target = 0;
    update();
}

void LyricWidget::setPosition(qint64 ms)
{
    int idx = -1;
    for (int i = 0; i < m_lines.size(); ++i) {
        if (m_lines[i].timeMs <= ms)
            idx = i;
        else
            break;
    }
    if (idx == m_current)
        return;
    m_current = idx;
    updateTarget();
}

void LyricWidget::setFontSize(int px)
{
    m_fontSize = qBound(12, px, 30);
    updateTarget();
    update();
}

void LyricWidget::clear()
{
    m_lines.clear();
    m_secondary.clear();
    m_current = -1;
    m_offset = 0;
    m_target = 0;
    update();
}

void LyricWidget::animateStep()
{
    const qreal diff = m_target - m_offset;
    if (qAbs(diff) < 0.5) {
        m_offset = m_target;
        m_anim->stop();
    } else {
        m_offset += diff * 0.22;
    }
    update();
}

void LyricWidget::updateTarget()
{
    if (m_lines.isEmpty()) {
        m_target = 0;
        return;
    }
    const qreal lineHeight = m_fontSize * 1.6;
    const qreal currentCenter = (m_current < 0 ? 0 : m_current) * lineHeight + lineHeight / 2.0;
    m_target = currentCenter - height() / 2.0;
    m_anim->start();
}

void LyricWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    if (m_lines.isEmpty()) {
        p.setPen(QColor(0x99, 0x99, 0x99));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("暂无歌词"));
        return;
    }

    const qreal lineHeight = m_fontSize * 1.6;
    const int activeSize = m_fontSize + 2;
    QFont activeFont(QStringLiteral("Microsoft YaHei UI"), activeSize, QFont::DemiBold);
    QFont idleFont(QStringLiteral("Microsoft YaHei UI"), m_fontSize);

    const int centerY = height() / 2;
    for (int i = 0; i < m_lines.size(); ++i) {
        const qreal y = i * lineHeight - m_offset;
        if (y + lineHeight < 0 || y > height())
            continue;
        const bool active = (i == m_current);
        p.setFont(active ? activeFont : idleFont);
        p.setPen(active ? kActive : kIdle);
        const QRectF r(0, y, width(), lineHeight);
        p.drawText(r, Qt::AlignCenter, m_lines[i].text);
        if (i == m_current && !m_secondary.isEmpty()) {
            for (const core::LyricLine &sub : m_secondary) {
                if (sub.timeMs == m_lines[i].timeMs) {
                    QFont subFont(QStringLiteral("Microsoft YaHei UI"), m_fontSize - 4);
                    p.setFont(subFont);
                    p.setPen(QColor(0x9A, 0x9A, 0xA5));
                    p.drawText(QRectF(0, y + lineHeight * 0.52, width(), lineHeight * 0.5),
                               Qt::AlignCenter, sub.text);
                    break;
                }
            }
        }
    }
    Q_UNUSED(centerY);
}

void LyricWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_lines.isEmpty() || event->button() != Qt::LeftButton)
        return;
    const qreal lineHeight = m_fontSize * 1.6;
    const int idx = int((event->position().y() + m_offset) / lineHeight);
    if (idx >= 0 && idx < m_lines.size())
        emit seekRequested(m_lines[idx].timeMs);
}

} // namespace ui
