#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class QPainter;

namespace ui {

// 深色极光背景:三组缓慢流动的光斑,低帧率自绘
class AuroraBackground : public QWidget
{
    Q_OBJECT
public:
    explicit AuroraBackground(QWidget *parent = nullptr);

    // 渲染深色极光场景(含呼吸明暗与扫光),供自身与播放器玻璃背底复用
    static void renderScene(QPainter *painter, const QRectF &rect, qreal seconds);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QTimer m_timer;
    QElapsedTimer m_clock;
};

} // namespace ui
