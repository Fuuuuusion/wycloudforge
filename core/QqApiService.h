#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

#include <functional>

class QNetworkAccessManager;

namespace core {

class QqApiService : public QObject
{
    Q_OBJECT
public:
    explicit QqApiService(QObject *parent = nullptr);
    ~QqApiService() override;

    QString apiBase() const;
    QString apiDir() const;
    bool autoStart() const;
    bool isRunning() const { return m_running; }

    static QString detectApiDir();
    static QString detectNode();

    void ensureRunning(std::function<void()> onReady,
                       std::function<void(const QString &)> onFail);
    void start();
    void stop();

signals:
    void serverReady();
    void serverFailed(const QString &message);
    void serverStateChanged(bool running);

private:
    void checkHealth(std::function<void(bool, const QString &)> done);
    void pollUntilReady(int attemptsLeft, std::function<void()> onReady,
                        std::function<void(const QString &)> onFail);

    QProcess *m_process = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    bool m_startedByUs = false;
    bool m_running = false;
    QString m_pendingDir;
};

} // namespace core
