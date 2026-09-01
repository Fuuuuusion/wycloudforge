#pragma once

#include "core/LrcParser.h"

#include <QList>
#include <QTimer>
#include <QWidget>

namespace ui {

class LyricWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LyricWidget(QWidget *parent = nullptr);

    void setLyrics(const QList<core::LyricLine> &lines, const QList<core::LyricLine> &secondary = {});
    void setPosition(qint64 ms);
    void setFontSize(int px);
    void setPreviewReturnDelay(int ms) { m_previewTimer.setInterval(qMax(1, ms)); }
    bool hasLyrics() const { return !m_lines.isEmpty(); }
    bool isPreviewing() const { return m_previewing; }
    qreal previewOffset() const { return m_preview; }
    void clear();

signals:
    void seekRequested(qint64 ms);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void updateTarget();
    void finishPreview();
    qreal lineHeight() const;

    QList<core::LyricLine> m_lines;
    QList<core::LyricLine> m_secondary;
    int m_current = -1;
    int m_fontSize = 18;
    qreal m_offset = 0;
    qreal m_target = 0;
    qreal m_preview = 0; // 滚轮预览偏移(正值显示下方歌词)
    bool m_previewing = false;
    QTimer m_previewTimer;
};

} // namespace ui
