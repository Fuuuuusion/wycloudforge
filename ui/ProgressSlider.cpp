#include "ProgressSlider.h"
#include "ui/ThemeManager.h"

#include <QMouseEvent>
#include <QPainter>

ProgressSlider::ProgressSlider(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(16);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
}

void ProgressSlider::setRange(int min, int max)
{
    m_minimum = min;
    m_maximum = qMax(min + 1, max);
    if (m_value < m_minimum)
        setValue(m_minimum);
    else if (m_value > m_maximum)
        setValue(m_maximum);
    update();
}

void ProgressSlider::setValue(int value, bool emitSignal)
{
    value = qBound(m_minimum, value, m_maximum);
    if (value == m_value)
        return;
    m_value = value;
    if (emitSignal)
        emit valueChanged(m_value);
    update();
}

int ProgressSlider::valueFromX(int x) const
{
    const qreal w = qMax<qreal>(1.0, width() - 8.0);
    const qreal ratio = qBound<qreal>(0.0, (x - 4.0) / w, 1.0);
    return qRound(m_minimum + ratio * (m_maximum - m_minimum));
}

void ProgressSlider::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int trackHeight = 4;
    const int y = height() / 2;
    const qreal ratio = m_maximum > m_minimum ? qreal(m_value - m_minimum) / (m_maximum - m_minimum) : 0.0;
    const qreal left = 4.0;
    const qreal right = width() - 4.0;
    const qreal fillEnd = left + (right - left) * ratio;

    p.setPen(Qt::NoPen);
    p.setBrush(themeColor(ui::ThemeColor::ProgressTrack));
    p.drawRoundedRect(QRectF(left, y - trackHeight / 2.0, right - left, trackHeight), 2, 2);
    p.setBrush(themeColor(ui::ThemeColor::Accent));
    p.drawRoundedRect(QRectF(left, y - trackHeight / 2.0, qMax<qreal>(0, fillEnd - left), trackHeight), 2, 2);

    if (m_showHandle && (m_hover || m_dragging)) {
        const int handleR = m_dragging ? 7 : 6;
        p.setBrush(themeColor(ui::ThemeColor::TextOnAccent));
        p.setPen(QPen(themeColor(ui::ThemeColor::Accent), 2));
        p.drawEllipse(QPointF(fillEnd, y), handleR, handleR);
    }
}

void ProgressSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        emit dragStarted();
        setValue(valueFromX(event->position().x()));
        event->accept();
    }
}

void ProgressSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        setValue(valueFromX(event->position().x()));
        event->accept();
    }
}

void ProgressSlider::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        // seekFinished 在 dragFinished 之前发出，让播放器同步 seek 产生的
        // positionChanged 仍处于拖动保护期，不能把刚释放的目标位置覆盖掉。
        emit seekFinished(m_value);
        m_dragging = false;
        emit dragFinished();
        update();
        event->accept();
    }
}

void ProgressSlider::enterEvent(QEnterEvent *)
{
    m_hover = true;
    update();
}

void ProgressSlider::leaveEvent(QEvent *)
{
    m_hover = false;
    update();
}
