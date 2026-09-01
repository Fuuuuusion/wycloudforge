#include "core/LibraryService.h"
#include "core/LyricsLoader.h"
#include "core/MusicSource.h"
#include "core/PlaylistController.h"
#include "core/SearchCache.h"
#include "core/SettingsService.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSqlQuery>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace core;

class PlaylistControllerTest : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void createRenameDelete();
    void addRemoveMoveSongs();
    void favorites();
    void batchTransactions();
    void recentPlays();
    void persistenceAcrossReopen();
    void duplicateAddAndOrphanCleanup();
    void downloadedPersistenceAndCacheIsolation();
    void downloadedLyricsSurviveReload();
    void independentOnlineLyricsSurviveReload();
    void managedDownloadFolderIsNotImported();
    void downloadAssociationsSurviveRestartAndRepair();
    void downloadManifestRestoresMissingOnlineRow();
    void downloadBackupRecoversLegacyOrphan();
    void localAvailabilityClassification();
    void stringRemoteIdentityPersists();
    void managedCacheClearPreservesUserData();

private:
    QTemporaryDir *m_dir = nullptr;
    LibraryService *m_library = nullptr;
    PlaylistController *m_controller = nullptr;
};

void PlaylistControllerTest::init()
{
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("NeteaseClone"));
    QCoreApplication::setApplicationName(QStringLiteral("NeteaseClone"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir->path());
    SettingsService::setOnlineDownloadDir(m_dir->filePath(QStringLiteral("downloads")));
    SettingsService::setRecommendCachePathOverride(
        m_dir->filePath(QStringLiteral("recommend.json")));
    SearchCache::setDefaultRootPathOverride(
        m_dir->filePath(QStringLiteral("search-cache")));
    LibraryService::setDatabasePathOverride(m_dir->filePath(QStringLiteral("t.db")));
    m_library = new LibraryService;
    QVERIFY2(m_library->openDatabase(), qPrintable(m_library->lastError()));
    m_controller = new PlaylistController;
    m_controller->setDatabase(m_library->database());
}

void PlaylistControllerTest::cleanup()
{
    delete m_controller;
    delete m_library;
    delete m_dir;
    LibraryService::setDatabasePathOverride(QString());
    SettingsService::setRecommendCachePathOverride(QString());
    SearchCache::setDefaultRootPathOverride(QString());
}

void PlaylistControllerTest::createRenameDelete()
{
    QCOMPARE(m_controller->playlists().size(), 1); // 我喜欢的音乐
    const int id = m_controller->createPlaylist(QStringLiteral("测试歌单"));
    QVERIFY(id > 1);
    QCOMPARE(m_controller->playlists().size(), 2);
    QVERIFY(m_controller->renamePlaylist(id, QStringLiteral("新名字")));
    QVERIFY(m_controller->playlists().last().name == QStringLiteral("新名字"));
    QVERIFY(m_controller->deletePlaylist(id));
    QCOMPARE(m_controller->playlists().size(), 1);
}

