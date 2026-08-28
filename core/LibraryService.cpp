#include "LibraryService.h"

#include "core/SettingsService.h"
#include "core/LyricsLoader.h"
#include "core/TagReader.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QVariant>

namespace core {
namespace {

const QStringList kSupportedSuffixes = { QStringLiteral("mp3"), QStringLiteral("flac"),
                                         QStringLiteral("wav"), QStringLiteral("m4a"),
                                         QStringLiteral("aac"), QStringLiteral("ogg"),
                                         QStringLiteral("mgg") };

bool isSupportedFile(const QString &path)
{
    return kSupportedSuffixes.contains(QFileInfo(path).suffix().toLower());
}

QString coverCacheDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/covers");
    QDir().mkpath(dir);
    return dir;
}

class ScanWorker : public QObject
{
    Q_OBJECT
public slots:
    void run(const QStringList &folders, const QString &dbPath)
    {
        int added = 0;
        int removed = 0;
        QStringList watchDirs;

        {
            const QString conn = QStringLiteral("scan_%1").arg(quintptr(QThread::currentThread()));
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(dbPath);
            if (db.open()) {
                QSqlQuery q(db);
                q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));

                int total = 0;
                for (const QString &folder : folders) {
                    QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
                    while (it.hasNext()) {
                        it.next();
                        if (isSupportedFile(it.filePath()))
                            ++total;
                    }
                }
                emit progress(0, qMax(1, total));

                int done = 0;
                for (const QString &folder : folders) {
                    QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
                    while (it.hasNext()) {
                        const QString path = it.next();
                        if (!isSupportedFile(path))
                            continue;
                        const TagInfo info = TagReader::read(path);
                        QString coverPath;
                        if (info.hasCover()) {
                            const QByteArray hash = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex();
                            const QString suffix = info.coverData.startsWith("\xFF\xD8") ? QStringLiteral("jpg")
                                : info.coverData.startsWith("\x89PNG") ? QStringLiteral("png") : QStringLiteral("jpg");
                            coverPath = coverCacheDir() + QLatin1Char('/') + QString::fromLatin1(hash) + QLatin1Char('.') + suffix;
                            if (!QFile::exists(coverPath)) {
                                QFile f(coverPath);
                                if (f.open(QIODevice::WriteOnly)) {
                                    f.write(info.coverData);
                                    f.close();
                                }
                            }
                        }
                        QSqlQuery ins(db);
                        ins.prepare(QStringLiteral(
                            "INSERT INTO songs(path,title,artist,album,duration_ms,cover_path,has_cover,missing) "
                            "VALUES(?,?,?,?,?,?,?,0) "
                            "ON CONFLICT(path) DO UPDATE SET title=excluded.title,artist=excluded.artist,"
                            "album=excluded.album,duration_ms=excluded.duration_ms,"
                            // 保留此前从网易云获取的封面;文件本身有内嵌封面时则以新封面为准。
                            "cover_path=CASE WHEN excluded.cover_path <> '' THEN excluded.cover_path ELSE songs.cover_path END,"
                            "has_cover=CASE WHEN excluded.cover_path <> '' THEN excluded.has_cover ELSE songs.has_cover END,missing=0"));
                        ins.addBindValue(path);
                        ins.addBindValue(info.title);
                        ins.addBindValue(info.artist);
                        ins.addBindValue(info.album);
                        ins.addBindValue(info.durationMs);
                        ins.addBindValue(coverPath);
                        ins.addBindValue(info.hasCover() ? 1 : 0);
                        if (ins.exec())
                            ++added;
                        ++done;
                        if (done % 25 == 0)
                            emit progress(done, qMax(1, total));
                    }
                }

                QSqlQuery select(db);
                select.exec(QStringLiteral("SELECT id,path,missing FROM songs"));
                QSqlQuery mark(db);
                mark.prepare(QStringLiteral("UPDATE songs SET missing=? WHERE id=?"));
                while (select.next()) {
                    const qint64 id = select.value(0).toLongLong();
                    const QString path = select.value(1).toString();
                    const bool wasMissing = select.value(2).toInt() != 0;
                    const bool exists = QFile::exists(path);
                    if (!wasMissing && !exists) {
                        mark.addBindValue(1);
                        mark.addBindValue(id);
                        mark.exec();
                        ++removed;
                    }
                }

                for (const QString &folder : folders) {
                    QDirIterator it(folder, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                    watchDirs << folder;
                    while (it.hasNext() && watchDirs.size() < 4000)
                        watchDirs << it.next();
                }
                db.close();
            }
            QSqlDatabase::removeDatabase(conn);
        }
        emit finished(added, removed, watchDirs);
    }

signals:
    void progress(int done, int total);
    void finished(int added, int removed, const QStringList &watchDirs);
};

} // namespace

