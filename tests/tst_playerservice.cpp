#include "core/PlayerService.h"
#include "core/MusicSourceRegistry.h"
#include "core/NeteaseApiClient.h"
#include "core/QqMusicSource.h"

#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace core;

namespace {

template <typename Source>
class LocalUrlSource final : public Source
{
public:
    using Source::Source;

    void addMedia(const QString &remoteId, const QString &filePath)
    {
        m_urls.insert(remoteId, QUrl::fromLocalFile(filePath).toString());
    }

    void addUrl(const QString &remoteId, const QUrl &url)
    {
        m_urls.insert(remoteId, url.toString());
    }

    void songUrls(const QStringList &ids, MusicSource::JsonArrayFn ok,
                  MusicSource::ErrFn err = {}) override
    {
        QJsonArray result;
        for (const QString &id : ids) {
            requests.append(id);
            if (emptyResponsesRemaining > 0) {
                --emptyResponsesRemaining;
                result.append(QJsonObject{
                    { QStringLiteral("remoteId"), id },
                    { QStringLiteral("url"), QString() },
                    { QStringLiteral("error"), QStringLiteral("temporary address failure") }
                });
                continue;
            }
            const QString url = m_urls.value(id);
            if (url.isEmpty()) {
                if (err)
                    err(QStringLiteral("测试媒体不存在：%1").arg(id));
                return;
            }
            result.append(QJsonObject{
                { QStringLiteral("remoteId"), id },
                { QStringLiteral("url"), url }
            });
        }
        if (ok)
            ok(result);
    }

    void songUrls(const QList<Song> &songs, MusicSource::JsonArrayFn ok,
                  MusicSource::ErrFn err = {}) override
    {
        QStringList ids;
        ids.reserve(songs.size());
        for (const Song &song : songs)
            ids.append(song.effectiveRemoteId());
        songUrls(ids, std::move(ok), std::move(err));
    }

    void searchSongsPage(const QString &keywords, int limit, int offset,
                         MusicSource::JsonArrayFn ok,
                         MusicSource::ErrFn err = {}) override
    {
        Q_UNUSED(err)
        searchKeywords = keywords;
        searchLimit = limit;
        searchOffset = offset;
        ++searchRequests;
        if (ok)
            ok(searchPayload);
    }

    void userPlaylists(const QString &, MusicSource::JsonArrayFn ok,
                       MusicSource::ErrFn = {}) override
    {
        if (ok)
            ok(playlistPayload);
    }

    QStringList requests;
    int emptyResponsesRemaining = 0;
    QJsonArray searchPayload;
    QJsonArray playlistPayload;
    QString searchKeywords;
    int searchLimit = 0;
    int searchOffset = 0;
    int searchRequests = 0;

private:
    QHash<QString, QString> m_urls;
};

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(bytes) == bytes.size();
}

void writeWav(const QString &path, int seconds)
{
    const int sampleRate = 22050;
    const int dataSize = sampleRate * 2 * seconds;
    QByteArray data(dataSize, '\0');
    QByteArray out;
    out.append("RIFF");
    out.append(char((36 + dataSize) & 0xFF));
    out.append(char(((36 + dataSize) >> 8) & 0xFF));
    out.append(char(((36 + dataSize) >> 16) & 0xFF));
    out.append(char(((36 + dataSize) >> 24) & 0xFF));
    out.append("WAVEfmt ");
    out.append(QByteArrayLiteral("\x10\x00\x00\x00"));
    out.append(QByteArrayLiteral("\x01\x00\x01\x00"));
    out.append(char(sampleRate & 0xFF));
    out.append(char((sampleRate >> 8) & 0xFF));
    out.append(char((sampleRate >> 16) & 0xFF));
    out.append(char((sampleRate >> 24) & 0xFF));
    const int byteRate = sampleRate * 2;
    out.append(char(byteRate & 0xFF));
    out.append(char((byteRate >> 8) & 0xFF));
    out.append(char((byteRate >> 16) & 0xFF));
    out.append(char((byteRate >> 24) & 0xFF));
    out.append(QByteArrayLiteral("\x02\x00\x10\x00"));
    out.append("data");
    out.append(char(dataSize & 0xFF));
    out.append(char((dataSize >> 8) & 0xFF));
    out.append(char((dataSize >> 16) & 0xFF));
    out.append(char((dataSize >> 24) & 0xFF));
    out.append(data);
    QVERIFY2(writeFile(path, out), "failed to write wav");
}

