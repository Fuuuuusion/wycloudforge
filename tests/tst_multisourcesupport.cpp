#include "core/CredentialStore.h"
#include "core/QqApiService.h"
#include "core/SettingsService.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

using namespace core;

namespace {

void serveHealth(QTcpServer *server, const QString &signature)
{
    QObject::connect(server, &QTcpServer::newConnection, server, [server, signature] {
        while (server->hasPendingConnections()) {
            QTcpSocket *socket = server->nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, signature] {
                const QByteArray request = socket->readAll();
                if (!request.contains("\r\n\r\n"))
                    return;
                const QByteArray body = QStringLiteral(
                    R"({"ok":true,"data":{"service":"%1"},"error":null})")
                                                .arg(signature).toUtf8();
                QByteArray response = QByteArrayLiteral(
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ");
                response += QByteArray::number(body.size());
                response += QByteArrayLiteral("\r\n\r\n");
                response += body;
                socket->write(response);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}

QString baseUrl(const QTcpServer &server)
{
    return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
}

} // namespace

class MultiSourceSupportTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void credentialRoundTripAndRemoval();
    void legacyCredentialMigration();
    void rejectsWrongServiceSignature();
    void acceptsExpectedServiceSignature();
    void unavailableServiceDoesNotAutoStartWhenDisabled();

private:
    QTemporaryDir m_settingsDir;
};

void MultiSourceSupportTest::initTestCase()
{
    QVERIFY(m_settingsDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("WyCloudForgeTests"));
    QCoreApplication::setApplicationName(QStringLiteral("MultiSourceSupport"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
}

void MultiSourceSupportTest::cleanup()
{
    QSettings settings;
    settings.clear();
    settings.sync();
}

void MultiSourceSupportTest::credentialRoundTripAndRemoval()
{
#ifndef Q_OS_WIN
    QSKIP("DPAPI 凭据存储仅在 Windows 上启用");
#else
    const QString secret = QStringLiteral("uin=9223372036854775808123; token=只用于测试");
    QString error;
    QVERIFY2(CredentialStore::write(QStringLiteral("QQ/Music"), secret, &error),
             qPrintable(error));
    QCOMPARE(CredentialStore::read(QStringLiteral("QQ/Music"), &error), secret);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QByteArray encrypted = QSettings().value(
        QStringLiteral("credentials/qqmusic.dpapi")).toByteArray();
    QVERIFY(!encrypted.isEmpty());
    QVERIFY(!encrypted.contains(secret.toUtf8()));
    QVERIFY2(CredentialStore::remove(QStringLiteral("QQ/Music"), &error), qPrintable(error));
    QVERIFY(CredentialStore::read(QStringLiteral("QQ/Music")).isEmpty());
#endif
}

void MultiSourceSupportTest::legacyCredentialMigration()
{
#ifndef Q_OS_WIN
    QSKIP("DPAPI 凭据迁移仅在 Windows 上启用");
#else
    QSettings settings;
    settings.setValue(QStringLiteral("legacy/plainCookie"), QStringLiteral("uin=10001; key=legacy"));
    settings.sync();
    QString error;
    QVERIFY2(CredentialStore::migrateLegacy(QStringLiteral("migration-test"),
                                             QStringLiteral("legacy/plainCookie"), &error),
             qPrintable(error));
    QCOMPARE(CredentialStore::read(QStringLiteral("migration-test")),
             QStringLiteral("uin=10001; key=legacy"));
    QVERIFY(!QSettings().contains(QStringLiteral("legacy/plainCookie")));
#endif
}

void MultiSourceSupportTest::rejectsWrongServiceSignature()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    serveHealth(&server, QStringLiteral("another-service"));
    SettingsService::setQqApiBase(baseUrl(server));
    SettingsService::setQqAutoStart(false);

    QqApiService service;
    bool finished = false;
    bool ready = false;
    QString failure;
    service.ensureRunning([&] { ready = true; finished = true; },
                          [&](const QString &message) { failure = message; finished = true; });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 3000);
    QVERIFY(!ready);
    QVERIFY(failure.contains(QStringLiteral("签名不匹配")));
    QVERIFY(!service.isRunning());
}

void MultiSourceSupportTest::acceptsExpectedServiceSignature()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    serveHealth(&server, QStringLiteral("wycloudforge-qq-wrapper"));
    SettingsService::setQqApiBase(baseUrl(server));
    SettingsService::setQqAutoStart(false);

    QqApiService service;
    bool finished = false;
    QString failure;
    service.ensureRunning([&] { finished = true; },
                          [&](const QString &message) { failure = message; finished = true; });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 3000);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(service.isRunning());
}

void MultiSourceSupportTest::unavailableServiceDoesNotAutoStartWhenDisabled()
{
    QTcpServer temporaryPort;
    QVERIFY(temporaryPort.listen(QHostAddress::LocalHost, 0));
    const QString url = baseUrl(temporaryPort);
    temporaryPort.close();
    SettingsService::setQqApiBase(url);
    SettingsService::setQqAutoStart(false);

    QqApiService service;
    bool finished = false;
    QString failure;
    service.ensureRunning([&] { finished = true; },
                          [&](const QString &message) { failure = message; finished = true; });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 3000);
    QVERIFY(failure.contains(QStringLiteral("未启动")));
    QVERIFY(!service.isRunning());
}

QTEST_MAIN(MultiSourceSupportTest)
#include "tst_multisourcesupport.moc"