QString LibraryService::s_dbOverride;

void LibraryService::setDatabasePathOverride(const QString &path)
{
    s_dbOverride = path;
}

LibraryService::LibraryService(QObject *parent)
    : QObject(parent)
{
    m_watcher = new QFileSystemWatcher(this);
    m_watchTimer = new QTimer(this);
    m_watchTimer->setSingleShot(true);
    m_watchTimer->setInterval(600);
    connect(m_watchTimer, &QTimer::timeout, this, [this] {
        if (!m_pendingFolders.isEmpty()) {
            const QStringList folders = m_pendingFolders;
            m_pendingFolders.clear();
            startWorker(folders);
        }
    });
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &LibraryService::onWatchChange);
}

LibraryService::~LibraryService()
{
    if (m_scanThread && m_scanThread->isRunning()) {
        m_scanThread->quit();
        m_scanThread->wait(3000);
    }
}

QString LibraryService::lastError() const
{
    return m_db.isValid() ? m_db.lastError().text() : QStringLiteral("invalid db handle");
}

bool LibraryService::openDatabase()
{
    m_dbPath = s_dbOverride.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/library.db")
        : s_dbOverride;
    QDir().mkpath(QFileInfo(m_dbPath).absolutePath());
    if (QSqlDatabase::contains(QStringLiteral("main")))
        QSqlDatabase::removeDatabase(QStringLiteral("main"));
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("main"));
    m_db.setDatabaseName(m_dbPath);
    if (!m_db.open())
        return false;

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS songs("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "path TEXT NOT NULL UNIQUE,"
        "title TEXT DEFAULT '',"
        "artist TEXT DEFAULT '',"
        "album TEXT DEFAULT '',"
        "duration_ms INTEGER DEFAULT 0,"
        "cover_path TEXT DEFAULT '',"
        "has_cover INTEGER DEFAULT 0,"
        "missing INTEGER DEFAULT 0,"
        "play_count INTEGER DEFAULT 0,"
        "last_played_ms INTEGER DEFAULT 0)"));
    // 多源迁移:老库补充列
    QStringList cols;
    {
        QSqlQuery info(m_db);
        info.exec(QStringLiteral("PRAGMA table_info(songs)"));
        while (info.next())
            cols << info.value(1).toString();
    }
    const auto addCol = [this](const QString &name, const QString &def) {
        QSqlQuery a(m_db);
        a.exec(QStringLiteral("ALTER TABLE songs ADD COLUMN %1 %2").arg(name, def));
    };
    if (!cols.contains(QStringLiteral("source"))) addCol(QStringLiteral("source"), QStringLiteral("INTEGER DEFAULT 0"));
    if (!cols.contains(QStringLiteral("online_id"))) addCol(QStringLiteral("online_id"), QStringLiteral("INTEGER DEFAULT 0"));
    if (!cols.contains(QStringLiteral("cover_url"))) addCol(QStringLiteral("cover_url"), QStringLiteral("TEXT DEFAULT ''"));
    if (!cols.contains(QStringLiteral("cache_path"))) addCol(QStringLiteral("cache_path"), QStringLiteral("TEXT DEFAULT ''"));
    if (!cols.contains(QStringLiteral("album_id"))) addCol(QStringLiteral("album_id"), QStringLiteral("INTEGER DEFAULT 0"));

    // 歌单表:老库补充封面/简介列
    {
        QStringList pcols;
        QSqlQuery pinfo(m_db);
        pinfo.exec(QStringLiteral("PRAGMA table_info(playlists)"));
        while (pinfo.next())
            pcols << pinfo.value(1).toString();
        if (!pcols.contains(QStringLiteral("cover_path"))) {
            QSqlQuery a(m_db);
            a.exec(QStringLiteral("ALTER TABLE playlists ADD COLUMN cover_path TEXT DEFAULT ''"));
        }
        if (!pcols.contains(QStringLiteral("description"))) {
            QSqlQuery a(m_db);
            a.exec(QStringLiteral("ALTER TABLE playlists ADD COLUMN description TEXT DEFAULT ''"));
        }
    }
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS song_cache("
        "song_id INTEGER PRIMARY KEY,"
        "cache_path TEXT NOT NULL,"
        "size_bytes INTEGER DEFAULT 0,"
        "last_used_ms INTEGER DEFAULT 0)"));
    q.exec(QStringLiteral(
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_songs_source_online "
        "ON songs(source, online_id) WHERE source>0 AND online_id>0"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS playlists("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL UNIQUE,"
        "cover_path TEXT DEFAULT '',"
        "description TEXT DEFAULT '',"
        "created_ms INTEGER DEFAULT 0)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS playlist_songs("
        "playlist_id INTEGER NOT NULL,"
        "song_id INTEGER NOT NULL,"
        "position INTEGER NOT NULL,"
        "PRIMARY KEY(playlist_id, song_id))"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS recent("
        "song_id INTEGER PRIMARY KEY,"
        "played_ms INTEGER NOT NULL)"));
    q.exec(QStringLiteral(
        "INSERT OR IGNORE INTO playlists(id,name,created_ms) VALUES(1,'我喜欢的音乐',0)"));

    reloadSongs();
    return true;
}

QStringList LibraryService::folders() const
{
    return SettingsService::musicFolders();
}

void LibraryService::setFolders(const QStringList &folders)
{
    SettingsService::setMusicFolders(folders);
    m_watcher->removePaths(m_watcher->directories());
    startScan();
}

void LibraryService::startScan()
{
    startWorker(folders());
}

void LibraryService::scanFolderNow(const QString &folder)
{
    startWorker({ folder });
}

void LibraryService::startWorker(const QStringList &folders)
{
    if (folders.isEmpty()) {
        reloadSongs();
        emit scanFinished(0, 0);
        return;
    }
    if (m_scanRunning)
        return;
    m_scanRunning = true;

    auto *thread = new QThread(this);
    m_scanThread = thread;
    auto *worker = new ScanWorker;
    worker->moveToThread(thread);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    const QString dbPath = m_dbPath;
    connect(this, &LibraryService::scanStarted, worker, [worker, folders, dbPath] {
        worker->run(folders, dbPath);
    });
    connect(worker, &ScanWorker::progress, this, &LibraryService::scanProgress);
    connect(worker, &ScanWorker::finished, this, [this](int added, int removed, const QStringList &watchDirs) {
        m_scanRunning = false;
        if (m_scanThread) {
            m_scanThread->quit();
            m_scanThread = nullptr;
        }
        m_watcher->removePaths(m_watcher->directories());
        m_watcher->addPaths(watchDirs);
        reloadSongs();
        emit scanFinished(added, removed);
        emit libraryChanged();
    });
    thread->start();
    emit scanStarted(folders.size());
}

void LibraryService::onWatchChange(const QString &path)
{
    if (!m_pendingFolders.contains(path))
        m_pendingFolders.append(path);
    m_watchTimer->start();
}

void LibraryService::reloadSongs()
{
    m_songs.clear();
    if (!m_db.isOpen())
        return;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral(
        "SELECT s.id,s.path,s.title,s.artist,s.album,s.duration_ms,s.cover_path,s.missing,"
        "s.play_count,s.last_played_ms,s.source,s.online_id,s.cover_url,s.album_id,sc.cache_path "
        "FROM songs s LEFT JOIN song_cache sc ON sc.song_id=s.id ORDER BY s.id"));
    while (q.next()) {
        Song s;
        s.id = q.value(0).toLongLong();
        s.filePath = q.value(1).toString();
        s.title = q.value(2).toString();
        s.artist = q.value(3).toString();
        s.album = q.value(4).toString();
        s.durationMs = q.value(5).toLongLong();
        s.coverPath = q.value(6).toString();
        s.missing = q.value(7).toInt() != 0;
        s.playCount = q.value(8).toLongLong();
        s.lastPlayedMs = q.value(9).toLongLong();
        s.source = q.value(10).toInt();
        s.onlineId = q.value(11).toLongLong();
        s.coverUrl = q.value(12).toString();
        s.albumId = q.value(13).toLongLong();
        s.cachePath = q.value(14).toString();
        s.lyricPath = LyricsLoader::sidecarPathFor(s.filePath);
        m_songs.append(s);
    }
}

