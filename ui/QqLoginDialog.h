#pragma once

#include <QDialog>

class QLabel;
class QJsonObject;
class QPushButton;
class QTimer;

namespace core {
class QqMusicSource;
}

namespace ui {

class QqLoginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit QqLoginDialog(core::QqMusicSource *source, QWidget *parent = nullptr);
    ~QqLoginDialog() override;

    void reject() override;

private:
    void switchMethod(const QString &method);
    void startLogin();
    void poll();
    void cancelAttempt();
    void scheduleNextPoll(int delayMs);
    void completeAuthorization(const QString &credential, const QJsonObject &profile);
    void manualCookieLogin();
    bool showQrImage(const QString &dataUrl);

    core::QqMusicSource *m_source = nullptr;
    QLabel *m_qrLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_qqButton = nullptr;
    QPushButton *m_wechatButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QTimer *m_timer = nullptr;
    QString m_method = QStringLiteral("qq");
    QString m_attemptId;
    quint64 m_generation = 0;
    bool m_pollInFlight = false;
    bool m_closing = false;
};

} // namespace ui
