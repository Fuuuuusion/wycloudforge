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
    enum WindowButton { SettingsBtn = 0, MinimizeBtn = 1, MaximizeBtn = 2, CloseBtn = 3 };

    explicit TitleBar(QWidget *parent = nullptr);

    void setMaximizedState(bool maximized);
    QRect windowButtonRect(int index) const;

signals:
    void searchRequested(const QString &text);
    void settingsClicked();
    void minimizeClicked();
    void maximizeClicked();
    void closeClicked();

private:
    QLineEdit *m_searchEdit = nullptr;
    QPushButton *m_settingsBtn = nullptr;
    QPushButton *m_minBtn = nullptr;
    QPushButton *m_maxBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
};

} // namespace ui
