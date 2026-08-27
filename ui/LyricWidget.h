#pragma once

#include "core/LrcParser.h"

#include <QList>
#include <QWidget>

class QTimer;

namespace ui {

class LyricWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LyricWidget(QWidget *parent = nullptr);

    void setLyrics(const QList<core::LyricLine> &lines, const QList<core::LyricLine> &secondary = {});
    void setPosition(qint64 ms);
    void setFontSize(int px);
    bool hasLyrics() const { return !m_lines.isEmpty(); }
    void clear();

signals:
    void seekRequested(qint64 ms);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void animateStep();
    void updateTarget();

    QList<core::LyricLine> m_lines;
    QList<core::LyricLine> m_secondary;
    int m_current = -1;
    int m_fontSize = 18;
    qreal m_offset = 0;
    qreal m_target = 0;
    QTimer *m_anim = nullptr;
};

} // namespace ui
