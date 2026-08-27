#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

namespace ui {

// 深色极光背景:三组缓慢流动的光斑,低帧率自绘
class AuroraBackground : public QWidget
{
    Q_OBJECT
public:
    explicit AuroraBackground(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QTimer m_timer;
    QElapsedTimer m_clock;
};

} // namespace ui

