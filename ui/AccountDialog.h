#pragma once

#include <QDialog>

class QLabel;

namespace core {
class MusicSource;
class QqApiService;
class QqMusicSource;
}

namespace ui {

class AccountDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AccountDialog(core::MusicSource *netease, core::QqMusicSource *qq,
                           core::QqApiService *qqService, QWidget *parent = nullptr);

signals:
    void accountStateChanged();

private:
    void rebuild();
    void loginNetease();
    void logoutNetease();
    void loginQq();
    void logoutQq();
    QLabel *makeAvatar(const QString &letterOrPath, int size);

    core::MusicSource *m_netease = nullptr;
    core::QqMusicSource *m_qq = nullptr;
    core::QqApiService *m_qqService = nullptr;
    int m_rowCount = 0;
};

} // namespace ui