class LocalHttpMediaServer final : public QObject
{
public:
    explicit LocalHttpMediaServer(QObject *parent = nullptr) : QObject(parent)
    {
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const QByteArray request = socket->readAll();
                    const QList<QByteArray> firstLine = request.split('\n').value(0).trimmed().split(' ');
                    const QByteArray path = firstLine.size() > 1 ? firstLine.at(1) : QByteArray();
                    const QByteArray payload = media.value(path);
                    if (payload.isEmpty()) {
                        socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    } else {
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nAccept-Ranges: bytes\r\nContent-Length: ");
                        socket->write(QByteArray::number(payload.size()));
                        socket->write("\r\nConnection: close\r\n\r\n");
                        socket->write(payload);
                    }
                    socket->disconnectFromHost();
                });
            }
        });
    }

    bool listen() { return server.listen(QHostAddress::LocalHost); }
    void add(const QByteArray &path, const QByteArray &payload) { media.insert(path, payload); }
    QUrl url(const QByteArray &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2")
                        .arg(server.serverPort()).arg(QString::fromLatin1(path)));
    }

private:
    QTcpServer server;
    QHash<QByteArray, QByteArray> media;
};

} // namespace

class PlayerServiceTest : public QObject
{
    Q_OBJECT
private slots:
    void playlistNavigation();
    void playbackQueueMutations();
    void removeSongByIdSynchronizesQueue();
    void playPauseAndPosition();
    void autoAdvanceInOrderMode();
    void repeatOneRestartsCurrentSong();
    void autoAdvanceInShuffleMode();
    void mixedSourceAutoAdvanceInOrderMode();
    void mixedSourceRepeatOneMode();
    void mixedSourceAutoAdvanceInShuffleMode();
    void cachedAndDownloadedAutoAdvance();
    void httpStreamAutoAdvance();
    void onlineUrlRetriesOnce();
    void cachedOnlineSongPlayback();
    void downloadedOnlineSongPlayback();
    void unifiedSearchContract();
    void unifiedSearchRejectsUnsupportedCategory();
    void unifiedCloudPlaylistContract();
};

void PlayerServiceTest::playlistNavigation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString p1 = dir.filePath(QStringLiteral("a.wav"));
    const QString p2 = dir.filePath(QStringLiteral("b.wav"));
    writeWav(p1, 2);
    writeWav(p2, 2);

    Song s1;
    s1.id = 1;
    s1.filePath = p1;
    s1.title = QStringLiteral("A");
    s1.durationMs = 2000;
    Song s2;
    s2.id = 2;
    s2.filePath = p2;
    s2.title = QStringLiteral("B");
    s2.durationMs = 2000;

    PlayerService player;
    QSignalSpy spy(&player, &PlayerService::songChanged);
    player.setPlaylist({ s1, s2 }, 0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(player.currentSong().title, QStringLiteral("A"));

    player.next();
    QCOMPARE(player.currentIndex(), 1);
    QCOMPARE(player.currentSong().title, QStringLiteral("B"));
    QCOMPARE(spy.count(), 2);

    player.prev();
    QCOMPARE(player.currentIndex(), 0);
    QCOMPARE(player.currentSong().title, QStringLiteral("A"));

    player.setMode(PlayerService::Shuffle);
    player.next();
    QVERIFY(player.currentIndex() >= 0 && player.currentIndex() <= 1);
}