Song LibraryService::songById(qint64 id) const
{
    for (const Song &s : m_songs)
        if (s.id == id)
            return s;
    return {};
}

Song LibraryService::songByPath(const QString &path) const
{
    for (const Song &s : m_songs)
        if (s.filePath == path)
            return s;
    return {};
}

Song LibraryService::songByOnlineId(int source, qint64 onlineId) const
{
    for (const Song &s : m_songs)
        if (s.source == source && s.onlineId == onlineId)
            return s;
    return {};
}

void LibraryService::markPlayed(qint64 songId)
{
    if (!m_db.isOpen())
        return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE songs SET play_count=play_count+1,last_played_ms=? WHERE id=?"));
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.addBindValue(songId);
    q.exec();
    for (Song &s : m_songs) {
        if (s.id == songId) {
            ++s.playCount;
            s.lastPlayedMs = QDateTime::currentMSecsSinceEpoch();
            break;
        }
    }
}

void LibraryService::removeSong(qint64 songId)
{
    if (!m_db.isOpen())
        return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM songs WHERE id=?"));
    q.addBindValue(songId);
    q.exec();
    QSqlQuery q2(m_db);
    q2.prepare(QStringLiteral("DELETE FROM playlist_songs WHERE song_id=?"));
    q2.addBindValue(songId);
    q2.exec();
    QSqlQuery q3(m_db);
    q3.prepare(QStringLiteral("DELETE FROM recent WHERE song_id=?"));
    q3.addBindValue(songId);
    q3.exec();
    QSqlQuery q4(m_db);
    q4.prepare(QStringLiteral("DELETE FROM song_cache WHERE song_id=?"));
    q4.addBindValue(songId);
    q4.exec();
    reloadSongs();
    emit libraryChanged();
}

