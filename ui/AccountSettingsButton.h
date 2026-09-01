#pragma once

#include <QPushButton>
#include <QVariantAnimation>

class QEnterEvent;
class QPaintEvent;

namespace ui {

class AccountSettingsButton final : public QPushButton
{
    Q_OBJECT
public:
    explicit AccountSettingsButton(QWidget *parent = nullptr);

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void animateTo(qreal target);

    QVariantAnimation m_animation;
    qreal m_progress = 0.0;
};

} // namespace ui