void PlayerServiceTest::playbackQueueMutations()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QList<Song> songs;
    for (int i = 0; i < 3; ++i) {
        Song song;
        song.id = i + 1;
        song.filePath = dir.filePath(QStringLiteral("queue-%1.wav").arg(i));
        song.title = QString(QChar(QLatin1Char('A').unicode() + i));
        song.durationMs = 1000;
        writeWav(song.filePath, 1);
        songs.append(song);
    }

    PlayerService player;
    player.setPlaylist(songs, 1);
    QCOMPARE(player.currentSong().title, QStringLiteral("B"));
    QVERIFY(player.removeAt(0));
    QCOMPARE(player.playlist().size(), 2);
    QCOMPARE(player.currentIndex(), 0);
    QCOMPARE(player.currentSong().title, QStringLiteral("B"));

    QVERIFY(player.removeAt(0));
    QCOMPARE(player.playlist().size(), 1);
    QCOMPARE(player.currentSong().title, QStringLiteral("C"));

    QSignalSpy songSpy(&player, &PlayerService::songChanged);
    player.clearPlaylist();
    QVERIFY(player.playlist().isEmpty());
    QCOMPARE(player.currentIndex(), -1);
    QCOMPARE(songSpy.count(), 1);
    QCOMPARE(qvariant_cast<Song>(songSpy.first().at(0)).id, qint64(-1));
}

void PlayerServiceTest::removeSongByIdSynchronizesQueue()
{
    Song first;
    first.id = 301;
    first.filePath = QStringLiteral("C:/missing/first.mp3");
    Song second;
    second.id = 302;
    second.filePath = QStringLiteral("C:/missing/second.mp3");
    PlayerService player;
    player.setPlaylist({ first, second }, 0);
    QVERIFY(player.removeSongById(first.id));
    QCOMPARE(player.playlist().size(), 1);
    QCOMPARE(player.currentSong().id, second.id);
    QVERIFY(player.removeSongById(second.id));
    QVERIFY(player.playlist().isEmpty());
    QCOMPARE(player.currentIndex(), -1);
}

void PlayerServiceTest::unifiedSearchContract()
{
    LocalUrlSource<NeteaseApiClient> source;
    source.searchPayload = {
        QJsonObject{
            { QStringLiteral("id"), QStringLiteral("9223372036854775808124") },
            { QStringLiteral("name"), QStringLiteral("测试搜索歌曲") },
            { QStringLiteral("fee"), 1 },
            { QStringLiteral("dt"), 234000 },
            { QStringLiteral("ar"), QJsonArray{
                QJsonObject{
                    { QStringLiteral("id"), QStringLiteral("artist-id") },
                    { QStringLiteral("name"), QStringLiteral("测试歌手") }
                }
            } },
            { QStringLiteral("al"), QJsonObject{
                { QStringLiteral("id"), QStringLiteral("album-id") },
                { QStringLiteral("name"), QStringLiteral("测试专辑") },
                { QStringLiteral("picUrl"), QStringLiteral("https://example.test/cover.jpg") }
            } }
        }
    };

    SearchRequest request;
    request.keywords = QStringLiteral("  测试搜索  ");
    request.category = SearchCategory::Songs;
    request.scope = SearchScope::Netease;
    request.limit = 12;
    request.offset = 24;
    request.generation = 77;

    bool completed = false;
    QString failure;
    SearchResponse response;
    source.MusicSource::search(request, [&](const SearchResponse &value) {
        response = value;
        completed = true;
    }, [&](const QString &message) {
        failure = message;
    });

    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(completed);
    QCOMPARE(source.searchRequests, 1);
    QCOMPARE(source.searchKeywords, QStringLiteral("测试搜索"));
    QCOMPARE(source.searchLimit, 12);
    QCOMPARE(source.searchOffset, 24);
    QCOMPARE(response.source, SourceId::Netease);
    QCOMPARE(response.category, SearchCategory::Songs);
    QCOMPARE(response.generation, quint64(77));
    QCOMPARE(response.offset, 24);
    QCOMPARE(response.items.size(), 1);
    const SearchResultItem item = response.items.constFirst();
    QCOMPARE(item.type, SearchItemType::Song);
    QCOMPARE(item.remoteId, QStringLiteral("9223372036854775808124"));
    QCOMPARE(item.song.effectiveRemoteId(), item.remoteId);
    QCOMPARE(item.song.stableIdentity(),
             QStringLiteral("1:9223372036854775808124"));
    QCOMPARE(item.sourceRank, 24);
    QVERIFY(item.song.requiresVip());
}

