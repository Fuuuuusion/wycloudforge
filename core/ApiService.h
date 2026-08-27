#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

#include <functional>

class QNetworkAccessManager;

namespace core {

// 管理本地音乐源 API 服务(NeteaseCloudMusicApi)的生命周期
class ApiService : public QObject
{
    Q_OBJECT
public:
    explicit ApiService(QObject *parent = nullptr);
    ~ApiService() override;

    QString apiBase() const;
    void setApiBase(const QString &url);
    bool autoStart() const;
    void setAutoStart(bool on);
    QString apiDir() const;
    void setApiDir(const QString &dir);

    static QString detectApiDir();
    static QString detectNode();

    bool isRunning() const { return m_running; }

    void ensureRunning(std::function<void()> onReady, std::function<void(const QString &)> onFail);
    void start();
    void stop();

signals:
    void serverReady();
    void serverFailed(const QString &message);
    void serverStateChanged(bool running);

private:
    void checkHealth(std::function<void(bool)> done);
    void pollUntilReady(int attemptsLeft);

    QProcess *m_process = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    bool m_startedByUs = false;
    bool m_running = false;
    QString m_pendingNode;
    QString m_pendingDir;
};

} // namespace core

