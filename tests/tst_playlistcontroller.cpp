#include "core/LibraryService.h"
#include "core/PlaylistController.h"
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
    void recentPlays();
    void persistenceAcrossReopen();
    void duplicateAddAndOrphanCleanup();
    void downloadedPersistenceAndCacheIsolation();
    void managedDownloadFolderIsNotImported();
    void downloadAssociationsSurviveRestartAndRepair();
    void localAvailabilityClassification();

private:
    QTemporaryDir *m_dir = nullptr;
    LibraryService *m_library = nullptr;
    PlaylistController *m_controller = nullptr;
};

void PlaylistControllerTest::init()
{
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    LibraryService::setDatabasePathOverride(m_dir->filePath(QStringLiteral("t.db")));
    m_library = new LibraryService;
    QVERIFY(m_library->openDatabase());
    m_controller = new PlaylistController;
    m_controller->setDatabase(m_library->database());
}

void PlaylistControllerTest::cleanup()
{
    delete m_controller;
    delete m_library;
    delete m_dir;
    LibraryService::setDatabasePathOverride(QString());
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

QTEST_MAIN(PlaylistControllerTest)
#include "tst_playlistcontroller.moc"