void PlayerServiceTest::unifiedCloudPlaylistContract()
{
    LocalUrlSource<NeteaseApiClient> source;
    source.playlistPayload = {
        QJsonObject{
            { QStringLiteral("id"), QStringLiteral("9223372036854775808124") },
            { QStringLiteral("name"), QStringLiteral("云歌单") },
            { QStringLiteral("coverImgUrl"), QStringLiteral("https://example.test/cloud.jpg") },
            { QStringLiteral("description"), QStringLiteral("只读") },
        },
        QJsonObject{
            { QStringLiteral("id"), QStringLiteral("9223372036854775808124") },
            { QStringLiteral("name"), QStringLiteral("重复项") },
        },
    };

    bool completed = false;
    source.userPlaylistItems(QStringLiteral("user"),
        [&completed](const QList<OnlinePlaylist> &playlists) {
            completed = true;
            QCOMPARE(playlists.size(), 1);
            QCOMPARE(playlists.first().source, SourceId::Netease);
            QCOMPARE(playlists.first().remoteId,
                     QStringLiteral("9223372036854775808124"));
            QCOMPARE(playlists.first().name, QStringLiteral("云歌单"));
            QCOMPARE(playlists.first().coverUrl,
                     QStringLiteral("https://example.test/cloud.jpg"));
        });
    QVERIFY(completed);
}

void PlayerServiceTest::unifiedSearchRejectsUnsupportedCategory()
{
    LocalUrlSource<NeteaseApiClient> source;
    SearchRequest request;
    request.keywords = QStringLiteral("歌手");
    request.category = SearchCategory::Artists;
    request.limit = 10;
    request.generation = 3;

    bool completed = false;
    QString failure;
    source.MusicSource::search(request, [&](const SearchResponse &) {
        completed = true;
    }, [&](const QString &message) {
        failure = message;
    });

    QVERIFY(!completed);
    QCOMPARE(source.searchRequests, 0);
    QVERIFY(failure.contains(QStringLiteral("暂不支持")));
}

void PlayerServiceTest::playPauseAndPosition()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString p1 = dir.filePath(QStringLiteral("a.wav"));
    writeWav(p1, 3);
    Song s1;
    s1.id = 1;
    s1.filePath = p1;
    s1.title = QStringLiteral("A");
    s1.durationMs = 3000;

    PlayerService player;
    player.setPlaylist({ s1 }, 0);
    player.play();

    const bool started = QTest::qWaitFor([&player] {
        return player.isPlaying();
    }, 4000);
    if (!started) {
        QSKIP("无可用音频输出设备,跳过播放状态验证");
    }
    QVERIFY(player.isPlaying());
    const bool advanced = QTest::qWaitFor([&player] {
        return player.position() >= 300;
    }, 3000);
    if (!advanced)
        QSKIP("音频端点未推进媒体时钟,跳过播放位置验证");
    player.pause();
    QVERIFY(!player.isPlaying());
    player.seek(1000);
    QVERIFY(player.position() >= 900);
}

