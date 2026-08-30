#include "core/CredentialStore.h"
#include "core/NeteaseApiClient.h"
#include "core/QqApiService.h"
#include "core/QqMusicSource.h"
#include "core/SettingsService.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonDocument>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

#include <functional>
#include <memory>

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

using JsonHandler = std::function<QJsonObject(const QString &, const QUrl &, const QJsonObject &)>;

void serveJson(QTcpServer *server, JsonHandler handler)
{
    QObject::connect(server, &QTcpServer::newConnection, server,
                     [server, handler = std::move(handler)] {
        while (server->hasPendingConnections()) {
            QTcpSocket *socket = server->nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            auto completed = std::make_shared<bool>(false);
            QObject::connect(socket, &QTcpSocket::readyRead, socket,
                             [socket, buffer, completed, handler] {
                if (*completed)
                    return;
                buffer->append(socket->readAll());
                const int headerEnd = buffer->indexOf("\r\n\r\n");
                if (headerEnd < 0)
                    return;
                const QByteArray headers = buffer->left(headerEnd);
                int contentLength = 0;
                const QList<QByteArray> lines = headers.split('\n');
                for (const QByteArray &rawLine : lines) {
                    const QByteArray line = rawLine.trimmed();
                    if (line.toLower().startsWith("content-length:"))
                        contentLength = line.mid(line.indexOf(':') + 1).trimmed().toInt();
                }
                const int bodyStart = headerEnd + 4;
                if (buffer->size() < bodyStart + contentLength)
                    return;
                *completed = true;
                const QList<QByteArray> requestParts = lines.value(0).trimmed().split(' ');
                const QString method = QString::fromLatin1(requestParts.value(0));
                const QString target = QString::fromLatin1(requestParts.value(1));
                const QUrl url(QStringLiteral("http://127.0.0.1") + target);
                const QByteArray bodyBytes = buffer->mid(bodyStart, contentLength);
                const QJsonObject body = QJsonDocument::fromJson(bodyBytes).object();
                const QByteArray responseBody = QJsonDocument(handler(method, url, body))
                                                    .toJson(QJsonDocument::Compact);
                QByteArray response = QByteArrayLiteral(
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ");
                response += QByteArray::number(responseBody.size());
                response += QByteArrayLiteral("\r\n\r\n");
                response += responseBody;
                socket->write(response);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
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
    void neteaseCategorizedSearchAndDiscovery();
    void qqCategorizedSearchAndDiscovery();

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

void MultiSourceSupportTest::neteaseCategorizedSearchAndDiscovery()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    serveJson(&server, [](const QString &, const QUrl &url, const QJsonObject &) {
        if (url.path() == QStringLiteral("/cloudsearch")) {
            const QUrlQuery query(url);
            const QString type = query.queryItemValue(QStringLiteral("type"));
            if (type == QStringLiteral("1")) {
                return QJsonObject{
                    { QStringLiteral("result"), QJsonObject{
                        { QStringLiteral("songCount"), 1 },
                        { QStringLiteral("songs"), QJsonArray{
                            QJsonObject{
                                { QStringLiteral("id"), QStringLiteral("restricted-song") },
                                { QStringLiteral("name"), QStringLiteral("受限歌曲") },
                                { QStringLiteral("pop"), 99.0 },
                                { QStringLiteral("privilege"), QJsonObject{
                                    { QStringLiteral("st"), -5 }
                                } },
                                { QStringLiteral("ar"), QJsonArray{
                                    QJsonObject{ { QStringLiteral("name"), QStringLiteral("受限歌手") } }
                                } },
                                { QStringLiteral("al"), QJsonObject{
                                    { QStringLiteral("id"), QStringLiteral("restricted-album") },
                                    { QStringLiteral("name"), QStringLiteral("受限专辑") }
                                } }
                            }
                        } }
                    } }
                };
            }
            if (type == QStringLiteral("100")) {
                return QJsonObject{
                    { QStringLiteral("result"), QJsonObject{
                        { QStringLiteral("artistCount"), 1 },
                        { QStringLiteral("artists"), QJsonArray{
                            QJsonObject{
                                { QStringLiteral("id"), QStringLiteral("netease-artist") },
                                { QStringLiteral("name"), QStringLiteral("网易云测试歌手") },
                                { QStringLiteral("picUrl"), QStringLiteral("https://example.test/n-artist.jpg") }
                            }
                        } }
                    } }
                };
            }
            if (type == QStringLiteral("1006")) {
                return QJsonObject{
                    { QStringLiteral("result"), QJsonObject{
                        { QStringLiteral("songCount"), 1 },
                        { QStringLiteral("songs"), QJsonArray{
                            QJsonObject{
                                { QStringLiteral("id"), QStringLiteral("netease-lyric-song") },
                                { QStringLiteral("name"), QStringLiteral("歌词命中歌曲") },
                                { QStringLiteral("ar"), QJsonArray{
                                    QJsonObject{ { QStringLiteral("name"), QStringLiteral("歌词歌手") } }
                                } },
                                { QStringLiteral("al"), QJsonObject{
                                    { QStringLiteral("id"), QStringLiteral("netease-album") },
                                    { QStringLiteral("name"), QStringLiteral("歌词专辑") }
                                } },
                                { QStringLiteral("lyrics"), QJsonArray{
                                    QStringLiteral("<b>命中的歌词片段</b>")
                                } }
                            }
                        } }
                    } }
                };
            }
        }
        if (url.path() == QStringLiteral("/search/suggest")) {
            return QJsonObject{
                { QStringLiteral("result"), QJsonObject{
                    { QStringLiteral("allMatch"), QJsonArray{
                        QJsonObject{
                            { QStringLiteral("keyword"), QStringLiteral("网易云联想") },
                            { QStringLiteral("type"), 100 }
                        }
                    } }
                } }
            };
        }
        if (url.path() == QStringLiteral("/search/hot/detail")) {
            return QJsonObject{
                { QStringLiteral("data"), QJsonArray{
                    QJsonObject{
                        { QStringLiteral("searchWord"), QStringLiteral("网易云热搜") },
                        { QStringLiteral("content"), QStringLiteral("热搜说明") },
                        { QStringLiteral("score"), 12345 }
                    }
                } }
            };
        }
        if (url.path() == QStringLiteral("/search/default")) {
            return QJsonObject{
                { QStringLiteral("data"), QJsonObject{
                    { QStringLiteral("showKeyword"), QStringLiteral("网易云默认词") }
                } }
            };
        }
        return QJsonObject{};
    });

    NeteaseApiClient source;
    source.setBaseUrl(baseUrl(server));
    QVERIFY(source.capabilities().searchArtists);
    QVERIFY(source.capabilities().searchLyrics);

    SearchRequest request;
    request.keywords = QStringLiteral("测试");
    request.category = SearchCategory::Artists;
    request.limit = 10;
    request.generation = 101;
    bool artistDone = false;
    SearchResponse artistResponse;
    source.search(request, [&](const SearchResponse &response) {
        artistResponse = response;
        artistDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(artistDone, 3000);
    QCOMPARE(artistResponse.generation, quint64(101));
    QCOMPARE(artistResponse.items.size(), 1);
    QCOMPARE(artistResponse.items.constFirst().type, SearchItemType::Artist);
    QCOMPARE(artistResponse.items.constFirst().remoteId, QStringLiteral("netease-artist"));

    request.category = SearchCategory::Songs;
    request.generation = 104;
    bool songDone = false;
    SearchResponse songResponse;
    source.search(request, [&](const SearchResponse &response) {
        songResponse = response;
        songDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(songDone, 3000);
    QCOMPARE(songResponse.items.size(), 1);
    QVERIFY(!songResponse.items.constFirst().playable);
    QCOMPARE(songResponse.items.constFirst().availabilityError,
             QStringLiteral("网易云版权或地区限制"));
    QCOMPARE(songResponse.items.constFirst().popularity, 99.0);

    request.category = SearchCategory::Lyrics;
    request.generation = 102;
    bool lyricDone = false;
    SearchResponse lyricResponse;
    source.search(request, [&](const SearchResponse &response) {
        lyricResponse = response;
        lyricDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(lyricDone, 3000);
    QCOMPARE(lyricResponse.items.size(), 1);
    QCOMPARE(lyricResponse.items.constFirst().type, SearchItemType::Lyric);
    QCOMPARE(lyricResponse.items.constFirst().subtitle, QStringLiteral("命中的歌词片段"));

    request.category = SearchCategory::All;
    request.limit = 10;
    request.offset = 10;
    request.generation = 103;
    bool allDone = false;
    SearchResponse allResponse;
    source.search(request, [&](const SearchResponse &response) {
        allResponse = response;
        allDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(allDone, 3000);
    QCOMPARE(allResponse.category, SearchCategory::All);
    QCOMPARE(allResponse.generation, quint64(103));
    QCOMPARE(allResponse.offset, 10);
    QCOMPARE(allResponse.items.size(), 3);
    QCOMPARE(allResponse.items.at(0).type, SearchItemType::Artist);
    QCOMPARE(allResponse.items.at(0).sourceRank, 3);
    QCOMPARE(allResponse.items.at(1).type, SearchItemType::Song);
    QCOMPARE(allResponse.items.at(2).type, SearchItemType::Lyric);

    bool suggestionDone = false;
    QList<SearchSuggestion> suggestions;
    source.searchSuggestions(QStringLiteral("网"), 5,
                             [&](const QList<SearchSuggestion> &value) {
        suggestions = value;
        suggestionDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(suggestionDone, 3000);
    QCOMPARE(suggestions.size(), 1);
    QCOMPARE(suggestions.constFirst().type, SearchItemType::Artist);

    bool hotDone = false;
    QList<HotSearchTerm> terms;
    source.hotSearch(5, [&](const QList<HotSearchTerm> &value) {
        terms = value;
        hotDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(hotDone, 3000);
    QCOMPARE(terms.size(), 1);
    QCOMPARE(terms.constFirst().text, QStringLiteral("网易云热搜"));
    QCOMPARE(terms.constFirst().score, 12345.0);

    bool defaultDone = false;
    QString defaultText;
    source.defaultSearchText([&](const QString &value) {
        defaultText = value;
        defaultDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(defaultDone, 3000);
    QCOMPARE(defaultText, QStringLiteral("网易云默认词"));
}

void MultiSourceSupportTest::qqCategorizedSearchAndDiscovery()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    serveJson(&server, [](const QString &, const QUrl &url, const QJsonObject &body) {
        QJsonObject data;
        if (url.path() == QStringLiteral("/v1/search")) {
            const QString category = body.value(QStringLiteral("category")).toString();
            data.insert(QStringLiteral("category"), category);
            data.insert(QStringLiteral("hasMore"), false);
            data.insert(QStringLiteral("items"), QJsonArray{
                QJsonObject{
                    { QStringLiteral("type"), QStringLiteral("playlist") },
                    { QStringLiteral("remoteId"), QStringLiteral("qq-playlist-as-text") },
                    { QStringLiteral("title"), QStringLiteral("QQ 测试歌单") },
                    { QStringLiteral("subtitle"), QStringLiteral("创建者") },
                    { QStringLiteral("coverUrl"), QStringLiteral("https://example.test/q-playlist.jpg") }
                }
            });
        } else if (url.path() == QStringLiteral("/v1/search/suggest")) {
            data.insert(QStringLiteral("suggestions"), QJsonArray{
                QJsonObject{
                    { QStringLiteral("type"), QStringLiteral("song") },
                    { QStringLiteral("remoteId"), QStringLiteral("qq-suggest-song") },
                    { QStringLiteral("text"), QStringLiteral("QQ 联想歌曲") },
                    { QStringLiteral("subtitle"), QStringLiteral("QQ 歌手") }
                }
            });
        } else if (url.path() == QStringLiteral("/v1/search/hot")) {
            data.insert(QStringLiteral("terms"), QJsonArray{
                QJsonObject{
                    { QStringLiteral("text"), QStringLiteral("QQ 热搜") },
                    { QStringLiteral("description"), QStringLiteral("QQ 热搜说明") },
                    { QStringLiteral("score"), 98765 }
                }
            });
        }
        return QJsonObject{
            { QStringLiteral("ok"), true },
            { QStringLiteral("data"), data },
            { QStringLiteral("error"), QJsonValue::Null }
        };
    });

    QqMusicSource source;
    source.setBaseUrl(baseUrl(server));
    QVERIFY(source.capabilities().searchPlaylists);
    QVERIFY(source.capabilities().searchSuggestions);

    SearchRequest request;
    request.keywords = QStringLiteral("测试");
    request.category = SearchCategory::Playlists;
    request.limit = 10;
    request.generation = 201;
    bool searchDone = false;
    SearchResponse searchResponse;
    source.search(request, [&](const SearchResponse &response) {
        searchResponse = response;
        searchDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(searchDone, 3000);
    QCOMPARE(searchResponse.generation, quint64(201));
    QCOMPARE(searchResponse.items.size(), 1);
    QCOMPARE(searchResponse.items.constFirst().type, SearchItemType::Playlist);
    QCOMPARE(searchResponse.items.constFirst().remoteId,
             QStringLiteral("qq-playlist-as-text"));

    bool suggestionDone = false;
    QList<SearchSuggestion> suggestions;
    source.searchSuggestions(QStringLiteral("Q"), 5,
                             [&](const QList<SearchSuggestion> &value) {
        suggestions = value;
        suggestionDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(suggestionDone, 3000);
    QCOMPARE(suggestions.size(), 1);
    QCOMPARE(suggestions.constFirst().remoteId, QStringLiteral("qq-suggest-song"));

    bool hotDone = false;
    QList<HotSearchTerm> terms;
    source.hotSearch(5, [&](const QList<HotSearchTerm> &value) {
        terms = value;
        hotDone = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(hotDone, 3000);
    QCOMPARE(terms.size(), 1);
    QCOMPARE(terms.constFirst().text, QStringLiteral("QQ 热搜"));
    QCOMPARE(terms.constFirst().score, 98765.0);
}

QTEST_MAIN(MultiSourceSupportTest)
#include "tst_multisourcesupport.moc"
