#pragma once

#include <QWidget>

class QPainter;

namespace ui {

// 固定深色背景。保留类名以避免影响主窗口的布局结构。
class AuroraBackground : public QWidget
{
    Q_OBJECT
public:
    explicit AuroraBackground(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};

} // namespace ui