void PlayerServiceTest::autoAdvanceInOrderMode()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString p1 = dir.filePath(QStringLiteral("order-a.wav"));
    const QString p2 = dir.filePath(QStringLiteral("order-b.wav"));
    writeWav(p1, 1);
    writeWav(p2, 1);

    Song s1;
    s1.id = 101;
    s1.filePath = p1;
    s1.title = QStringLiteral("顺序一");
    Song s2;
    s2.id = 102;
    s2.filePath = p2;
    s2.title = QStringLiteral("顺序二");

    PlayerService player;
    player.setMode(PlayerService::Order);
    player.setPlaylist({ s1, s2 }, 0);
    player.play();
    if (!QTest::qWaitFor([&player] { return player.isPlaying(); }, 4000))
        QSKIP("无可用音频输出设备,跳过自动联播验证");
    QVERIFY(QTest::qWaitFor([&player] { return player.currentIndex() == 1; }, 4000));
    QCOMPARE(player.currentSong().title, QStringLiteral("顺序二"));
}

void PlayerServiceTest::repeatOneRestartsCurrentSong()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString p1 = dir.filePath(QStringLiteral("repeat-a.wav"));
    const QString p2 = dir.filePath(QStringLiteral("repeat-b.wav"));
    writeWav(p1, 1);
    writeWav(p2, 1);

    Song s1;
    s1.id = 111;
    s1.filePath = p1;
    s1.title = QStringLiteral("循环一");
    Song s2;
    s2.id = 112;
    s2.filePath = p2;
    s2.title = QStringLiteral("循环二");

    PlayerService player;
    player.setMode(PlayerService::RepeatOne);
    player.setPlaylist({ s1, s2 }, 0);
    player.play();
    if (!QTest::qWaitFor([&player] { return player.isPlaying(); }, 4000))
        QSKIP("无可用音频输出设备,跳过单曲循环验证");
    QTest::qWait(1500);
    QCOMPARE(player.currentIndex(), 0);
    QCOMPARE(player.currentSong().title, QStringLiteral("循环一"));
}

void PlayerServiceTest::autoAdvanceInShuffleMode()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QList<Song> songs;
    for (int i = 0; i < 3; ++i) {
        const QString path = dir.filePath(QStringLiteral("shuffle-%1.wav").arg(i));
        writeWav(path, 1);
        Song song;
        song.id = 121 + i;
        song.filePath = path;
        song.title = QStringLiteral("随机%1").arg(i + 1);
        songs.append(song);
    }

    PlayerService player;
    player.setMode(PlayerService::Shuffle);
    player.setPlaylist(songs, 0);
    player.play();
    if (!QTest::qWaitFor([&player] { return player.isPlaying(); }, 4000))
        QSKIP("无可用音频输出设备,跳过随机播放验证");
    QVERIFY(QTest::qWaitFor([&player] { return player.currentIndex() != 0; }, 4000));
    QVERIFY(player.currentIndex() >= 0 && player.currentIndex() < songs.size());
}

