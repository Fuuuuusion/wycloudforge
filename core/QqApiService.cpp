#include "QqApiService.h"

#include "core/SettingsService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
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

QqApiService::QqApiService(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

QqApiService::~QqApiService()
{
    stop();
}

QString QqApiService::apiBase() const
{
    return SettingsService::qqApiBase();
}

bool QqApiService::autoStart() const
{
    return SettingsService::qqAutoStart();
}

QString QqApiService::apiDir() const
{
    const QString saved = SettingsService::qqApiDir();
    if (!saved.isEmpty() && QFileInfo::exists(QDir(saved).filePath(QStringLiteral("server.js"))))
        return QDir(saved).absolutePath();
    return detectApiDir();
}

QString QqApiService::detectApiDir()
{
    const QStringList candidates = {
        qEnvironmentVariable("QQ_MUSIC_API_DIR"),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("本地部署/qq-api")),
        QDir::currentPath() + QStringLiteral("/本地部署/qq-api"),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../本地部署/qq-api")),
        QDir::homePath() + QStringLiteral("/Documents/ChatGPT/仿网易云播放器/本地部署/qq-api")
    };
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo::exists(QDir(candidate).filePath(QStringLiteral("server.js"))))
            return QDir(candidate).absolutePath();
    }
    return {};
}

QString QqApiService::detectNode()
{
    if (!qEnvironmentVariable("NODE").isEmpty())
        return qEnvironmentVariable("NODE");
    const QString bundled = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("runtime/node/node.exe"));
    if (QFileInfo::exists(bundled))
        return bundled;
    return QStringLiteral("node");
}

void QqApiService::checkHealth(std::function<void(bool, const QString &)> done)
{
    QNetworkRequest request(QUrl(apiBase() + QStringLiteral("/health")));
    request.setTransferTimeout(1200);
    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
        const QByteArray payload = reply->isOpen() ? reply->readAll() : QByteArray();
        const bool transportOk = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (!transportOk) {
            if (done)
                done(false, QString());
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        const QString signature = root.value(QStringLiteral("data")).toObject()
                                      .value(QStringLiteral("service")).toString();
        const bool valid = root.value(QStringLiteral("ok")).toBool()
            && signature == QStringLiteral("wycloudforge-qq-wrapper");
        if (done)
            done(valid, valid ? QString()
                              : QStringLiteral("QQ 音乐服务地址已被其他服务占用或服务签名不匹配：%1")
                                    .arg(reply->url().adjusted(QUrl::RemovePath).toString()));
    });
}

void QqApiService::ensureRunning(std::function<void()> onReady,
                                 std::function<void(const QString &)> onFail)
{
    checkHealth([this, onReady = std::move(onReady), onFail = std::move(onFail)](bool ok, const QString &error) {
        if (ok) {
            m_running = true;
            emit serverStateChanged(true);
            if (onReady)
                onReady();
            return;
        }
        if (!error.isEmpty()) {
            m_running = false;
            emit serverFailed(error);
            if (onFail)
                onFail(error);
            return;
        }
        if (!autoStart()) {
            const QString message = QStringLiteral("QQ 音乐服务未启动（已关闭自动启动）");
            if (onFail)
                onFail(message);
            return;
        }
        const QString dir = apiDir();
        if (dir.isEmpty()) {
            const QString message = QStringLiteral("未找到 QQ 音乐包装服务目录");
            emit serverFailed(message);
            if (onFail)
                onFail(message);
            return;
        }
        if (!QFileInfo::exists(QDir(dir).filePath(
                QStringLiteral("node_modules/@sansenjian/qq-music-api/package.json")))) {
            const QString message = QStringLiteral(
                "QQ 音乐依赖未安装，请在 本地部署/qq-api 目录手动执行 npm install");
            emit serverFailed(message);
            if (onFail)
                onFail(message);
            return;
        }
        start();
        pollUntilReady(15, onReady, onFail);
    });
}

void QqApiService::start()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
        return;
    const QString dir = apiDir();
    if (dir.isEmpty()) {
        emit serverFailed(QStringLiteral("未找到 QQ 音乐包装服务目录"));
        return;
    }
    if (!QFileInfo::exists(QDir(dir).filePath(QStringLiteral("node_modules/@sansenjian/qq-music-api/package.json")))) {
        emit serverFailed(QStringLiteral("QQ 音乐依赖未安装，请在 qq-api 目录手动执行 npm install"));
        return;
    }
    m_pendingDir = dir;
    m_startedByUs = true;
    if (m_process)
        delete m_process;
    m_process = new QProcess(this);
    m_process->setProgram(detectNode());
    m_process->setArguments({ QDir(dir).filePath(QStringLiteral("server.js")) });
    m_process->setWorkingDirectory(dir);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PORT"),
                       QString::number(QUrl(apiBase()).port(3200)));
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

void QqApiService::stop()
{
    if (m_process && m_startedByUs && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(2000))
            m_process->kill();
    }
    m_running = false;
    m_startedByUs = false;
}

void QqApiService::pollUntilReady(int attemptsLeft, std::function<void()> onReady,
                                  std::function<void(const QString &)> onFail)
{
    if (attemptsLeft <= 0) {
        const QString message = QStringLiteral("QQ 音乐服务启动超时：%1")
                                    .arg(QDir(m_pendingDir).filePath(QStringLiteral("server.js")));
        m_running = false;
        emit serverStateChanged(false);
        emit serverFailed(message);
        if (onFail)
            onFail(message);
        return;
    }
    QTimer::singleShot(500, this, [this, attemptsLeft, onReady, onFail] {
        checkHealth([this, attemptsLeft, onReady, onFail](bool ok, const QString &error) {
            if (ok) {
                m_running = true;
                emit serverStateChanged(true);
                emit serverReady();
                if (onReady)
                    onReady();
            } else if (!error.isEmpty()) {
                m_running = false;
                emit serverFailed(error);
                if (onFail)
                    onFail(error);
            } else {
                pollUntilReady(attemptsLeft - 1, onReady, onFail);
            }
        });
    });
}

} // namespace core
