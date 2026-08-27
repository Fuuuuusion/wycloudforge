#pragma once

#include <QPixmap>
#include <QWidget>

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

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QPixmap m_cover;
    QString m_name;
    QString m_sub;
    bool m_hover = false;
    bool m_round = false;
    int m_cardWidth = 150;
    int m_coverSize = 150;
};

} // namespace ui