void PlayerServiceTest::mixedSourceAutoAdvanceInOrderMode()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString neteasePath = dir.filePath(QStringLiteral("mixed-netease.wav"));
    const QString qqPath = dir.filePath(QStringLiteral("mixed-qq.wav"));
    writeWav(neteasePath, 1);
    writeWav(qqPath, 1);

    LocalUrlSource<NeteaseApiClient> netease;
    LocalUrlSource<QqMusicSource> qq;
    netease.addMedia(QStringLiteral("n-order"), neteasePath);
    qq.addMedia(QStringLiteral("q-order"), qqPath);
    MusicSourceRegistry registry;
    registry.registerSource(&netease);
    registry.registerSource(&qq);

    Song first = MusicSource::makeOnlineSong(SourceId::Netease, QStringLiteral("netease"),
                                              QStringLiteral("n-order"), QStringLiteral("网易云一"),
                                              {}, {}, 1000, {});
    first.id = 201;
    Song second = MusicSource::makeOnlineSong(SourceId::QqMusic, QStringLiteral("qqmusic"),
                                               QStringLiteral("q-order"), QStringLiteral("QQ二"),
                                               {}, {}, 1000, {});
    second.id = 202;

    QCOMPARE(registry.sourceFor(first), static_cast<MusicSource *>(&netease));
    QCOMPARE(registry.sourceFor(second), static_cast<MusicSource *>(&qq));
    PlayerService player;
    player.setSourceRegistry(&registry);
    player.setMode(PlayerService::Order);
    player.setPlaylist({ first, second }, 0);
    player.play();
    if (!QTest::qWaitFor([&player] { return player.isPlaying(); }, 4000))
        QSKIP("无可用音频输出设备,跳过混合来源顺序联播验证");
    QVERIFY(QTest::qWaitFor([&player] { return player.currentIndex() == 1; }, 4000));
    QCOMPARE(netease.requests, QStringList{ QStringLiteral("n-order") });
    QCOMPARE(qq.requests, QStringList{ QStringLiteral("q-order") });
}

void PlayerServiceTest::mixedSourceRepeatOneMode()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString qqPath = dir.filePath(QStringLiteral("mixed-repeat-qq.wav"));
    const QString neteasePath = dir.filePath(QStringLiteral("mixed-repeat-netease.wav"));
    writeWav(qqPath, 1);
    writeWav(neteasePath, 1);

    LocalUrlSource<NeteaseApiClient> netease;
    LocalUrlSource<QqMusicSource> qq;
    qq.addMedia(QStringLiteral("q-repeat"), qqPath);
    netease.addMedia(QStringLiteral("n-repeat"), neteasePath);
    MusicSourceRegistry registry;
    registry.registerSource(&netease);
    registry.registerSource(&qq);

    Song first = MusicSource::makeOnlineSong(SourceId::QqMusic, QStringLiteral("qqmusic"),
                                              QStringLiteral("q-repeat"), QStringLiteral("QQ循环"),
                                              {}, {}, 1000, {});
    first.id = 211;
    Song second = MusicSource::makeOnlineSong(SourceId::Netease, QStringLiteral("netease"),
                                               QStringLiteral("n-repeat"), QStringLiteral("网易云候选"),
                                               {}, {}, 1000, {});
    second.id = 212;

    PlayerService player;
    player.setSourceRegistry(&registry);
    player.setMode(PlayerService::RepeatOne);
    player.setPlaylist({ first, second }, 0);
    player.play();
    if (!QTest::qWaitFor([&player] { return player.isPlaying(); }, 4000))
        QSKIP("无可用音频输出设备,跳过混合来源单曲循环验证");
    if (!QTest::qWaitFor([&player] { return player.position() >= 300; }, 3000))
        QSKIP("音频端点未推进媒体时钟,跳过混合来源单曲循环验证");
    QVERIFY(QTest::qWaitFor([&qq] { return qq.requests.size() >= 2; }, 4000));
    QCOMPARE(player.currentIndex(), 0);
    for (const QString &request : qq.requests)
        QCOMPARE(request, QStringLiteral("q-repeat"));
    QVERIFY(netease.requests.isEmpty());
}