void PlaylistControllerTest::addRemoveMoveSongs()
{
    QSqlQuery q(m_library->database());
    for (int i = 0; i < 3; ++i) {
        q.prepare(QStringLiteral(
            "INSERT INTO songs(path,title,artist,album,duration_ms) VALUES(?,?,?,?,?)"));
        q.addBindValue(QStringLiteral("/tmp/song%1.mp3").arg(i));
        q.addBindValue(QStringLiteral("歌%1").arg(i + 1));
        q.addBindValue(QStringLiteral("歌手"));
        q.addBindValue(QStringLiteral("专辑"));
        q.addBindValue(1000 * (i + 1));
        QVERIFY(q.exec());
    }

    const int pl = m_controller->createPlaylist(QStringLiteral("队列"));
    QVERIFY(m_controller->addSong(pl, 1));
    QVERIFY(m_controller->addSong(pl, 2));
    QVERIFY(m_controller->addSong(pl, 3));
    auto songs = m_controller->songsOf(pl);
    QCOMPARE(songs.size(), 3);
    QCOMPARE(songs[0].title, QStringLiteral("歌1"));

    QVERIFY(m_controller->moveSong(pl, 0, 2));
    songs = m_controller->songsOf(pl);
    QCOMPARE(songs[0].title, QStringLiteral("歌2"));
    QCOMPARE(songs[2].title, QStringLiteral("歌1"));

    QVERIFY(m_controller->removeSong(pl, songs[0].id));
    QCOMPARE(m_controller->songsOf(pl).size(), 2);

    const QString cachePath = m_dir->filePath(QStringLiteral("cached.mp3"));
    QFile cache(cachePath);
    QVERIFY(cache.open(QIODevice::WriteOnly));
    QVERIFY(cache.write("cache") > 0);
    cache.close();
    q.prepare(QStringLiteral(
        "INSERT INTO songs(path,title,artist,album,duration_ms,source,online_id,cover_url,album_id) "
        "VALUES(?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(QStringLiteral("netease://99"));
    q.addBindValue(QStringLiteral("线上歌"));
    q.addBindValue(QStringLiteral("线上歌手"));
    q.addBindValue(QStringLiteral("线上专辑"));
    q.addBindValue(180000);
    q.addBindValue(1);
    q.addBindValue(99);
    q.addBindValue(QStringLiteral("https://example.invalid/cover.jpg"));
    q.addBindValue(1234);
    QVERIFY(q.exec());
    const qint64 onlineId = q.lastInsertId().toLongLong();
    q.prepare(QStringLiteral("INSERT INTO song_cache(song_id,cache_path,size_bytes,last_used_ms) VALUES(?,?,?,?)"));
    q.addBindValue(onlineId);
    q.addBindValue(cachePath);
    q.addBindValue(6);
    q.addBindValue(1);
    QVERIFY(q.exec());
    QVERIFY(m_controller->addSong(pl, onlineId));
    const auto persisted = m_controller->songsOf(pl).last();
    QCOMPARE(persisted.source, 1);
    QCOMPARE(persisted.onlineId, qint64(99));
    QCOMPARE(persisted.cachePath, cachePath);
}

void PlaylistControllerTest::favorites()
{
    QSqlQuery q(m_library->database());
    q.prepare(QStringLiteral("INSERT INTO songs(path,title) VALUES(?,?)"));
    q.addBindValue(QStringLiteral("/tmp/fav.mp3"));
    q.addBindValue(QStringLiteral("收藏歌"));
    QVERIFY(q.exec());
    const qint64 songId = q.lastInsertId().toLongLong();

    QVERIFY(!m_controller->isFavorite(songId));
    m_controller->setFavorite(songId, true);
    QVERIFY(m_controller->isFavorite(songId));
    QCOMPARE(m_controller->songsOf(m_controller->favoritePlaylistId()).size(), 1);
    m_controller->setFavorite(songId, false);
    QVERIFY(!m_controller->isFavorite(songId));
}

void PlaylistControllerTest::batchTransactions()
{
    QList<qint64> ids;
    QSqlQuery insert(m_library->database());
    insert.prepare(QStringLiteral("INSERT INTO songs(path,title) VALUES(?,?)"));
    for (int i = 0; i < 3; ++i) {
        insert.bindValue(0, QStringLiteral("/tmp/batch-%1.mp3").arg(i));
        insert.bindValue(1, QStringLiteral("批量歌曲 %1").arg(i + 1));
        QVERIFY(insert.exec());
        ids.append(insert.lastInsertId().toLongLong());
    }

    const int playlistId = m_controller->createPlaylist(QStringLiteral("批量事务"));
    QVERIFY(playlistId > 1);
    QSignalSpy playlistsChanged(m_controller, &PlaylistController::playlistsChanged);
    QSignalSpy songsChanged(m_controller, &PlaylistController::playlistSongsChanged);

    auto result = m_controller->addSongsBatch(
        playlistId, { ids[0], ids[1], ids[1] });
    QVERIFY(result.success);
    QCOMPARE(result.requested, 3);
    QCOMPARE(result.changed, 2);
    QCOMPARE(result.unchanged, 1);
    QCOMPARE(m_controller->songsOf(playlistId).size(), 2);
    QCOMPARE(playlistsChanged.count(), 0);
    QCOMPARE(songsChanged.count(), 1);

    result = m_controller->addSongsBatch(playlistId, { ids[2], 999999 });
    QVERIFY(!result.success);
    QCOMPARE(result.changed, 0);
    QVERIFY(!m_controller->isInPlaylist(playlistId, ids[2]));

    playlistsChanged.clear();
    songsChanged.clear();
    result = m_controller->removeSongsBatch(
        playlistId, { ids[0], ids[1], ids[1] });
    QVERIFY(result.success);
    QCOMPARE(result.requested, 3);
    QCOMPARE(result.changed, 2);
    QCOMPARE(result.unchanged, 1);
    QCOMPARE(m_controller->songsOf(playlistId).size(), 0);
    QCOMPARE(playlistsChanged.count(), 0);
    QCOMPARE(songsChanged.count(), 1);

    playlistsChanged.clear();
    songsChanged.clear();
    result = m_controller->setFavoritesBatch(ids, true);
    QVERIFY(result.success);
    QCOMPARE(result.changed, 3);
    QCOMPARE(result.unchanged, 0);
    QCOMPARE(m_controller->songsOf(m_controller->favoritePlaylistId()).size(), 3);
    QCOMPARE(playlistsChanged.count(), 0);
    QCOMPARE(songsChanged.count(), 1);

    playlistsChanged.clear();
    songsChanged.clear();
    result = m_controller->setFavoritesBatch({ ids[0], ids[1], ids[1] }, false);
    QVERIFY(result.success);
    QCOMPARE(result.changed, 2);
    QCOMPARE(result.unchanged, 1);
    QVERIFY(!m_controller->isFavorite(ids[0]));
    QVERIFY(!m_controller->isFavorite(ids[1]));
    QVERIFY(m_controller->isFavorite(ids[2]));
    QCOMPARE(playlistsChanged.count(), 0);
    QCOMPARE(songsChanged.count(), 1);
}

void PlaylistControllerTest::recentPlays()
{
    QSqlQuery q(m_library->database());
    q.prepare(QStringLiteral("INSERT INTO songs(path,title) VALUES(?,?)"));
    q.addBindValue(QStringLiteral("/tmp/r1.mp3"));
    q.addBindValue(QStringLiteral("最近一"));
    QVERIFY(q.exec());
    const qint64 id1 = q.lastInsertId().toLongLong();
    q.prepare(QStringLiteral("INSERT INTO songs(path,title) VALUES(?,?)"));
    q.addBindValue(QStringLiteral("/tmp/r2.mp3"));
    q.addBindValue(QStringLiteral("最近二"));
    QVERIFY(q.exec());
    const qint64 id2 = q.lastInsertId().toLongLong();

    m_controller->recordPlay(id1);
    m_controller->recordPlay(id2);
    QSqlQuery verify(m_library->database());
    QVERIFY(verify.exec(QStringLiteral("SELECT COUNT(*) FROM recent")));
    QVERIFY(verify.next());
    QCOMPARE(verify.value(0).toInt(), 2);
    const auto recent = m_controller->recentSongs(10);
    QCOMPARE(recent.size(), 2);
    QCOMPARE(recent[0].title, QStringLiteral("最近二"));
}

void PlaylistControllerTest::persistenceAcrossReopen()
{
    QSqlQuery q(m_library->database());
    q.prepare(QStringLiteral("INSERT INTO songs(path,title) VALUES(?,?)"));
    q.addBindValue(QStringLiteral("/tmp/persist-favorite.mp3"));
    q.addBindValue(QStringLiteral("重启后收藏"));
    QVERIFY(q.exec());
    const qint64 favoriteId = q.lastInsertId().toLongLong();

    q.prepare(QStringLiteral(
        "INSERT INTO songs(path,title,source,online_id) VALUES(?,?,?,?)"));
    q.addBindValue(QStringLiteral("netease://123456"));
    q.addBindValue(QStringLiteral("重启后歌单"));
    q.addBindValue(1);
    q.addBindValue(123456);
    QVERIFY(q.exec());
    const qint64 playlistSongId = q.lastInsertId().toLongLong();

    const int playlistId = m_controller->createPlaylist(QStringLiteral("持久化歌单"));
    QVERIFY(playlistId > 1);
    QVERIFY(m_controller->setFavorite(favoriteId, true));
    QVERIFY(m_controller->addSong(playlistId, playlistSongId));

    // 释放查询对象持有的数据库句柄，模拟应用彻底退出后重新连接。
    q = QSqlQuery();
    delete m_controller;
    m_controller = nullptr;
    delete m_library;
    m_library = nullptr;

    m_library = new LibraryService;
    QVERIFY(m_library->openDatabase());
    m_controller = new PlaylistController;
    m_controller->setDatabase(m_library->database());

    QVERIFY(m_controller->isFavorite(favoriteId));
    QCOMPARE(m_controller->songsOf(m_controller->favoritePlaylistId()).size(), 1);
    const QList<Song> persisted = m_controller->songsOf(playlistId);
    QCOMPARE(persisted.size(), 1);
    QCOMPARE(persisted.first().id, playlistSongId);
    QCOMPARE(persisted.first().source, 1);
    QCOMPARE(persisted.first().onlineId, qint64(123456));
}

void PlaylistControllerTest::duplicateAddAndOrphanCleanup()
{
    QSqlQuery q(m_library->database());
    QVERIFY(q.exec(QStringLiteral("INSERT INTO songs(path,title) VALUES('/tmp/once.mp3','只添加一次')")));
    const qint64 songId = q.lastInsertId().toLongLong();
    const int playlistId = m_controller->createPlaylist(QStringLiteral("去重歌单"));
    QVERIFY(playlistId > 1);
    QVERIFY(m_controller->addSong(playlistId, songId));
    QVERIFY(m_controller->addSong(playlistId, songId));
    QCOMPARE(m_controller->songsOf(playlistId).size(), 1);

    q.prepare(QStringLiteral(
        "INSERT INTO song_cache(song_id,cache_path,size_bytes,last_used_ms) VALUES(?,?,?,?)"));
    q.addBindValue(999999);
    q.addBindValue(m_dir->filePath(QStringLiteral("orphan.mp3")));
    q.addBindValue(1);
    q.addBindValue(1);
    QVERIFY(q.exec());
    q = QSqlQuery();

    delete m_controller;
    m_controller = nullptr;
    delete m_library;
    m_library = nullptr;
    m_library = new LibraryService;
    QVERIFY(m_library->openDatabase());
    m_controller = new PlaylistController;
    m_controller->setDatabase(m_library->database());

    QSqlQuery verify(m_library->database());
    QVERIFY(verify.exec(QStringLiteral("SELECT COUNT(*) FROM song_cache WHERE song_id=999999")));
    QVERIFY(verify.next());
    QCOMPARE(verify.value(0).toInt(), 0);
    QCOMPARE(m_controller->songsOf(playlistId).size(), 1);
}

void PlaylistControllerTest::downloadedPersistenceAndCacheIsolation()
{
    QSqlQuery q(m_library->database());
    q.prepare(QStringLiteral("INSERT INTO songs(path,title,source,online_id) VALUES(?,?,?,?)"));
    q.addBindValue(QStringLiteral("netease://download-test"));
    q.addBindValue(QStringLiteral("永久下载测试"));
    q.addBindValue(1);
    q.addBindValue(8080);
    QVERIFY(q.exec());
    const qint64 songId = q.lastInsertId().toLongLong();
    m_library->reloadDatabase();

    const QString coverPath = m_dir->filePath(QStringLiteral("song-cover.jpg"));
    QFile cover(coverPath);
    QVERIFY(cover.open(QIODevice::WriteOnly));
    QVERIFY(cover.write("cover") > 0);
    cover.close();
    m_library->setSongCoverPath(songId, coverPath);

    const QString downloadPath = m_dir->filePath(QStringLiteral("artist - song.mp3"));
    QFile download(downloadPath);
    QVERIFY(download.open(QIODevice::WriteOnly));
    QVERIFY(download.write("download") > 0);
    download.close();
    QVERIFY(m_library->setSongDownloaded(songId, downloadPath));

    const QString cacheDirectory = m_library->cacheDir();
    QVERIFY2(QDir().mkpath(cacheDirectory), qPrintable(cacheDirectory));
    const QString cachePath = QDir(cacheDirectory).filePath(QStringLiteral("isolation.mp3"));
    QFile cache(cachePath);
    QVERIFY(cache.open(QIODevice::WriteOnly));
    QVERIFY(cache.write("cache") > 0);
    cache.close();
    m_library->setSongCached(songId, cachePath, QFileInfo(cachePath).size());

    Song current = m_library->songById(songId);
    QCOMPARE(current.coverPath, coverPath);
    QCOMPARE(current.downloadPath, QDir::toNativeSeparators(QFileInfo(downloadPath).absoluteFilePath()));
    QVERIFY(current.isDownloaded());
    QVERIFY(current.isCached());

    m_library->clearCache();
    QVERIFY(QFileInfo::exists(downloadPath));
    QVERIFY(!QFileInfo::exists(cachePath));
    current = m_library->songById(songId);
    QCOMPARE(current.coverPath, coverPath);
    QCOMPARE(current.downloadPath, QDir::toNativeSeparators(QFileInfo(downloadPath).absoluteFilePath()));
    QVERIFY(m_library->isSongDownloaded(songId));

    const int playlistId = m_controller->createPlaylist(QStringLiteral("下载关系"));
    QVERIFY(m_controller->addSong(playlistId, songId));
    QVERIFY(m_library->removeSongDownload(songId));
    QVERIFY(!QFileInfo::exists(downloadPath));
    QVERIFY(m_controller->isInPlaylist(playlistId, songId));
}

void PlaylistControllerTest::downloadedLyricsSurviveReload()
{
    Song online;
    online.filePath = QStringLiteral("netease://lyrics-reload");
    online.title = QStringLiteral("重启歌词");
    online.source = 1;
    online.onlineId = 8181;
    const qint64 songId = m_library->upsertOnlineSong(online);
    QVERIFY(songId > 0);

    const QString downloadPath = m_dir->filePath(QStringLiteral("downloaded-with-lyrics.mp3"));
    QFile audio(downloadPath);
    QVERIFY(audio.open(QIODevice::WriteOnly));
    QVERIFY(audio.write("download") > 0);
    audio.close();
    const QString lyricPath = LyricsLoader::sidecarPathFor(downloadPath);
    QFile lyric(lyricPath);
    QVERIFY(lyric.open(QIODevice::WriteOnly));
    QVERIFY(lyric.write("[00:03.00]persisted\n") > 0);
    lyric.close();

    QVERIFY(m_library->setSongDownloaded(songId, downloadPath));
    Song current = m_library->songById(songId);
    QCOMPARE(QDir::cleanPath(current.lyricPath), QDir::cleanPath(lyricPath));
    QCOMPARE(LyricsLoader::load(current).first().text, QStringLiteral("persisted"));

    m_library->reloadDatabase();
    current = m_library->songById(songId);
    QCOMPARE(QDir::cleanPath(current.lyricPath), QDir::cleanPath(lyricPath));
    QCOMPARE(LyricsLoader::load(current).first().text, QStringLiteral("persisted"));
}

void PlaylistControllerTest::independentOnlineLyricsSurviveReload()
{
    Song online = MusicSource::makeOnlineSong(
        SourceId::QqMusic, QStringLiteral("qqmusic"), QStringLiteral("LYRIC_MID"),
        QStringLiteral("独立歌词"), QStringLiteral("测试歌手"), {}, 180000, {});
    const qint64 songId = m_library->upsertOnlineSong(online);
    QVERIFY(songId > 0);
    online.id = songId;
    const QString lyricPath = m_library->lyricCachePathFor(online);
    QVERIFY(!lyricPath.isEmpty());
    Song writable = online;
    writable.lyricPath = lyricPath;
    QVERIFY(LyricsLoader::saveSidecar(writable, QStringLiteral("[00:01.00]cached lyric\n")));
    m_library->setSongLyricPath(songId, lyricPath);

    Song current = m_library->songById(songId);
    QCOMPARE(QDir::cleanPath(current.lyricPath), QDir::cleanPath(lyricPath));
    QCOMPARE(LyricsLoader::load(current).first().text, QStringLiteral("cached lyric"));
    QVERIFY(!current.isLocallyAvailable());

    m_library->reloadDatabase();
    current = m_library->songById(songId);
    QCOMPARE(QDir::cleanPath(current.lyricPath), QDir::cleanPath(lyricPath));
    QCOMPARE(LyricsLoader::load(current).first().text, QStringLiteral("cached lyric"));
    QVERIFY(!current.isLocallyAvailable());

    m_library->clearCache();
    QVERIFY(QFileInfo::exists(lyricPath));
    QCOMPARE(QDir::cleanPath(m_library->songById(songId).lyricPath), QDir::cleanPath(lyricPath));
}

void PlaylistControllerTest::managedDownloadFolderIsNotImported()
{
    QCoreApplication::setOrganizationName(QStringLiteral("NeteaseClone"));
    QCoreApplication::setApplicationName(QStringLiteral("NeteaseClone"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir->path());
    const QString oldDownloadDir = QSettings().value(QStringLiteral("online/downloadDir")).toString();
    const QString musicRoot = m_dir->filePath(QStringLiteral("music"));
    const QString managedDir = QDir(musicRoot).filePath(QStringLiteral("NeteaseClone Downloads"));
    QVERIFY(QDir().mkpath(managedDir));
    SettingsService::setOnlineDownloadDir(managedDir);

    const QString localPath = QDir(musicRoot).filePath(QStringLiteral("local.mp3"));
    QFile local(localPath);
    QVERIFY(local.open(QIODevice::WriteOnly));
    QVERIFY(local.write("local") > 0);
    local.close();

    const QString managedPath = QDir(managedDir).filePath(QStringLiteral("online.mp3"));
    QFile managed(managedPath);
    QVERIFY(managed.open(QIODevice::WriteOnly));
    QVERIFY(managed.write("download") > 0);
    managed.close();

    // 模拟旧版本已经把下载文件错误写成 source=0；扫描完成时也应清理它。
    QSqlQuery insert(m_library->database());
    insert.prepare(QStringLiteral("INSERT INTO songs(path,title) VALUES(?,?)"));
    insert.addBindValue(managedPath);
    insert.addBindValue(QStringLiteral("误导入下载"));
    QVERIFY(insert.exec());

    QSignalSpy scanFinished(m_library, &LibraryService::scanFinished);
    m_library->scanFolderNow(musicRoot);
    QVERIFY(QTest::qWaitFor([&scanFinished] { return scanFinished.count() > 0; }, 5000));

    const QList<Song> songs = m_library->allSongs();
    QCOMPARE(songs.size(), 1);
    QCOMPARE(songs.first().source, 0);
    QCOMPARE(QDir::cleanPath(songs.first().filePath), QDir::cleanPath(localPath));

    if (oldDownloadDir.isEmpty())
        QSettings().remove(QStringLiteral("online/downloadDir"));
    else
        SettingsService::setOnlineDownloadDir(oldDownloadDir);
}

void PlaylistControllerTest::downloadAssociationsSurviveRestartAndRepair()
{
    QCoreApplication::setOrganizationName(QStringLiteral("NeteaseClone"));
    QCoreApplication::setApplicationName(QStringLiteral("NeteaseClone"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir->path());
    const QString oldDownloadDir = QSettings().value(QStringLiteral("online/downloadDir")).toString();
    const QString managedDir = m_dir->filePath(QStringLiteral("NeteaseClone Downloads"));
    QVERIFY(QDir().mkpath(managedDir));
    SettingsService::setOnlineDownloadDir(managedDir);

    Song online;
    online.filePath = QStringLiteral("netease://8001");
    online.title = QStringLiteral("恢复下载");
    online.artist = QStringLiteral("歌手/组合");
    online.source = 1;
    online.onlineId = 8001;
    const qint64 onlineId = m_library->upsertOnlineSong(online);
    QVERIFY(onlineId > 0);

    const QString downloadPath = QDir(managedDir).filePath(
        QStringLiteral("歌手_组合 - 恢复下载.mp3"));
    QFile download(downloadPath);
    QVERIFY(download.open(QIODevice::WriteOnly));
    QVERIFY(download.write("download") > 0);
    download.close();

    // 旧库 download_path 为空时，启动重载应按下载命名规则重新关联现有文件。
    m_library->reloadDatabase();
    Song restored = m_library->songById(onlineId);
    QCOMPARE(QDir::cleanPath(restored.downloadPath), QDir::cleanPath(downloadPath));
    QVERIFY(restored.isDownloaded());

    // 旧版本误导入的 source=0 副本被清理前，其歌单关系应迁移到在线记录。
    QSqlQuery insert(m_library->database());
    insert.prepare(QStringLiteral("INSERT INTO songs(path,title,artist) VALUES(?,?,?)"));
    insert.addBindValue(downloadPath);
    insert.addBindValue(QStringLiteral("恢复下载"));
    insert.addBindValue(QStringLiteral("歌手/组合"));
    QVERIFY(insert.exec());
    const qint64 importedId = insert.lastInsertId().toLongLong();
    const int playlistId = m_controller->createPlaylist(QStringLiteral("下载迁移"));
    QVERIFY(m_controller->addSong(playlistId, importedId));
    m_library->reloadDatabase();
    QVERIFY(m_library->songById(importedId).id <= 0);
    QVERIFY(m_controller->isInPlaylist(playlistId, onlineId));

    // 临时不可访问只改变下载状态，不能永久清空数据库里的关联路径。
    QVERIFY(QFile::remove(downloadPath));
    m_library->reloadDatabase();
    restored = m_library->songById(onlineId);
    QCOMPARE(QDir::cleanPath(restored.downloadPath), QDir::cleanPath(downloadPath));
    QVERIFY(!restored.isDownloaded());

    QVERIFY(download.open(QIODevice::WriteOnly));
    QVERIFY(download.write("download-again") > 0);
    download.close();
    m_library->reloadDatabase();
    restored = m_library->songById(onlineId);
    QCOMPARE(QDir::cleanPath(restored.downloadPath), QDir::cleanPath(downloadPath));
    QVERIFY(restored.isDownloaded());

    delete m_controller;
    m_controller = nullptr;
    delete m_library;
    m_library = new LibraryService;
    QVERIFY(m_library->openDatabase());
    m_controller = new PlaylistController;
    m_controller->setDatabase(m_library->database());
    restored = m_library->songById(onlineId);
    QCOMPARE(QDir::cleanPath(restored.downloadPath), QDir::cleanPath(downloadPath));
    QVERIFY(restored.isDownloaded());
    QVERIFY(m_controller->isInPlaylist(playlistId, onlineId));

    if (oldDownloadDir.isEmpty())
        QSettings().remove(QStringLiteral("online/downloadDir"));
    else
        SettingsService::setOnlineDownloadDir(oldDownloadDir);
}

void PlaylistControllerTest::downloadManifestRestoresMissingOnlineRow()
{
    const QString managedDir = m_dir->filePath(QStringLiteral("manifest-downloads"));
    QVERIFY(QDir().mkpath(managedDir));
    SettingsService::setOnlineDownloadDir(managedDir);

    const QString coverPath = m_dir->filePath(QStringLiteral("qq-cover.jpg"));
    QFile cover(coverPath);
    QVERIFY(cover.open(QIODevice::WriteOnly));
    QVERIFY(cover.write("cover") > 0);
    cover.close();

    Song qq;
    qq.filePath = QStringLiteral("qqmusic://004NQRUH4anAYS");
    qq.title = QStringLiteral("了解");
    qq.artist = QStringLiteral("孙燕姿");
    qq.album = QStringLiteral("未完成");
    qq.durationMs = 286640;
    qq.coverPath = coverPath;
    qq.source = int(SourceId::QqMusic);
    qq.remoteId = QStringLiteral("004NQRUH4anAYS");
    qq.mediaRemoteId = QStringLiteral("C400004NQRUH4anAYS");
    qq.albumRemoteId = QStringLiteral("000-album-mid");
    qq.artistRemoteId = QStringLiteral("000-artist-mid");
    const qint64 qqId = m_library->upsertOnlineSong(qq);
    QVERIFY(qqId > 0);

    Song sameName = qq;
    sameName.filePath = QStringLiteral("netease://287412");
    sameName.source = int(SourceId::Netease);
    sameName.remoteId = QStringLiteral("287412");
    sameName.onlineId = 287412;
    sameName.albumRemoteId = QStringLiteral("28539");
    sameName.albumId = 28539;
    const qint64 neteaseId = m_library->upsertOnlineSong(sameName);
    QVERIFY(neteaseId > 0);

    const QString downloadPath = QDir(managedDir).filePath(QStringLiteral("孙燕姿 - 了解.mp3"));
    QFile audio(downloadPath);
    QVERIFY(audio.open(QIODevice::WriteOnly));
    QVERIFY(audio.write("downloaded-audio") > 0);
    audio.close();
    QVERIFY(m_library->setSongDownloaded(qqId, downloadPath));

    const QString manifestPath = QDir(managedDir).filePath(
        QStringLiteral(".wycloudforge-downloads.json"));
    QFile manifest(manifestPath);
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    const QByteArray manifestData = manifest.readAll();
    QVERIFY(manifestData.contains("004NQRUH4anAYS"));
    QVERIFY(manifestData.contains("C400004NQRUH4anAYS"));
    manifest.close();

    QSqlQuery remove(m_library->database());
    remove.prepare(QStringLiteral("DELETE FROM songs WHERE id=?"));
    remove.addBindValue(qqId);
    QVERIFY(remove.exec());
    m_library->reloadDatabase();

    const Song restored = m_library->songByRemoteId(int(SourceId::QqMusic), qq.remoteId);
    QVERIFY(restored.id > 0);
    QCOMPARE(restored.title, qq.title);
    QCOMPARE(restored.album, qq.album);
    QCOMPARE(restored.albumRemoteId, qq.albumRemoteId);
    QCOMPARE(restored.mediaRemoteId, qq.mediaRemoteId);
    QCOMPARE(restored.coverPath, coverPath);
    QCOMPARE(QDir::cleanPath(restored.downloadPath), QDir::cleanPath(downloadPath));
    QVERIFY(restored.isDownloaded());
    QVERIFY(!m_library->songById(neteaseId).isDownloaded());
}

void PlaylistControllerTest::downloadBackupRecoversLegacyOrphan()
{
    const QString managedDir = m_dir->filePath(QStringLiteral("backup-downloads"));
    QVERIFY(QDir().mkpath(managedDir));
    SettingsService::setOnlineDownloadDir(managedDir);
    const QString downloadPath = QDir(managedDir).filePath(
        QStringLiteral("备份歌手 - 备份下载.mp3"));
    QFile audio(downloadPath);
    QVERIFY(audio.open(QIODevice::WriteOnly));
    QVERIFY(audio.write("legacy-download") > 0);
    audio.close();

    const QString backupDir = QFileInfo(m_library->databasePath()).absolutePath()
        + QStringLiteral("/db-backups/automatic-20990101-000000-000");
    QVERIFY(QDir().mkpath(backupDir));
    const QString backupPath = QDir(backupDir).filePath(
        QFileInfo(m_library->databasePath()).fileName());
    const QString connectionName = QStringLiteral("legacy_download_backup");
    {
        QSqlDatabase backup = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        backup.setDatabaseName(backupPath);
        QVERIFY(backup.open());
        QSqlQuery create(backup);
        QVERIFY(create.exec(QStringLiteral(
            "CREATE TABLE songs(id INTEGER PRIMARY KEY,path TEXT,title TEXT,artist TEXT,album TEXT,"
            "duration_ms INTEGER,cover_path TEXT,source INTEGER,remote_id TEXT,online_id INTEGER,"
            "cover_url TEXT,album_remote_id TEXT,album_id INTEGER,artist_remote_id TEXT,download_path TEXT)")));
        QSqlQuery insert(backup);
        insert.prepare(QStringLiteral(
            "INSERT INTO songs(path,title,artist,album,duration_ms,cover_path,source,remote_id,"
            "online_id,cover_url,album_remote_id,album_id,artist_remote_id,download_path) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
        insert.addBindValue(QStringLiteral("qqmusic://legacy-mid"));
        insert.addBindValue(QStringLiteral("备份下载"));
        insert.addBindValue(QStringLiteral("备份歌手"));
        insert.addBindValue(QStringLiteral("备份专辑"));
        insert.addBindValue(123000);
        insert.addBindValue(QStringLiteral("C:/covers/legacy.jpg"));
        insert.addBindValue(int(SourceId::QqMusic));
        insert.addBindValue(QStringLiteral("legacy-mid"));
        insert.addBindValue(0);
        insert.addBindValue(QStringLiteral("https://example.invalid/legacy.jpg"));
        insert.addBindValue(QStringLiteral("legacy-album"));
        insert.addBindValue(0);
        insert.addBindValue(QStringLiteral("legacy-artist"));
        insert.addBindValue(downloadPath);
        QVERIFY(insert.exec());
        backup.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    m_library->reloadDatabase();
    const Song restored = m_library->songByRemoteId(int(SourceId::QqMusic),
                                                    QStringLiteral("legacy-mid"));
    QVERIFY(restored.id > 0);
    QVERIFY(restored.isDownloaded());
    QCOMPARE(restored.title, QStringLiteral("备份下载"));
    QCOMPARE(restored.albumRemoteId, QStringLiteral("legacy-album"));

    QFile manifest(QDir(managedDir).filePath(QStringLiteral(".wycloudforge-downloads.json")));
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    QVERIFY(manifest.readAll().contains("legacy-mid"));
}

void PlaylistControllerTest::localAvailabilityClassification()
{
    Song browsed;
    browsed.source = 1;
    browsed.onlineId = 1001;
    QVERIFY(!browsed.isCached());
    QVERIFY(!browsed.isDownloaded());
    QVERIFY(!browsed.isLocallyAvailable());

    const QString cachePath = m_dir->filePath(QStringLiteral("available-cache.mp3"));
    QFile cache(cachePath);
    QVERIFY(cache.open(QIODevice::WriteOnly));
    QVERIFY(cache.write("cache") > 0);
    cache.close();
    browsed.cachePath = cachePath;
    QVERIFY(browsed.isCached());
    QVERIFY(browsed.isLocallyAvailable());

    Song downloaded;
    downloaded.source = 1;
    downloaded.onlineId = 1002;
    const QString downloadPath = m_dir->filePath(QStringLiteral("available-download.mp3"));
    QFile download(downloadPath);
    QVERIFY(download.open(QIODevice::WriteOnly));
    QVERIFY(download.write("download") > 0);
    download.close();
    downloaded.downloadPath = downloadPath;
    QVERIFY(downloaded.isDownloaded());
    QVERIFY(downloaded.isLocallyAvailable());

    Song stale = browsed;
    stale.cachePath = m_dir->filePath(QStringLiteral("missing-cache.mp3"));
    QVERIFY(!stale.isCached());
    QVERIFY(!stale.isLocallyAvailable());

    Song local;
    local.source = 0;
    local.missing = true;
    QVERIFY(local.isLocallyAvailable());
}

void PlaylistControllerTest::stringRemoteIdentityPersists()
{
    Song qq;
    qq.source = int(SourceId::QqMusic);
    qq.remoteId = QStringLiteral("0039MnYb0qxYhV");
    qq.mediaRemoteId = QStringLiteral("C4000039MnYb0qxYhV");
    qq.albumRemoteId = QStringLiteral("004DABuD2r4V8n");
    qq.artistRemoteId = QStringLiteral("0025NhlN2yWrP4");
    qq.filePath = QStringLiteral("qqmusic://0039MnYb0qxYhV");
    qq.title = QStringLiteral("字符串身份");
    qq.artist = QStringLiteral("QQ 歌手");
    const qint64 songId = m_library->upsertOnlineSong(qq);
    QVERIFY(songId > 0);

    Song updated = qq;
    updated.title = QStringLiteral("字符串身份（更新）");
    QCOMPARE(m_library->upsertOnlineSong(updated), songId);
    QCOMPARE(m_library->songByRemoteId(int(SourceId::QqMusic), qq.remoteId).id, songId);

    const int playlistId = m_controller->createPlaylist(QStringLiteral("QQ 字符串歌单"));
    QVERIFY(m_controller->addSong(playlistId, songId));

    delete m_controller;
    m_controller = nullptr;
    delete m_library;
    m_library = new LibraryService;
    QVERIFY(m_library->openDatabase());
    m_controller = new PlaylistController;
    m_controller->setDatabase(m_library->database());

    const Song restored = m_library->songByRemoteId(int(SourceId::QqMusic), qq.remoteId);
    QCOMPARE(restored.id, songId);
    QCOMPARE(restored.title, updated.title);
    QCOMPARE(restored.remoteId, qq.remoteId);
    QCOMPARE(restored.mediaRemoteId, qq.mediaRemoteId);
    QCOMPARE(restored.albumRemoteId, qq.albumRemoteId);
    QCOMPARE(restored.artistRemoteId, qq.artistRemoteId);
    QCOMPARE(restored.onlineId, qint64(0));
    const Song playlistSong = m_controller->songsOf(playlistId).first();
    QCOMPARE(playlistSong.stableIdentity(), QStringLiteral("2:0039MnYb0qxYhV"));
    QCOMPARE(playlistSong.mediaRemoteId, qq.mediaRemoteId);
}

void PlaylistControllerTest::managedCacheClearPreservesUserData()
{
    const auto writeFile = [](const QString &path, const QByteArray &contents) {
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            return false;
        QFile file(path);
        return file.open(QIODevice::WriteOnly)
            && file.write(contents) == contents.size();
    };
    const auto addOnlineSong = [this](SourceId source, const QString &remoteId,
                                      const QString &title) {
        Song song;
        song.source = int(source);
        song.remoteId = remoteId;
        song.onlineId = source == SourceId::Netease ? remoteId.toLongLong() : 0;
        song.filePath = (source == SourceId::Netease
                             ? QStringLiteral("netease://")
                             : QStringLiteral("qqmusic://")) + remoteId;
        song.title = title;
        song.artist = QStringLiteral("缓存测试歌手");
        song.album = QStringLiteral("缓存测试专辑");
        song.durationMs = 180000;
        return m_library->upsertOnlineSong(song);
    };

    const qint64 transientId = addOnlineSong(
        SourceId::Netease, QStringLiteral("910001"), QStringLiteral("纯浏览记录"));
    const qint64 favoriteId = addOnlineSong(
        SourceId::Netease, QStringLiteral("910002"), QStringLiteral("收藏记录"));
    const qint64 playlistSongId = addOnlineSong(
        SourceId::QqMusic, QStringLiteral("CACHE_PLAYLIST_MID"), QStringLiteral("歌单记录"));
    const qint64 recentId = addOnlineSong(
        SourceId::Netease, QStringLiteral("910004"), QStringLiteral("播放历史记录"));
    const qint64 lyricId = addOnlineSong(
        SourceId::QqMusic, QStringLiteral("CACHE_LYRIC_MID"), QStringLiteral("歌词记录"));
    const qint64 queueId = addOnlineSong(
        SourceId::Netease, QStringLiteral("910006"), QStringLiteral("当前队列记录"));
    const qint64 downloadedId = addOnlineSong(
        SourceId::QqMusic, QStringLiteral("CACHE_DOWNLOAD_MID"), QStringLiteral("永久下载记录"));
    QVERIFY(transientId > 0);
    QVERIFY(favoriteId > 0);
    QVERIFY(playlistSongId > 0);
    QVERIFY(recentId > 0);
    QVERIFY(lyricId > 0);
    QVERIFY(queueId > 0);
    QVERIFY(downloadedId > 0);

    QVERIFY(m_controller->setFavorite(favoriteId, true));
    const int playlistId = m_controller->createPlaylist(QStringLiteral("缓存保护歌单"));
    QVERIFY(playlistId > 1);
    QVERIFY(m_controller->addSong(playlistId, playlistSongId));
    m_library->markPlayed(recentId);
    m_controller->recordPlay(recentId);

    const QString lyricPath = m_library->lyricCachePathFor(m_library->songById(lyricId));
    QVERIFY(writeFile(lyricPath, QByteArrayLiteral("[00:01.00]must survive\n")));
    m_library->setSongLyricPath(lyricId, lyricPath);

    const QString playbackPath = m_library->cacheFilePathFor(m_library->songById(queueId));
    QVERIFY(writeFile(playbackPath, QByteArrayLiteral("temporary audio")));
    const QString legacyPlaybackLyric = LyricsLoader::sidecarPathFor(playbackPath);
    const QString preservedPlaybackLyric = m_library->lyricCachePathFor(
        m_library->songById(queueId));
    QVERIFY(writeFile(legacyPlaybackLyric,
                      QByteArrayLiteral("[00:02.00]legacy cache lyric\n")));
    m_library->setSongCached(queueId, playbackPath, QFileInfo(playbackPath).size());

    const QString transientCover = m_library->songCoverCachePath(
        m_library->songById(transientId));
    const QString favoriteCover = m_library->songCoverCachePath(
        m_library->songById(favoriteId));
    const QString queueCover = m_library->songCoverCachePath(m_library->songById(queueId));
    QVERIFY(writeFile(transientCover, QByteArrayLiteral("transient cover")));
    QVERIFY(writeFile(favoriteCover, QByteArrayLiteral("favorite cover")));
    QVERIFY(writeFile(queueCover, QByteArrayLiteral("queue cover")));
    m_library->setSongCoverPath(transientId, transientCover);
    m_library->setSongCoverPath(favoriteId, favoriteCover);
    m_library->setSongCoverPath(queueId, queueCover);

    const QString downloadedCover = m_library->songCoverCachePath(
        m_library->songById(downloadedId));
    QVERIFY(writeFile(downloadedCover, QByteArrayLiteral("downloaded cover")));
    m_library->setSongCoverPath(downloadedId, downloadedCover);
    const QString downloadedAudio = QDir(m_library->downloadDir()).filePath(
        QStringLiteral("缓存测试歌手 - 永久下载记录.mp3"));
    QVERIFY(writeFile(downloadedAudio, QByteArrayLiteral("permanent audio")));
    QVERIFY(m_library->setSongDownloaded(downloadedId, downloadedAudio));

    const QString localAudio = m_dir->filePath(QStringLiteral("local-song.mp3"));
    const QString localCover = m_library->coverCacheDir()
        + QStringLiteral("/local-import-cover.jpg");
    QVERIFY(writeFile(localAudio, QByteArrayLiteral("local audio")));
    QVERIFY(writeFile(localCover, QByteArrayLiteral("local cover")));
    QSqlQuery insertLocal(m_library->database());
    insertLocal.prepare(QStringLiteral(
        "INSERT INTO songs(path,title,source,cover_path,has_cover) VALUES(?,?,0,?,1)"));
    insertLocal.addBindValue(localAudio);
    insertLocal.addBindValue(QStringLiteral("本地歌曲"));
    insertLocal.addBindValue(localCover);
    QVERIFY(insertLocal.exec());
    const qint64 localId = insertLocal.lastInsertId().toLongLong();

    const QString playlistCover = m_library->playlistCoverCachePath(
        SourceId::Netease, QStringLiteral("910099"));
    QVERIFY(writeFile(playlistCover, QByteArrayLiteral("user playlist cover")));
    QVERIFY(m_controller->setPlaylistCover(playlistId, playlistCover));
    const QString recommendationCover = m_library->playlistCoverCachePath(
        SourceId::QqMusic, QStringLiteral("CACHE_RECOMMEND_PLAYLIST"));
    QVERIFY(writeFile(recommendationCover, QByteArrayLiteral("recommend cover")));

    const QString recommendPath = SettingsService::recommendCachePath();
    const QString qqRecommendPath = QFileInfo(recommendPath).absolutePath()
        + QStringLiteral("/recommend-qq.json");
    QVERIFY(writeFile(recommendPath, QByteArrayLiteral("{\"source\":\"netease\"}")));
    QVERIFY(writeFile(qqRecommendPath, QByteArrayLiteral("{\"source\":\"qq\"}")));
    const QString searchRoot = m_dir->filePath(QStringLiteral("search-cache"));
    QVERIFY(writeFile(QDir(searchRoot).filePath(QStringLiteral("results/result.json")),
                      QByteArrayLiteral("search result")));
    QVERIFY(writeFile(QDir(searchRoot).filePath(QStringLiteral("suggestions/suggestion.json")),
                      QByteArrayLiteral("search suggestion")));
    QVERIFY(writeFile(QDir(searchRoot).filePath(QStringLiteral("hot/hot.json")),
                      QByteArrayLiteral("hot terms")));
    SearchCache searchCache;
    searchCache.addHistory(QStringLiteral("孙燕姿"));
    QCOMPARE(searchCache.history(), QStringList{ QStringLiteral("孙燕姿") });

    m_library->reloadDatabase();
    const CacheUsageBreakdown before = m_library->cacheUsageDetailed({ queueId });
    QCOMPARE(before.playbackSongs, 1);
    QVERIFY(before.coverFiles >= 4);
    QCOMPARE(before.responseFiles, 5);
    QCOMPARE(before.transientOnlineSongs, 1);

    const CacheClearResult result = m_library->clearCache({ queueId });
    QVERIFY2(result.complete(), qPrintable(result.failures.join(QLatin1Char('\n'))));
    QCOMPARE(result.playbackSongs, 1);
    QVERIFY(result.coverFiles >= 4);
    QCOMPARE(result.responseFiles, 5);
    QCOMPARE(result.transientOnlineSongs, 1);

    QVERIFY(m_library->songById(transientId).id <= 0);
    QVERIFY(m_library->songById(favoriteId).id > 0);
    QVERIFY(m_controller->isFavorite(favoriteId));
    QVERIFY(m_library->songById(playlistSongId).id > 0);
    QVERIFY(m_controller->isInPlaylist(playlistId, playlistSongId));
    QVERIFY(m_library->songById(recentId).id > 0);
    QCOMPARE(m_controller->recentSongs(10).first().id, recentId);
    QVERIFY(m_library->songById(lyricId).id > 0);
    QVERIFY(QFileInfo(lyricPath).isFile());
    QCOMPARE(LyricsLoader::load(m_library->songById(lyricId)).first().text,
             QStringLiteral("must survive"));
    QVERIFY(m_library->songById(queueId).id > 0);
    QVERIFY(!QFileInfo::exists(playbackPath));
    QVERIFY(!m_library->songById(queueId).isCached());
    QVERIFY(!QFileInfo::exists(legacyPlaybackLyric));
    QVERIFY(QFileInfo(preservedPlaybackLyric).isFile());
    QCOMPARE(LyricsLoader::load(m_library->songById(queueId)).first().text,
             QStringLiteral("legacy cache lyric"));

    const Song downloaded = m_library->songById(downloadedId);
    QVERIFY(downloaded.id > 0);
    QVERIFY(downloaded.isDownloaded());
    QVERIFY(QFileInfo(downloadedAudio).isFile());
    QCOMPARE(QDir::cleanPath(downloaded.coverPath), QDir::cleanPath(downloadedCover));
    QVERIFY(QFileInfo(downloadedCover).isFile());
    const Song local = m_library->songById(localId);
    QVERIFY(local.id > 0);
    QCOMPARE(QDir::cleanPath(local.coverPath), QDir::cleanPath(localCover));
    QVERIFY(QFileInfo(localCover).isFile());
    QVERIFY(QFileInfo(playlistCover).isFile());

    QVERIFY(!QFileInfo::exists(transientCover));
    QVERIFY(!QFileInfo::exists(favoriteCover));
    QVERIFY(!QFileInfo::exists(queueCover));
    QVERIFY(!QFileInfo::exists(recommendationCover));
    QVERIFY(!QFileInfo::exists(recommendPath));
    QVERIFY(!QFileInfo::exists(qqRecommendPath));
    QVERIFY(!QFileInfo::exists(QDir(searchRoot).filePath(
        QStringLiteral("results/result.json"))));
    QVERIFY(!QFileInfo::exists(QDir(searchRoot).filePath(
        QStringLiteral("suggestions/suggestion.json"))));
    QVERIFY(!QFileInfo::exists(QDir(searchRoot).filePath(QStringLiteral("hot/hot.json"))));
    QCOMPARE(searchCache.history(), QStringList{ QStringLiteral("孙燕姿") });

    const CacheUsageBreakdown after = m_library->cacheUsageDetailed({ queueId });
    QCOMPARE(after.totalBytes(), qint64(0));
    QCOMPARE(after.playbackSongs, 0);
    QCOMPARE(after.coverFiles, 0);
    QCOMPARE(after.responseFiles, 0);
    QCOMPARE(after.transientOnlineSongs, 0);
}

QTEST_MAIN(PlaylistControllerTest)
#include "tst_playlistcontroller.moc"
