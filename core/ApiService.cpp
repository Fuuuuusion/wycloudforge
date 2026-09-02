#include "ApiService.h"

#include "core/SettingsService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace core {

ApiService::ApiService(QObject *parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

ApiService::~ApiService()
{
    stop();
}

QString ApiService::apiBase() const
{
    return SettingsService::onlineApiBase();
}

void ApiService::setApiBase(const QString &url)
{
    SettingsService::setOnlineApiBase(url);
}

bool ApiService::autoStart() const
{
    return SettingsService::onlineAutoStart();
}

void ApiService::setAutoStart(bool on)
{
    SettingsService::setOnlineAutoStart(on);
}

QString ApiService::apiDir() const
{
    const QString saved = SettingsService::onlineApiDir();
    if (!saved.isEmpty() && QFileInfo::exists(QDir(saved).filePath(QStringLiteral("app.js"))))
        return saved;
    return detectApiDir();
}

void ApiService::setApiDir(const QString &dir)
{
    SettingsService::setOnlineApiDir(dir);
}

QString ApiService::detectApiDir()
{
    const QStringList candidates = {
        qEnvironmentVariable("NET_EASE_API_DIR"),
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("本地部署/netease-api/node_modules/NeteaseCloudMusicApi")),
        QDir::currentPath() + QStringLiteral("/本地部署/netease-api/node_modules/NeteaseCloudMusicApi"),
        QDir::homePath() + QStringLiteral("/Documents/ChatGPT/仿网易云播放器/本地部署/netease-api/node_modules/NeteaseCloudMusicApi")
    };
    for (const QString &c : candidates) {
        if (!c.isEmpty() && QFileInfo::exists(QDir(c).filePath(QStringLiteral("app.js"))))
            return QDir(c).absolutePath();
    }
    return {};
}

QString ApiService::detectNode()
{
    if (!qEnvironmentVariable("NODE").isEmpty())
        return qEnvironmentVariable("NODE");
    const QString bundled = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("runtime/node/node.exe"));
    if (QFileInfo::exists(bundled))
        return bundled;
    return QStringLiteral("node");
}

void ApiService::checkHealth(std::function<void(bool)> done)
{
    QNetworkRequest req(QUrl(apiBase() + QStringLiteral("/")));
    req.setTransferTimeout(1200);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        if (done)
            done(reply->error() == QNetworkReply::NoError);
    });
}

void ApiService::ensureRunning(std::function<void()> onReady, std::function<void(const QString &)> onFail)
{
    checkHealth([this, onReady, onFail](bool ok) {
        if (ok) {
            m_running = true;
            emit serverStateChanged(true);
            if (onReady)
                onReady();
            return;
        }
        if (!autoStart()) {
            m_running = false;
            if (onFail)
                onFail(QStringLiteral("在线服务未启动(已关闭自动启动)"));
            return;
        }
        start();
        pollUntilReady(15, onReady, onFail);
    });
}

void ApiService::start()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
        return;
    const QString dir = apiDir();
    if (dir.isEmpty()) {
        emit serverFailed(QStringLiteral("未找到网易云 API 目录,请在设置中手动指定"));
        return;
    }
    m_startedByUs = true;
    m_pendingDir = dir;
    m_pendingNode = detectNode();
    if (m_process)
        delete m_process;
    m_process = new QProcess(this);
    m_process->setProgram(m_pendingNode);
    m_process->setArguments({ QDir(dir).filePath(QStringLiteral("app.js")) });
    m_process->setWorkingDirectory(dir);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PORT"),
                       QString::number(QUrl(apiBase()).port(3000)));
    m_process->setProcessEnvironment(environment);
#ifdef Q_OS_WIN
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    connect(m_process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        m_running = false;
        emit serverStateChanged(false);
    });
    m_process->start();
}

void ApiService::stop()
{
    if (m_process && m_startedByUs && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(2000))
            m_process->kill();
    }
    m_running = false;
    m_startedByUs = false;
}

void ApiService::pollUntilReady(int attemptsLeft, std::function<void()> onReady,
                                std::function<void(const QString &)> onFail)
{
    if (attemptsLeft <= 0) {
        m_running = false;
        emit serverStateChanged(false);
        const QString message = QStringLiteral("在线服务启动超时,请手动运行:node \"%1\"")
                                    .arg(QDir(m_pendingDir).filePath(QStringLiteral("app.js")));
        emit serverFailed(message);
        if (onFail)
            onFail(message);
        return;
    }
    QTimer::singleShot(800, this, [this, attemptsLeft, onReady, onFail] {
        checkHealth([this, attemptsLeft, onReady, onFail](bool ok) {
            if (ok) {
                m_running = true;
                emit serverStateChanged(true);
                emit serverReady();
                if (onReady)
                    onReady();
            } else {
                pollUntilReady(attemptsLeft - 1, onReady, onFail);
            }
        });
    });
}

} // namespace core