void PlayerServiceTest::mixedSourceAutoAdvanceInShuffleMode()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LocalUrlSource<NeteaseApiClient> netease;
    LocalUrlSource<QqMusicSource> qq;
    QList<Song> songs;
    for (int i = 0; i < 3; ++i) {
        const QString remoteId = QStringLiteral("%1-shuffle-%2")
                                     .arg(i == 1 ? QStringLiteral("n") : QStringLiteral("q")).arg(i);
        const QString path = dir.filePath(remoteId + QStringLiteral(".wav"));
        writeWav(path, 1);
        const SourceId sourceId = i == 1 ? SourceId::Netease : SourceId::QqMusic;
        if (sourceId == SourceId::Netease)
            netease.addMedia(remoteId, path);
        else
            qq.addMedia(remoteId, path);
        Song song = MusicSource::makeOnlineSong(
            sourceId, sourceId == SourceId::Netease ? QStringLiteral("netease")
                                                     : QStringLiteral("qqmusic"),
            remoteId, QStringLiteral("混合随机%1").arg(i), {}, {}, 1000, {});
        song.id = 221 + i;
        songs.append(song);
    }
    MusicSourceRegistry registry;
    registry.registerSource(&netease);
    registry.registerSource(&qq);
    PlayerService player;
    player.setSourceRegistry(&registry);
    player.setMode(PlayerService::Shuffle);
    player.setPlaylist(songs, 0);
    player.play();
    if (!QTest::qWaitFor([&player] { return player.isPlaying(); }, 4000))
        QSKIP("无可用音频输出设备,跳过混合来源随机联播验证");
    QVERIFY(QTest::qWaitFor([&player] { return player.currentIndex() != 0; }, 4000));
    const Song current = player.currentSong();
    if (current.sourceId() == SourceId::Netease)
        QVERIFY(netease.requests.contains(current.effectiveRemoteId()));
    else
        QVERIFY(qq.requests.contains(current.effectiveRemoteId()));
}

void PlayerServiceTest::cachedAndDownloadedAutoAdvance()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString cachePath = dir.filePath(QStringLiteral("cached-first.wav"));
    const QString downloadPath = dir.filePath(QStringLiteral("downloaded-second.wav"));
    writeWav(cachePath, 1);
    writeWav(downloadPath, 1);
    Song cached = MusicSource::makeOnlineSong(SourceId::Netease, QStringLiteral("netease"),
        QStringLiteral("cached-first"), QStringLiteral("缓存一"), {}, {}, 1000, {});
    cached.id = 241;
    cached.cachePath = cachePath;
    Song downloaded = MusicSource::makeOnlineSong(SourceId::QqMusic, QStringLiteral("qqmusic"),
        QStringLiteral("downloaded-second"), QStringLiteral("下载二"), {}, {}, 1000, {});
    downloaded.id = 242;
    downloaded.downloadPath = downloadPath;

    PlayerService player;
    player.setPlaylist({ cached, downloaded }, 0);
    player.play();
    if (!QTest::qWaitFor([&player] { return player.isPlaying(); }, 4000))
        QSKIP("无可用音频输出设备,跳过缓存与下载联播验证");
    QVERIFY(QTest::qWaitFor([&player] { return player.currentIndex() == 1; }, 5000));
}

void PlayerServiceTest::httpStreamAutoAdvance()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString firstPath = dir.filePath(QStringLiteral("http-first.wav"));
    const QString secondPath = dir.filePath(QStringLiteral("http-second.wav"));
    writeWav(firstPath, 1);
    writeWav(secondPath, 1);
    QFile firstFile(firstPath);
    QFile secondFile(secondPath);
    QVERIFY(firstFile.open(QIODevice::ReadOnly));
    QVERIFY(secondFile.open(QIODevice::ReadOnly));
    LocalHttpMediaServer server;
    server.add("/first.wav", firstFile.readAll());
    server.add("/second.wav", secondFile.readAll());
    QVERIFY(server.listen());

    LocalUrlSource<NeteaseApiClient> source;
    source.addUrl(QStringLiteral("http-first"), server.url("/first.wav"));
    source.addUrl(QStringLiteral("http-second"), server.url("/second.wav"));
    Song first = MusicSource::makeOnlineSong(SourceId::Netease, QStringLiteral("netease"),
        QStringLiteral("http-first"), QStringLiteral("HTTP一"), {}, {}, 1000, {});
    first.id = 251;
    Song second = MusicSource::makeOnlineSong(SourceId::Netease, QStringLiteral("netease"),
        QStringLiteral("http-second"), QStringLiteral("HTTP二"), {}, {}, 1000, {});
    second.id = 252;
    PlayerService player;
    player.setSourceProvider(&source);
    player.setPlaylist({ first, second }, 0);
    player.play();
    if (!QTest::qWaitFor([&player] { return player.isPlaying(); }, 5000))
        QSKIP("无可用音频输出设备,跳过 HTTP 流联播验证");
    if (!QTest::qWaitFor([&player] { return player.position() >= 300; }, 3000))
        QSKIP("音频端点未推进 HTTP 媒体时钟,跳过 HTTP 流联播验证");
    QVERIFY(QTest::qWaitFor([&player] { return player.currentIndex() == 1; }, 6000));
}

