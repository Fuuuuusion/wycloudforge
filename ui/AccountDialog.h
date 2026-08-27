#pragma once

#include <QDialog>

class QLabel;

namespace core {
class MusicSource;
}

namespace ui {

class AccountDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AccountDialog(core::MusicSource *netease, QWidget *parent = nullptr);

signals:
    void accountStateChanged();

private:
    void rebuild();
    void loginNetease();
    void logoutNetease();
    QLabel *makeAvatar(const QString &letterOrPath, int size);

    core::MusicSource *m_netease = nullptr;
    int m_rowCount = 0;
};

} // namespace ui
