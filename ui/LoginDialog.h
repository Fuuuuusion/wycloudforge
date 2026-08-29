#pragma once

#include "core/MusicSource.h"

#include <QDialog>

class QLabel;
class QPushButton;
class QTimer;

namespace ui {

class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LoginDialog(core::MusicSource *source, QWidget *parent = nullptr);

    qint64 uid() const { return m_uid; }
    QString nickname() const { return m_nickname; }

private:
    void startLogin();
    void poll();
    void completeLogin(int attemptsLeft);

    core::MusicSource *m_source = nullptr;
    QLabel *m_qrLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QTimer *m_timer = nullptr;
    QString m_key;
    bool m_pollInFlight = false;
    qint64 m_uid = 0;
    QString m_nickname;
};

} // namespace ui