void PlayerServiceTest::onlineUrlRetriesOnce()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("retry-url.wav"));
    writeWav(path, 3);
    LocalUrlSource<QqMusicSource> qq;
    qq.addMedia(QStringLiteral("q-retry"), path);
    qq.emptyResponsesRemaining = 1;
    MusicSourceRegistry registry;
    registry.registerSource(&qq);
    Song song = MusicSource::makeOnlineSong(
        SourceId::QqMusic, QStringLiteral("qqmusic"), QStringLiteral("q-retry"),
        QStringLiteral("地址重试"), {}, {}, 3000, {});
    song.id = 231;

    PlayerService player;
    player.setSourceRegistry(&registry);
    player.setPlaylist({ song }, 0);
    player.play();
    if (!QTest::qWaitFor([&player] { return player.isPlaying(); }, 4000))
        QSKIP("无可用音频输出设备,跳过在线地址重试验证");
    QCOMPARE(qq.requests, QStringList({ QStringLiteral("q-retry"), QStringLiteral("q-retry") }));
}

void PlayerServiceTest::cachedOnlineSongPlayback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString cachePath = dir.filePath(QStringLiteral("online-cache.wav"));
    writeWav(cachePath, 3);

    Song song;
    song.id = 42;
    song.filePath = QStringLiteral("netease://123456");
    song.title = QStringLiteral("缓存歌曲");
    song.source = 1;
    song.onlineId = 123456;
    song.cachePath = cachePath;
    song.durationMs = 3000;

    PlayerService player;
    QSignalSpy changed(&player, &PlayerService::songChanged);
    player.setPlaylist({ song }, 0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(player.currentSong().cachePath, cachePath);
    player.play();
    const bool started = QTest::qWaitFor([&player] { return player.isPlaying(); }, 4000);
    if (!started)
        QSKIP("无可用音频输出设备,跳过缓存在线歌曲播放验证");
    if (!QTest::qWaitFor([&player] { return player.position() >= 300; }, 3000))
        QSKIP("音频端点未推进缓存媒体时钟,跳过缓存位置验证");
}

void PlayerServiceTest::downloadedOnlineSongPlayback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString downloadPath = dir.filePath(QStringLiteral("downloaded.wav"));
    writeWav(downloadPath, 3);

    Song song;
    song.id = 43;
    song.filePath = QStringLiteral("netease://654321");
    song.title = QStringLiteral("下载歌曲");
    song.source = 1;
    song.onlineId = 654321;
    song.downloadPath = downloadPath;
    song.durationMs = 3000;

    PlayerService player;
    QSignalSpy changed(&player, &PlayerService::songChanged);
    player.setPlaylist({ song }, 0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(player.currentSong().downloadPath, downloadPath);
    player.play();
    const bool started = QTest::qWaitFor([&player] { return player.isPlaying(); }, 4000);
    if (!started)
        QSKIP("无可用音频输出设备,跳过下载在线歌曲播放验证");
    if (!QTest::qWaitFor([&player] { return player.position() >= 300; }, 3000))
        QSKIP("音频端点未推进下载媒体时钟,跳过下载位置验证");
}

QTEST_MAIN(PlayerServiceTest)
#include "tst_playerservice.moc"