qint64 LibraryService::upsertOnlineSong(const Song &song)
{
    if (!m_db.isOpen() || song.onlineId <= 0)
        return -1;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO songs(path,title,artist,album,duration_ms,cover_url,source,online_id,album_id,missing) "
        "VALUES(?,?,?,?,?,?,?,?,?,0) "
        "ON CONFLICT(source,online_id) WHERE source>0 AND online_id>0 DO UPDATE SET "
        "title=excluded.title,artist=excluded.artist,album=excluded.album,"
        "duration_ms=excluded.duration_ms,cover_url=excluded.cover_url,"
        "album_id=excluded.album_id,missing=0"));
    q.addBindValue(song.filePath);
    q.addBindValue(song.title);
    q.addBindValue(song.artist);
    q.addBindValue(song.album);
    q.addBindValue(song.durationMs);
    q.addBindValue(song.coverUrl);
    q.addBindValue(song.source);
    q.addBindValue(song.onlineId);
    q.addBindValue(song.albumId);
    if (!q.exec())
        return -1;

    QSqlQuery sel(m_db);
    sel.prepare(QStringLiteral("SELECT id FROM songs WHERE source=? AND online_id=?"));
    sel.addBindValue(song.source);
    sel.addBindValue(song.onlineId);
    sel.exec();
    if (!sel.next())
        return -1;
    const qint64 id = sel.value(0).toLongLong();

    for (Song &s : m_songs) {
        if (s.source == song.source && s.onlineId == song.onlineId) {
            s.id = id;
            s.title = song.title;
            s.artist = song.artist;
            s.album = song.album;
            s.durationMs = song.durationMs;
            s.coverUrl = song.coverUrl;
            s.albumId = song.albumId;
            s.filePath = song.filePath;
            return id;
        }
    }
    Song copy = song;
    copy.id = id;
    m_songs.append(copy);
    return id;
}

void LibraryService::fillMissingSongMetadata(qint64 songId, const QString &artist, const QString &album)
{
    if (!m_db.isOpen())
        return;
    const Song current = songById(songId);
    if (current.id <= 0)
        return;
    const QString newArtist = current.artist.isEmpty() ? artist : current.artist;
    const QString newAlbum = current.album.isEmpty() ? album : current.album;
    if (newArtist == current.artist && newAlbum == current.album)
        return;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE songs SET artist=?,album=? WHERE id=?"));
    q.addBindValue(newArtist);
    q.addBindValue(newAlbum);
    q.addBindValue(songId);
    if (!q.exec())
        return;
    for (Song &s : m_songs) {
        if (s.id == songId) {
            s.artist = newArtist;
            s.album = newAlbum;
            break;
        }
    }
    emit libraryChanged();
}

void LibraryService::setSongCoverPath(qint64 songId, const QString &path)
{
    if (!m_db.isOpen())
        return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE songs SET cover_path=?,has_cover=1 WHERE id=?"));
    q.addBindValue(path);
    q.addBindValue(songId);
    q.exec();
    for (Song &s : m_songs) {
        if (s.id == songId) {
            s.coverPath = path;
            break;
        }
    }
    emit libraryChanged();
}

QString LibraryService::cacheDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/cache");
    QDir().mkpath(dir);
    return dir;
}

QString LibraryService::coverCacheDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/covers");
    QDir().mkpath(dir);
    return dir;
}

QString LibraryService::songCoverCachePath(const Song &song) const
{
    if (!song.isOnline() || song.onlineId <= 0)
        return {};
    return coverCacheDir() + QStringLiteral("/song_%1_%2.jpg").arg(song.source).arg(song.onlineId);
}

