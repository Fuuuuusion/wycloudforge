#pragma once

#include <QWidget>

class QHBoxLayout;
class QLineEdit;
class QPushButton;
class QLabel;

namespace ui {

class TitleBar : public QWidget
{
    Q_OBJECT
public:
    enum WindowButton { MinimizeBtn = 0, MaximizeBtn = 1, CloseBtn = 2 };

    explicit TitleBar(QWidget *parent = nullptr);

    void setMaximizedState(bool maximized);
    QRect windowButtonRect(int index) const;
    QRect searchRect() const;

signals:
    void searchRequested(const QString &text);
    void minimizeClicked();
    void maximizeClicked();
    void closeClicked();

private:
    QLineEdit *m_searchEdit = nullptr;
    QPushButton *m_minBtn = nullptr;
    QPushButton *m_maxBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
};

} // namespace ui
