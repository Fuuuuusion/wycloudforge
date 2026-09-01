#pragma once

#include <QWidget>

class QPushButton;

namespace ui {

class AccountSettingsButton;

class SidebarFooter : public QWidget
{
    Q_OBJECT
public:
    explicit SidebarFooter(QWidget *parent = nullptr);

    AccountSettingsButton *settingsButton() const { return m_settingsButton; }
    QPushButton *refreshButton() const { return m_refreshButton; }

signals:
    void settingsClicked();
    void refreshClicked();

private:
    AccountSettingsButton *m_settingsButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
};

} // namespace ui
