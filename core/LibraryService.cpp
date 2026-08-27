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
                                         QStringLiteral("aac") };

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
                            "cover_path=excluded.cover_path,has_cover=excluded.has_cover,missing=0"));
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
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS playlists("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL UNIQUE,"
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
        "SELECT id,path,title,artist,album,duration_ms,cover_path,missing,play_count,last_played_ms "
        "FROM songs ORDER BY id"));
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
    reloadSongs();
    emit libraryChanged();
}

} // namespace core

#include "LibraryService.moc"
