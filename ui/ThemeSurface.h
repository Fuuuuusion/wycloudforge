#pragma once

#include <QWidget>

class QPainter;

namespace ui {

// 主窗口根表面。颜色始终来自当前主题，不承担装饰或动画效果。
class ThemeSurface : public QWidget
{
    Q_OBJECT
public:
    explicit ThemeSurface(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};

} // namespace ui
