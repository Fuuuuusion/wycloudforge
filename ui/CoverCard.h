#pragma once

#include <QPixmap>
#include <QWidget>

class QVariantAnimation;

namespace ui {

class CoverCard : public QWidget
{
    Q_OBJECT
public:
    explicit CoverCard(QWidget *parent = nullptr);

    void setCover(const QPixmap &cover);
    void setText(const QString &name, const QString &sub = QString());
    void setRound(bool round);
    void setFixedCardSize(int width, int coverSize);
    void setFullCoverCard(bool enabled);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void updateCoverPixmap();
    qreal hoverProgress() const;

    QPixmap m_sourceCover;
    QPixmap m_cover;
    QString m_name;
    QString m_sub;
    bool m_hover = false;
    bool m_pressed = false;
    bool m_round = false;
    bool m_fullCoverCard = false;
    int m_cardWidth = 150;
    int m_coverSize = 150;
    QVariantAnimation *m_hoverAnimation = nullptr;
};

} // namespace ui

