#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace ui {

class AccountPanel : public QWidget
{
    Q_OBJECT
public:
    explicit AccountPanel(QWidget *parent = nullptr);

    void refresh();

signals:
    void accountClicked();
    void settingsClicked();

private:
    QPushButton *m_avatar = nullptr;
    QPushButton *m_accountButton = nullptr;
};

} // namespace ui