QString LibraryService::playlistCoverCachePath(qint64 playlistId) const
{
    if (playlistId <= 0)
        return {};
    return coverCacheDir() + QStringLiteral("/playlist_%1.jpg").arg(playlistId);
}

QString LibraryService::cacheFilePathFor(const Song &song) const
{
    if (!song.isOnline())
        return {};
    const QByteArray key = QStringLiteral("%1:%2").arg(song.source).arg(song.onlineId).toUtf8();
    const QString name = QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());
    return cacheDir() + QLatin1Char('/') + name + QStringLiteral(".mp3");
}

QString LibraryService::cachePathFor(qint64 songId) const
{
    for (const Song &s : m_songs)
        if (s.id == songId)
            return s.cachePath;
    return {};
}

bool LibraryService::isSongCached(qint64 songId) const
{
    return !cachePathFor(songId).isEmpty();
}

void LibraryService::setSongCached(qint64 songId, const QString &path, qint64 sizeBytes)
{
    if (!m_db.isOpen())
        return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO song_cache(song_id,cache_path,size_bytes,last_used_ms) VALUES(?,?,?,?) "
        "ON CONFLICT(song_id) DO UPDATE SET cache_path=excluded.cache_path,"
        "size_bytes=excluded.size_bytes,last_used_ms=excluded.last_used_ms"));
    q.addBindValue(songId);
    q.addBindValue(path);
    q.addBindValue(sizeBytes);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.exec();
    QSqlQuery u(m_db);
    u.prepare(QStringLiteral("UPDATE songs SET cache_path=? WHERE id=?"));
    u.addBindValue(path);
    u.addBindValue(songId);
    u.exec();
    for (Song &s : m_songs) {
        if (s.id == songId) {
            s.cachePath = path;
            break;
        }
    }
    evictCacheIfNeeded();
    emit cacheChanged();
}

void LibraryService::clearCache()
{
    QDir dir(cacheDir());
    for (const QFileInfo &fi : dir.entryInfoList(QDir::Files))
        QFile::remove(fi.absoluteFilePath());
    if (m_db.isOpen()) {
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("DELETE FROM song_cache"));
        q.exec(QStringLiteral("UPDATE songs SET cache_path='' WHERE source>0"));
    }
    for (Song &s : m_songs)
        s.cachePath.clear();
    emit cacheChanged();
    emit libraryChanged();
}

void LibraryService::cacheUsage(qint64 *bytes, int *count) const
{
    qint64 b = 0;
    int c = 0;
    if (m_db.isOpen()) {
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("SELECT COALESCE(SUM(size_bytes),0), COUNT(*) FROM song_cache"));
        if (q.next()) {
            b = q.value(0).toLongLong();
            c = q.value(1).toInt();
        }
    }
    if (bytes)
        *bytes = b;
    if (count)
        *count = c;
}

void LibraryService::evictCacheIfNeeded()
{
    if (!m_db.isOpen())
        return;
    const int maxCount = SettingsService::onlineCacheMaxCount();
    const qint64 maxBytes = qint64(SettingsService::onlineCacheMaxMB()) * 1024 * 1024;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COALESCE(SUM(size_bytes),0), COUNT(*) FROM song_cache"));
    if (!q.next())
        return;
    const qint64 bytes = q.value(0).toLongLong();
    const int count = q.value(1).toInt();
    if (count <= maxCount && bytes <= maxBytes)
        return;

    QSqlQuery oldest(m_db);
    oldest.exec(QStringLiteral("SELECT song_id, cache_path FROM song_cache ORDER BY last_used_ms ASC"));
    QList<QPair<qint64, QString>> victims;
    while (oldest.next())
        victims.append({ oldest.value(0).toLongLong(), oldest.value(1).toString() });
    int removeCount = qMax(0, count - maxCount);
    qint64 removeBytes = qMax<qint64>(0, bytes - maxBytes);
    for (const auto &v : victims) {
        if (removeCount <= 0 && removeBytes <= 0)
            break;
        QFile::remove(v.second);
        QSqlQuery del(m_db);
        del.prepare(QStringLiteral("DELETE FROM song_cache WHERE song_id=?"));
        del.addBindValue(v.first);
        del.exec();
        --removeCount;
        removeBytes -= QFileInfo(v.second).size();
    }
    for (Song &s : m_songs) {
        for (const auto &v : victims)
            if (s.id == v.first)
                s.cachePath.clear();
    }
    emit cacheChanged();
}

} // namespace core

#include "LibraryService.moc"
