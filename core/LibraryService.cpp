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
#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QDebug>
#include <QRegularExpression>

#include <algorithm>

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

QString normalizedFileKey(const QString &path)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(
        QFileInfo(path).absoluteFilePath()));
#ifdef Q_OS_WIN
    return clean.toCaseFolded();
#else
    return clean;
#endif
}

bool isPathInside(const QString &path, const QString &directory)
{
    if (directory.isEmpty())
        return false;
    const QString fileKey = normalizedFileKey(path);
    const QString directoryKey = normalizedFileKey(directory);
    return fileKey == directoryKey
        || fileKey.startsWith(directoryKey + QLatin1Char('/'));
}

QString safeDownloadPart(QString value)
{
    value = value.trimmed();
    // 不使用原始字符串：该表达式位于 Q_OBJECT 类之前时会触发 Qt MOC 误解析，
    // 进而漏生成 ScanWorker 的元对象代码。
    value.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]")), QStringLiteral("_"));
    value.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1F]")), QStringLiteral("_"));
    while (value.endsWith(QLatin1Char('.')) || value.endsWith(QLatin1Char(' ')))
        value.chop(1);
    if (value.isEmpty())
        value = QStringLiteral("未知");
    if (value.size() > 80)
        value = value.left(80).trimmed();
    return value;
}

QString downloadBaseName(const QString &titleValue, const QString &artistValue, const QString &remoteId)
{
    const QString title = titleValue.isEmpty() ? QStringLiteral("歌曲_%1").arg(remoteId)
                                                : titleValue;
    const QString artist = artistValue.isEmpty() ? QStringLiteral("未知歌手") : artistValue;
    return safeDownloadPart(artist) + QStringLiteral(" - ") + safeDownloadPart(title);
}

QString coverCacheDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/covers");
    QDir().mkpath(dir);
    return dir;
}

QString databaseIntegrityError(const QSqlDatabase &db)
{
    QSqlQuery check(db);
    if (!check.exec(QStringLiteral("PRAGMA quick_check")))
        return check.lastError().text();
    QStringList errors;
    while (check.next()) {
        const QString message = check.value(0).toString();
        if (message.compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0)
            errors.append(message);
    }
    return errors.join(QStringLiteral("; "));
}

QString backupDatabaseFiles(const QString &dbPath)
{
    const QString root = QFileInfo(dbPath).absolutePath() + QStringLiteral("/db-backups/automatic-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    if (!QDir().mkpath(root))
        return {};

    // 主数据库必须备份成功后，才允许后续丢弃损坏的 WAL。仅复制到 WAL/SHM
    // 不能构成可恢复备份。
    const QString databaseCopy = root + QLatin1Char('/') + QFileInfo(dbPath).fileName();
    if (!QFile::copy(dbPath, databaseCopy)) {
        QDir(root).removeRecursively();
        return {};
    }
    for (const QString &suffix : { QStringLiteral("-wal"), QStringLiteral("-shm") }) {
        const QString source = dbPath + suffix;
        if (!QFileInfo::exists(source))
            continue;
        QFile::copy(source, root + QLatin1Char('/') + QFileInfo(source).fileName());
    }
    return root;
}

bool openConfiguredDatabase(QSqlDatabase *database, const QString &dbPath, QString *error)
{
    *database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("main"));
    database->setDatabaseName(dbPath);
    if (!database->open()) {
        *error = database->lastError().text();
        database->close();
        return false;
    }
    QSqlQuery pragma(*database);
    // 打开数据库和设置运行参数是两个独立步骤。损坏的 WAL/索引可能让
    // journal_mode=WAL 报错，但此时数据库句柄仍然可以用于完整性检查和恢复；
    // 不能把这个可恢复错误直接变成“音乐库无法安全打开”。
    const auto optionalPragma = [&pragma](const QString &sql) {
        if (!pragma.exec(sql))
            qWarning() << "SQLite pragma failed:" << sql << pragma.lastError().text();
    };
    optionalPragma(QStringLiteral("PRAGMA busy_timeout=5000"));
    optionalPragma(QStringLiteral("PRAGMA journal_mode=WAL"));
    optionalPragma(QStringLiteral("PRAGMA synchronous=FULL"));
    optionalPragma(QStringLiteral("PRAGMA wal_autocheckpoint=100"));
    return true;
}

void closeDatabaseConnection(QSqlDatabase *database)
{
    const QString connectionName = database->connectionName();
    database->close();
    *database = QSqlDatabase();
    if (!connectionName.isEmpty() && QSqlDatabase::contains(connectionName))
        QSqlDatabase::removeDatabase(connectionName);
}

struct RecoveryRows
{
    QList<QVariantList> rows;
    QString error;
};

RecoveryRows readRecoveryRows(const QSqlDatabase &database, const QString &sql)
{
    RecoveryRows result;
    QSqlQuery query(database);
    if (!query.exec(sql)) {
        result.error = query.lastError().text();
        return result;
    }
    const int columnCount = query.record().count();
    while (query.next()) {
        QVariantList row;
        row.reserve(columnCount);
        for (int column = 0; column < columnCount; ++column)
            row.append(query.value(column));
        result.rows.append(row);
    }
    if (query.lastError().isValid())
        result.error = query.lastError().text();
    return result;
}

bool executeRecoverySql(const QSqlDatabase &database, const QString &sql, QString *error)
{
    QSqlQuery query(database);
    if (query.exec(sql))
        return true;
    if (error)
        *error = query.lastError().text();
    return false;
}

bool createRecoverySchema(const QSqlDatabase &database, QString *error)
{
    const QStringList statements = {
        QStringLiteral("CREATE TABLE songs(id INTEGER PRIMARY KEY AUTOINCREMENT,path TEXT NOT NULL UNIQUE,title TEXT DEFAULT '',artist TEXT DEFAULT '',album TEXT DEFAULT '',duration_ms INTEGER DEFAULT 0,cover_path TEXT DEFAULT '',has_cover INTEGER DEFAULT 0,missing INTEGER DEFAULT 0,play_count INTEGER DEFAULT 0,last_played_ms INTEGER DEFAULT 0,source INTEGER DEFAULT 0,remote_id TEXT DEFAULT '',online_id INTEGER DEFAULT 0,cover_url TEXT DEFAULT '',cache_path TEXT DEFAULT '',album_remote_id TEXT DEFAULT '',album_id INTEGER DEFAULT 0,artist_remote_id TEXT DEFAULT '',download_path TEXT DEFAULT '',file_mtime_ms INTEGER DEFAULT 0,file_size INTEGER DEFAULT 0)"),
        QStringLiteral("CREATE TABLE playlists(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT NULL UNIQUE,cover_path TEXT DEFAULT '',description TEXT DEFAULT '',created_ms INTEGER DEFAULT 0)"),
        QStringLiteral("CREATE TABLE song_cache(song_id INTEGER PRIMARY KEY,cache_path TEXT NOT NULL,size_bytes INTEGER DEFAULT 0,last_used_ms INTEGER DEFAULT 0)"),
        QStringLiteral("CREATE TABLE playlist_songs(playlist_id INTEGER NOT NULL,song_id INTEGER NOT NULL,position INTEGER NOT NULL,PRIMARY KEY(playlist_id,song_id))"),
        QStringLiteral("CREATE TABLE recent(song_id INTEGER PRIMARY KEY,played_ms INTEGER NOT NULL)")
    };
    for (const QString &statement : statements) {
        if (!executeRecoverySql(database, statement, error))
            return false;
    }
    return executeRecoverySql(database, QStringLiteral(
        "CREATE UNIQUE INDEX idx_songs_source_remote ON songs(source, remote_id) "
        "WHERE source>0 AND remote_id<>''"), error);
}

template<typename RowWriter>
bool writeRecoveryRows(const QSqlDatabase &database, const QList<QVariantList> &rows,
                       const QString &sql, RowWriter &&writer, QString *error)
{
    QSqlQuery query(database);
    for (const QVariantList &row : rows) {
        query.clear();
        if (!query.prepare(sql)) {
            if (error)
                *error = query.lastError().text();
            return false;
        }
        if (!writer(query, row))
            continue;
        if (!query.exec()) {
            qWarning() << "Skipping unrecoverable database row:" << query.lastError().text();
        }
    }
    return true;
}

class ScanWorker : public QObject
{
    Q_OBJECT
public slots:
    void run(const QStringList &folders, const QString &dbPath, const QString &managedDownloadDir)
    {
        int added = 0;
        int removed = 0;
        QStringList watchDirs;

        const QString conn = QStringLiteral("scan_%1").arg(quintptr(QThread::currentThread()));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(dbPath);
            if (db.open()) {
                {
                    QSqlQuery q(db);
                    q.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
                    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
                    q.exec(QStringLiteral("PRAGMA synchronous=FULL"));

                    struct Fingerprint {
                        qint64 modifiedMs = 0;
                        qint64 size = 0;
                    };
                    QHash<QString, Fingerprint> knownFiles;
                    QSqlQuery known(db);
                    if (known.exec(QStringLiteral(
                            "SELECT path,file_mtime_ms,file_size FROM songs WHERE source=0"))) {
                        while (known.next()) {
                            knownFiles.insert(normalizedFileKey(known.value(0).toString()),
                                              { known.value(1).toLongLong(), known.value(2).toLongLong() });
                        }
                    }
                    known.finish();
                    const bool transactionStarted = db.transaction();

                    int done = 0;
                    for (const QString &folder : folders) {
                        if (QThread::currentThread()->isInterruptionRequested())
                            break;
                        QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
                        while (it.hasNext()) {
                            if (QThread::currentThread()->isInterruptionRequested())
                                break;
                            const QString path = it.next();
                            if (!isSupportedFile(path) || isPathInside(path, managedDownloadDir))
                                continue;
                            const QFileInfo fileInfo(path);
                            const qint64 modifiedMs = fileInfo.lastModified().toMSecsSinceEpoch();
                            const qint64 fileSize = fileInfo.size();
                            const auto knownIt = knownFiles.constFind(normalizedFileKey(path));
                            const bool unchanged = knownIt != knownFiles.constEnd()
                                && knownIt->modifiedMs == modifiedMs && knownIt->size == fileSize;
                            if (unchanged) {
                                ++done;
                                if (done % 25 == 0)
                                    emit progress(done, done);
                                continue;
                            }
                            const bool legacyFingerprint = knownIt != knownFiles.constEnd()
                                && knownIt->modifiedMs == 0 && knownIt->size == 0;
                            if (legacyFingerprint) {
                                // 旧库首次升级只补文件指纹，保留已经读取好的标签；之后仅在
                                // 大小或修改时间发生变化时才重新调用 TagLib。
                                QSqlQuery unchanged(db);
                                unchanged.prepare(QStringLiteral(
                                    "UPDATE songs SET missing=0,file_mtime_ms=?,file_size=? WHERE path=?"));
                                unchanged.addBindValue(modifiedMs);
                                unchanged.addBindValue(fileSize);
                                unchanged.addBindValue(path);
                                unchanged.exec();
                                ++done;
                                if (done % 25 == 0)
                                    emit progress(done, done);
                                continue;
                            }
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
                                "INSERT INTO songs(path,title,artist,album,duration_ms,cover_path,has_cover,missing,file_mtime_ms,file_size) "
                                "VALUES(?,?,?,?,?,?,?,0,?,?) "
                                "ON CONFLICT(path) DO UPDATE SET title=excluded.title,artist=excluded.artist,"
                                "album=excluded.album,duration_ms=excluded.duration_ms,"
                                // 保留此前从网易云获取的封面;文件本身有内嵌封面时则以新封面为准。
                                "cover_path=CASE WHEN excluded.cover_path <> '' THEN excluded.cover_path ELSE songs.cover_path END,"
                                "has_cover=CASE WHEN excluded.cover_path <> '' THEN excluded.has_cover ELSE songs.has_cover END,"
                                "missing=0,file_mtime_ms=excluded.file_mtime_ms,file_size=excluded.file_size"));
                            ins.addBindValue(path);
                            ins.addBindValue(info.title);
                            ins.addBindValue(info.artist);
                            ins.addBindValue(info.album);
                            ins.addBindValue(info.durationMs);
                            ins.addBindValue(coverPath);
                            ins.addBindValue(info.hasCover() ? 1 : 0);
                            ins.addBindValue(modifiedMs);
                            ins.addBindValue(fileSize);
                            if (ins.exec())
                                ++added;
                            ++done;
                            if (done % 25 == 0)
                                emit progress(done, done);
                        }
                    }

                    if (!QThread::currentThread()->isInterruptionRequested()) {
                        QSqlQuery select(db);
                        // 在线歌曲使用 netease:// 虚拟路径，不能按本地文件是否存在判断失效。
                        q.exec(QStringLiteral("UPDATE songs SET missing=0 WHERE source>0"));
                        select.exec(QStringLiteral("SELECT id,path,source,missing FROM songs"));
                        QSqlQuery mark(db);
                        mark.prepare(QStringLiteral("UPDATE songs SET missing=? WHERE id=?"));
                        while (select.next()) {
                            if (QThread::currentThread()->isInterruptionRequested())
                                break;
                            const qint64 id = select.value(0).toLongLong();
                            const QString path = select.value(1).toString();
                            const int source = select.value(2).toInt();
                            const bool wasMissing = select.value(3).toInt() != 0;
                            if (source > 0)
                                continue;
                            const bool exists = QFile::exists(path);
                            if (!wasMissing && !exists) {
                                mark.bindValue(0, 1);
                                mark.bindValue(1, id);
                                if (mark.exec())
                                    ++removed;
                            }
                        }
                    }

                    if (transactionStarted) {
                        if (QThread::currentThread()->isInterruptionRequested())
                            db.rollback();
                        else if (!db.commit())
                            qWarning() << "Music scan commit failed:" << db.lastError().text();
                    }

                    for (const QString &folder : folders) {
                        if (QThread::currentThread()->isInterruptionRequested())
                            break;
                        if (isPathInside(folder, managedDownloadDir))
                            continue;
                        QDirIterator it(folder, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                        watchDirs << folder;
                        while (it.hasNext() && watchDirs.size() < 4000) {
                            if (QThread::currentThread()->isInterruptionRequested())
                                break;
                            const QString directory = it.next();
                            if (!isPathInside(directory, managedDownloadDir))
                                watchDirs << directory;
                        }
                    }
                }
                db.close();
            }
        }
        // Qt 要求所有 QSqlQuery 和 QSqlDatabase 句柄都先析构，才能移除连接。
        QSqlDatabase::removeDatabase(conn);
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
        m_scanThread->requestInterruption();
        m_scanThread->quit();
        m_scanThread->wait();
    }
    if (m_db.isOpen()) {
        QSqlQuery checkpoint(m_db);
        if (!checkpoint.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)")))
            qWarning() << "Final WAL checkpoint failed:" << checkpoint.lastError().text();
    }
    const QString connectionName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    if (!connectionName.isEmpty() && QSqlDatabase::contains(connectionName))
        QSqlDatabase::removeDatabase(connectionName);
}

QString LibraryService::lastError() const
{
    if (!m_lastError.isEmpty())
        return m_lastError;
    return m_db.isValid() ? m_db.lastError().text() : QStringLiteral("invalid db handle");
}

bool LibraryService::recoverCorruptDatabase(const QString &backupPath, QString *error)
{
    if (backupPath.isEmpty()) {
        if (error)
            *error = QStringLiteral("安全备份路径为空");
        return false;
    }

    // 先从未使用索引的表扫描中尽可能提取用户数据。索引损坏时，NOT INDEXED
    // 仍有机会读取表本身；读取失败的行会被跳过，而原库始终保留在备份中。
    QString songsSql = QStringLiteral(
        "SELECT id,path,title,artist,album,duration_ms,cover_path,has_cover,missing,"
        "play_count,last_played_ms,source,");
    QSqlQuery songColumns(m_db);
    QSet<QString> availableSongColumns;
    if (songColumns.exec(QStringLiteral("PRAGMA table_info(songs)"))) {
        while (songColumns.next())
            availableSongColumns.insert(songColumns.value(1).toString());
    }
    const auto textColumnOr = [&availableSongColumns](const QString &column, const QString &fallback) {
        return availableSongColumns.contains(column) ? column : fallback + QStringLiteral(" AS ") + column;
    };
    songsSql += textColumnOr(QStringLiteral("remote_id"),
                             availableSongColumns.contains(QStringLiteral("online_id"))
                                 ? QStringLiteral("CAST(online_id AS TEXT)") : QStringLiteral("''"));
    songsSql += QStringLiteral(",online_id,cover_url,cache_path,");
    songsSql += textColumnOr(QStringLiteral("album_remote_id"),
                             availableSongColumns.contains(QStringLiteral("album_id"))
                                 ? QStringLiteral("CAST(album_id AS TEXT)") : QStringLiteral("''"));
    songsSql += QStringLiteral(",album_id,");
    songsSql += textColumnOr(QStringLiteral("artist_remote_id"), QStringLiteral("''"));
    songsSql += QLatin1Char(',') + textColumnOr(QStringLiteral("download_path"), QStringLiteral("''")) + QLatin1Char(' ');
    songsSql += QStringLiteral("FROM songs NOT INDEXED ORDER BY rowid");
    songColumns = QSqlQuery();
    const RecoveryRows songs = readRecoveryRows(m_db, songsSql);
    const RecoveryRows playlists = readRecoveryRows(m_db, QStringLiteral(
        "SELECT id,name,cover_path,description,created_ms FROM playlists NOT INDEXED ORDER BY rowid"));
    const RecoveryRows caches = readRecoveryRows(m_db, QStringLiteral(
        "SELECT song_id,cache_path,size_bytes,last_used_ms FROM song_cache NOT INDEXED ORDER BY rowid"));
    const RecoveryRows memberships = readRecoveryRows(m_db, QStringLiteral(
        "SELECT playlist_id,song_id,position FROM playlist_songs NOT INDEXED ORDER BY rowid"));
    const RecoveryRows recent = readRecoveryRows(m_db, QStringLiteral(
        "SELECT song_id,played_ms FROM recent NOT INDEXED ORDER BY rowid"));

    const auto reportReadError = [](const QString &table, const RecoveryRows &result) {
        if (!result.error.isEmpty())
            qWarning() << "Partial database recovery for" << table << ":" << result.error
                       << "rows:" << result.rows.size();
    };
    reportReadError(QStringLiteral("songs"), songs);
    reportReadError(QStringLiteral("playlists"), playlists);
    reportReadError(QStringLiteral("song_cache"), caches);
    reportReadError(QStringLiteral("playlist_songs"), memberships);
    reportReadError(QStringLiteral("recent"), recent);

    const QString recoveredPath = m_dbPath + QStringLiteral(".recovered-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString connectionName = QStringLiteral("recovery_%1").arg(quintptr(this));
    QSqlDatabase recovered = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    recovered.setDatabaseName(recoveredPath);
    if (!recovered.open()) {
        if (error)
            *error = recovered.lastError().text();
        recovered = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        return false;
    }
    {
        QSqlQuery pragma(recovered);
        if (!pragma.exec(QStringLiteral("PRAGMA synchronous=FULL"))) {
            if (error)
                *error = pragma.lastError().text();
            recovered.close();
            recovered = QSqlDatabase();
            QSqlDatabase::removeDatabase(connectionName);
            QFile::remove(recoveredPath);
            return false;
        }
    }
    QString recoveryError;
    if (!createRecoverySchema(recovered, &recoveryError)) {
        if (error)
            *error = recoveryError;
        recovered.close();
        recovered = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        QFile::remove(recoveredPath);
        return false;
    }

    if (!executeRecoverySql(recovered,
                            QStringLiteral("INSERT INTO playlists(id,name,created_ms) VALUES(1,'我喜欢的音乐',0)"),
                            &recoveryError)) {
        if (error)
            *error = recoveryError;
        recovered.close();
        recovered = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        QFile::remove(recoveredPath);
        return false;
    }

    if (!executeRecoverySql(recovered, QStringLiteral("BEGIN"), &recoveryError)) {
        if (error)
            *error = recoveryError;
        recovered.close();
        recovered = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        QFile::remove(recoveredPath);
        return false;
    }
    QSet<QString> paths;
    QSet<QString> onlineKeys;
    const bool songsWritten = writeRecoveryRows(recovered, songs.rows, QStringLiteral(
        "INSERT OR IGNORE INTO songs(id,path,title,artist,album,duration_ms,cover_path,has_cover,missing,"
        "play_count,last_played_ms,source,remote_id,online_id,cover_url,cache_path,album_remote_id,album_id,artist_remote_id,download_path) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"),
        [&paths, &onlineKeys](QSqlQuery &query, const QVariantList &row) {
            if (row.size() != 20)
                return false;
            const QString path = row.at(1).toString();
            if (path.isEmpty() || paths.contains(path))
                return false;
            const int source = row.at(11).toInt();
            const QString remoteId = row.at(12).toString();
            const QString onlineKey = QStringLiteral("%1:%2").arg(source).arg(remoteId);
            if (source > 0 && !remoteId.isEmpty() && onlineKeys.contains(onlineKey))
                return false;
            paths.insert(path);
            if (source > 0 && !remoteId.isEmpty())
                onlineKeys.insert(onlineKey);
            for (const QVariant &value : row)
                query.addBindValue(value);
            return true;
        }, &recoveryError);
    const bool playlistsWritten = writeRecoveryRows(recovered, playlists.rows, QStringLiteral(
        "INSERT OR IGNORE INTO playlists(id,name,cover_path,description,created_ms) VALUES(?,?,?,?,?)"),
        [](QSqlQuery &query, const QVariantList &row) {
            if (row.size() != 5 || row.at(1).toString().isEmpty())
                return false;
            for (const QVariant &value : row)
                query.addBindValue(value);
            return true;
        }, &recoveryError);
    const bool membershipsWritten = writeRecoveryRows(recovered, memberships.rows, QStringLiteral(
        "INSERT OR IGNORE INTO playlist_songs(playlist_id,song_id,position) VALUES(?,?,?)"),
        [](QSqlQuery &query, const QVariantList &row) {
            if (row.size() != 3)
                return false;
            for (const QVariant &value : row)
                query.addBindValue(value);
            return true;
        }, &recoveryError);
    const bool cachesWritten = writeRecoveryRows(recovered, caches.rows, QStringLiteral(
        "INSERT OR REPLACE INTO song_cache(song_id,cache_path,size_bytes,last_used_ms) VALUES(?,?,?,?)"),
        [](QSqlQuery &query, const QVariantList &row) {
            if (row.size() != 4 || row.at(1).toString().isEmpty())
                return false;
            for (const QVariant &value : row)
                query.addBindValue(value);
            return true;
        }, &recoveryError);
    const bool recentWritten = writeRecoveryRows(recovered, recent.rows, QStringLiteral(
        "INSERT OR REPLACE INTO recent(song_id,played_ms) VALUES(?,?)"),
        [](QSqlQuery &query, const QVariantList &row) {
            if (row.size() != 2)
                return false;
            for (const QVariant &value : row)
                query.addBindValue(value);
            return true;
        }, &recoveryError);
    const bool committed = songsWritten && playlistsWritten && membershipsWritten && cachesWritten
        && recentWritten && executeRecoverySql(recovered, QStringLiteral("COMMIT"), &recoveryError);
    if (!committed) {
        QSqlQuery rollback(recovered);
        rollback.exec(QStringLiteral("ROLLBACK"));
        if (error)
            *error = recoveryError;
        recovered.close();
        recovered = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        QFile::remove(recoveredPath);
        return false;
    }
    recovered.close();
    recovered = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);

    closeDatabaseConnection(&m_db);
    const QString corruptDb = backupPath + QStringLiteral("/corrupt-library.db");
    const QString corruptWal = backupPath + QStringLiteral("/corrupt-library.db-wal");
    const QString corruptShm = backupPath + QStringLiteral("/corrupt-library.db-shm");
    const auto moveAside = [](const QString &source, const QString &destination) {
        return !QFileInfo::exists(source) || QFile::rename(source, destination);
    };
    if (!moveAside(m_dbPath, corruptDb)
        || !moveAside(m_dbPath + QStringLiteral("-wal"), corruptWal)
        || !moveAside(m_dbPath + QStringLiteral("-shm"), corruptShm)
        || !QFile::rename(recoveredPath, m_dbPath)) {
        if (QFileInfo::exists(corruptDb) && !QFileInfo::exists(m_dbPath))
            QFile::rename(corruptDb, m_dbPath);
        if (QFileInfo::exists(corruptWal) && !QFileInfo::exists(m_dbPath + QStringLiteral("-wal")))
            QFile::rename(corruptWal, m_dbPath + QStringLiteral("-wal"));
        if (QFileInfo::exists(corruptShm) && !QFileInfo::exists(m_dbPath + QStringLiteral("-shm")))
            QFile::rename(corruptShm, m_dbPath + QStringLiteral("-shm"));
        QFile::remove(recoveredPath);
        if (error)
            *error = QStringLiteral("无法替换损坏数据库文件");
        openConfiguredDatabase(&m_db, m_dbPath, &m_lastError);
        return false;
    }
    if (!openConfiguredDatabase(&m_db, m_dbPath, &m_lastError)) {
        if (error)
            *error = m_lastError;
        return false;
    }
    return true;
}

bool LibraryService::openDatabase()
{
    m_lastError.clear();
    m_dbPath = s_dbOverride.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/library.db")
        : s_dbOverride;
    QDir().mkpath(QFileInfo(m_dbPath).absolutePath());
    if (m_db.isValid()) {
        m_db.close();
        m_db = QSqlDatabase();
    }
    if (QSqlDatabase::contains(QStringLiteral("main"))) {
        QSqlDatabase stale = QSqlDatabase::database(QStringLiteral("main"), false);
        stale.close();
        stale = QSqlDatabase();
        QSqlDatabase::removeDatabase(QStringLiteral("main"));
    }
    if (!openConfiguredDatabase(&m_db, m_dbPath, &m_lastError))
        return false;

    QString integrityError = databaseIntegrityError(m_db);
    if (!integrityError.isEmpty()) {
        QString checkpointError;
        QString repairError;
        QString backupPath;
        {
            QSqlQuery checkpoint(m_db);
            if (!checkpoint.exec(QStringLiteral("PRAGMA wal_checkpoint(FULL)")))
                checkpointError = checkpoint.lastError().text();
            backupPath = backupDatabaseFiles(m_dbPath);
            QSqlQuery repair(m_db);
            if (!repair.exec(QStringLiteral("REINDEX")))
                repairError = repair.lastError().text();
        }

        // REINDEX 即使报告某个旧索引读取失败，也必须依据新的完整性检查结果
        // 决定是否继续，不能直接把应用判定为启动失败。
        integrityError = databaseIntegrityError(m_db);
        if (!integrityError.isEmpty()) {
            if (backupPath.isEmpty()) {
                m_lastError = QStringLiteral("数据库损坏且无法创建安全备份：%1")
                                  .arg(integrityError);
                return false;
            }

            // 现场问题是 WAL 损坏而已落盘的主数据库仍然健康。先关闭所有句柄，
            // 再丢弃已经完整备份的 WAL/SHM，并重新验证主库，避免用户被困在
            // “自动修复失败”的启动弹窗中。
            closeDatabaseConnection(&m_db);
            const bool removedWal = !QFileInfo::exists(m_dbPath + QStringLiteral("-wal"))
                || QFile::remove(m_dbPath + QStringLiteral("-wal"));
            const bool removedShm = !QFileInfo::exists(m_dbPath + QStringLiteral("-shm"))
                || QFile::remove(m_dbPath + QStringLiteral("-shm"));
            if (!removedWal || !removedShm
                || !openConfiguredDatabase(&m_db, m_dbPath, &m_lastError)) {
                m_lastError = QStringLiteral("数据库恢复失败；备份：%1；%2")
                                  .arg(backupPath, m_lastError);
                return false;
            }
            integrityError = databaseIntegrityError(m_db);
            if (!integrityError.isEmpty()) {
                QString recoveryError;
                if (!recoverCorruptDatabase(backupPath, &recoveryError)) {
                    m_lastError = QStringLiteral("数据库重建失败：%1；备份：%2")
                                      .arg(recoveryError.isEmpty() ? integrityError : recoveryError, backupPath);
                    return false;
                }
                integrityError = databaseIntegrityError(m_db);
                if (!integrityError.isEmpty()) {
                    m_lastError = QStringLiteral("数据库重建后完整性检查失败：%1；备份：%2")
                                      .arg(integrityError, backupPath);
                    return false;
                }
                qWarning() << "Recovered database by rebuilding readable rows; backup:" << backupPath;
            }
            qWarning() << "Recovered database by discarding corrupt WAL; backup:" << backupPath
                       << "checkpoint:" << checkpointError << "reindex:" << repairError;
        } else {
            qWarning() << "Repaired database indexes; backup:" << backupPath
                       << "reindex:" << repairError;
        }
    }

    QSqlQuery q(m_db);
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
    const QList<QPair<QString, QString>> songColumnsToEnsure = {
        { QStringLiteral("source"), QStringLiteral("INTEGER DEFAULT 0") },
        { QStringLiteral("remote_id"), QStringLiteral("TEXT DEFAULT ''") },
        { QStringLiteral("online_id"), QStringLiteral("INTEGER DEFAULT 0") },
        { QStringLiteral("cover_url"), QStringLiteral("TEXT DEFAULT ''") },
        { QStringLiteral("cache_path"), QStringLiteral("TEXT DEFAULT ''") },
        { QStringLiteral("album_remote_id"), QStringLiteral("TEXT DEFAULT ''") },
        { QStringLiteral("album_id"), QStringLiteral("INTEGER DEFAULT 0") },
        { QStringLiteral("artist_remote_id"), QStringLiteral("TEXT DEFAULT ''") },
        { QStringLiteral("download_path"), QStringLiteral("TEXT DEFAULT ''") },
        { QStringLiteral("file_mtime_ms"), QStringLiteral("INTEGER DEFAULT 0") },
        { QStringLiteral("file_size"), QStringLiteral("INTEGER DEFAULT 0") }
    };
    bool needsSongMigration = false;
    for (const auto &column : songColumnsToEnsure)
        needsSongMigration = needsSongMigration || !cols.contains(column.first);
    if (needsSongMigration) {
        QSqlQuery checkpoint(m_db);
        checkpoint.exec(QStringLiteral("PRAGMA wal_checkpoint(FULL)"));
        checkpoint.finish();
        const QString migrationBackup = backupDatabaseFiles(m_dbPath);
        if (migrationBackup.isEmpty()) {
            m_lastError = QStringLiteral("数据库迁移前无法创建安全备份");
            return false;
        }
        if (!m_db.transaction()) {
            m_lastError = QStringLiteral("无法开始数据库迁移事务：%1").arg(m_db.lastError().text());
            return false;
        }
        bool migrationOk = true;
        QString migrationError;
        for (const auto &column : songColumnsToEnsure) {
            if (cols.contains(column.first))
                continue;
            QSqlQuery alter(m_db);
            if (!alter.exec(QStringLiteral("ALTER TABLE songs ADD COLUMN %1 %2")
                                .arg(column.first, column.second))) {
                migrationOk = false;
                migrationError = alter.lastError().text();
                break;
            }
        }
        if (migrationOk) {
            QSqlQuery backfill(m_db);
            migrationOk = backfill.exec(QStringLiteral(
                "UPDATE songs SET remote_id=CAST(online_id AS TEXT) "
                "WHERE source>0 AND remote_id='' AND online_id>0"));
            if (migrationOk)
                migrationOk = backfill.exec(QStringLiteral(
                    "UPDATE songs SET album_remote_id=CAST(album_id AS TEXT) "
                    "WHERE source>0 AND album_remote_id='' AND album_id>0"));
            if (!migrationOk)
                migrationError = backfill.lastError().text();
        }
        if (migrationOk) {
            QSqlQuery conflicts(m_db);
            migrationOk = conflicts.exec(QStringLiteral(
                "SELECT source,remote_id FROM songs WHERE source>0 AND remote_id<>'' "
                "GROUP BY source,remote_id HAVING COUNT(*)>1 LIMIT 1"));
            if (migrationOk && conflicts.next()) {
                migrationOk = false;
                migrationError = QStringLiteral("发现重复远端歌曲身份 %1:%2")
                                     .arg(conflicts.value(0).toInt()).arg(conflicts.value(1).toString());
            } else if (!migrationOk) {
                migrationError = conflicts.lastError().text();
            }
        }
        if (migrationOk) {
            QSqlQuery index(m_db);
            migrationOk = index.exec(QStringLiteral(
                "CREATE UNIQUE INDEX IF NOT EXISTS idx_songs_source_remote "
                "ON songs(source,remote_id) WHERE source>0 AND remote_id<>''"));
            if (!migrationOk)
                migrationError = index.lastError().text();
        }
        if (!migrationOk || !m_db.commit()) {
            if (migrationError.isEmpty())
                migrationError = m_db.lastError().text();
            m_db.rollback();
            m_lastError = QStringLiteral("数据库多源迁移失败：%1；备份：%2")
                              .arg(migrationError, migrationBackup);
            return false;
        }
    }

    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS playlists("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL UNIQUE,"
        "cover_path TEXT DEFAULT '',"
        "description TEXT DEFAULT '',"
        "created_ms INTEGER DEFAULT 0)"));

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
    if (!q.exec(QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_songs_source_remote "
            "ON songs(source, remote_id) WHERE source>0 AND remote_id<>''"))) {
        m_lastError = QStringLiteral("创建多源歌曲唯一索引失败：%1").arg(q.lastError().text());
        return false;
    }
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
    q.exec(QStringLiteral("DELETE FROM song_cache WHERE song_id NOT IN (SELECT id FROM songs)"));
    q.exec(QStringLiteral(
        "DELETE FROM playlist_songs WHERE playlist_id NOT IN (SELECT id FROM playlists) "
        "OR song_id NOT IN (SELECT id FROM songs)"));

    integrityError = databaseIntegrityError(m_db);
    if (!integrityError.isEmpty()) {
        m_lastError = QStringLiteral("数据库初始化后完整性检查失败：%1").arg(integrityError);
        return false;
    }

    removeManagedDownloadImports();
    reloadSongs();
    return m_lastError.isEmpty();
}

void LibraryService::reloadDatabase()
{
    removeManagedDownloadImports();
    reloadSongs();
    emit libraryChanged();
}

void LibraryService::reconcileManagedDownloads()
{
    if (!m_db.isOpen())
        return;

    const QString managedDownloadDir = SettingsService::onlineDownloadDir();
    const QDir directory(managedDownloadDir);
    if (managedDownloadDir.isEmpty() || !directory.exists())
        return;

    struct DownloadRow {
        qint64 id = -1;
        QString remoteId;
        QString title;
        QString artist;
        QString storedPath;
        QString baseKey;
        bool assigned = false;
    };

    QList<DownloadRow> rows;
    QSet<QString> claimedPaths;
    QSqlQuery songs(m_db);
    if (!songs.exec(QStringLiteral(
            "SELECT id,remote_id,title,artist,download_path FROM songs "
            "WHERE source>0 AND remote_id<>'' ORDER BY id")))
        return;
    while (songs.next()) {
        DownloadRow row;
        row.id = songs.value(0).toLongLong();
        row.remoteId = songs.value(1).toString();
        row.title = songs.value(2).toString();
        row.artist = songs.value(3).toString();
        row.storedPath = songs.value(4).toString();
        row.baseKey = downloadBaseName(row.title, row.artist, row.remoteId).toCaseFolded();
        row.assigned = !row.storedPath.isEmpty() && QFileInfo(row.storedPath).isFile()
            && QFileInfo(row.storedPath).size() > 0;
        if (row.assigned)
            claimedPaths.insert(normalizedFileKey(row.storedPath));
        rows.append(row);
    }
    songs.finish();

    QHash<QString, QString> exactFiles;
    QHash<QString, QList<QPair<int, QString>>> numberedFiles;
    const QRegularExpression numberedSuffix(QStringLiteral(R"(^(.*) \((\d+)\)$)"));
    const QFileInfoList files = directory.entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                                        QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &file : files) {
        if (!isSupportedFile(file.absoluteFilePath()) || file.size() <= 0)
            continue;
        const QString path = QDir::toNativeSeparators(file.absoluteFilePath());
        const QString completeBase = file.completeBaseName();
        exactFiles.insert(completeBase.toCaseFolded(), path);
        const QRegularExpressionMatch match = numberedSuffix.match(completeBase);
        if (match.hasMatch()) {
            const int suffix = match.captured(2).toInt();
            if (suffix >= 2)
                numberedFiles[match.captured(1).toCaseFolded()].append({ suffix, path });
        }
    }
    for (auto it = numberedFiles.begin(); it != numberedFiles.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(), [](const auto &left, const auto &right) {
            return left.first == right.first ? left.second < right.second : left.first < right.first;
        });
    }

    const auto assignPath = [this, &claimedPaths](DownloadRow &row, const QString &candidate) {
        if (candidate.isEmpty() || claimedPaths.contains(normalizedFileKey(candidate)))
            return false;
        QSqlQuery update(m_db);
        update.prepare(QStringLiteral("UPDATE songs SET download_path=? WHERE id=? AND source>0"));
        update.addBindValue(candidate);
        update.addBindValue(row.id);
        if (!update.exec() || update.numRowsAffected() != 1)
            return false;
        row.storedPath = candidate;
        row.assigned = true;
        claimedPaths.insert(normalizedFileKey(candidate));
        return true;
    };

    // 先匹配无序号的精确文件名，避免把标题本身以“(2)”结尾的歌曲误当作重名副本。
    for (DownloadRow &row : rows) {
        if (!row.assigned)
            assignPath(row, exactFiles.value(row.baseKey));
    }
    for (DownloadRow &row : rows) {
        if (row.assigned)
            continue;
        const auto candidates = numberedFiles.value(row.baseKey);
        for (const auto &candidate : candidates) {
            if (assignPath(row, candidate.second))
                break;
        }
    }
}

void LibraryService::removeManagedDownloadImports()
{
    if (!m_db.isOpen())
        return;

    // 旧版本可能把应用下载目录里的文件作为 source=0 再次导入。先把文件按
    // “歌手 - 歌名”规则绑定回在线记录，再清理重复本地行，避免下载关联丢失。
    reconcileManagedDownloads();

    const QString managedDownloadDir = SettingsService::onlineDownloadDir();
    if (managedDownloadDir.isEmpty())
        return;

    QHash<QString, qint64> onlineIdsByPath;
    QSqlQuery onlineRows(m_db);
    if (onlineRows.exec(QStringLiteral(
            "SELECT id,download_path FROM songs WHERE source>0 AND download_path<>''"))) {
        while (onlineRows.next())
            onlineIdsByPath.insert(normalizedFileKey(onlineRows.value(1).toString()),
                                   onlineRows.value(0).toLongLong());
    }
    onlineRows.finish();

    struct ImportedRow {
        qint64 id = -1;
        qint64 onlineId = -1;
    };
    QList<ImportedRow> importedRows;
    QSqlQuery find(m_db);
    if (!find.exec(QStringLiteral("SELECT id,path FROM songs WHERE source=0")))
        return;
    while (find.next()) {
        const QString path = find.value(1).toString();
        if (isPathInside(path, managedDownloadDir))
            importedRows.append({ find.value(0).toLongLong(),
                                  onlineIdsByPath.value(normalizedFileKey(path), -1) });
    }
    find.finish();
    if (importedRows.isEmpty() || !m_db.transaction())
        return;

    bool ok = true;
    for (const ImportedRow &row : importedRows) {
        if (row.onlineId > 0) {
            QSqlQuery memberships(m_db);
            memberships.prepare(QStringLiteral(
                "INSERT OR IGNORE INTO playlist_songs(playlist_id,song_id,position) "
                "SELECT playlist_id,?,position FROM playlist_songs WHERE song_id=?"));
            memberships.addBindValue(row.onlineId);
            memberships.addBindValue(row.id);
            ok = memberships.exec();

            QSqlQuery recentValue(m_db);
            recentValue.prepare(QStringLiteral("SELECT played_ms FROM recent WHERE song_id=?"));
            recentValue.addBindValue(row.id);
            if (ok) {
                ok = recentValue.exec();
                if (ok && recentValue.next()) {
                    const qint64 playedMs = recentValue.value(0).toLongLong();
                    recentValue.finish();
                    QSqlQuery copyRecent(m_db);
                    copyRecent.prepare(QStringLiteral(
                        "INSERT INTO recent(song_id,played_ms) VALUES(?,?) "
                        "ON CONFLICT(song_id) DO UPDATE SET played_ms=MAX(played_ms,excluded.played_ms)"));
                    copyRecent.addBindValue(row.onlineId);
                    copyRecent.addBindValue(playedMs);
                    ok = copyRecent.exec();
                }
            }

            QSqlQuery mergeMetadata(m_db);
            mergeMetadata.prepare(QStringLiteral(
                "UPDATE songs SET "
                "play_count=MAX(play_count,COALESCE((SELECT play_count FROM songs WHERE id=?),0)),"
                "last_played_ms=MAX(last_played_ms,COALESCE((SELECT last_played_ms FROM songs WHERE id=?),0)),"
                "cover_path=CASE WHEN cover_path='' THEN COALESCE((SELECT cover_path FROM songs WHERE id=?),'') "
                "ELSE cover_path END WHERE id=?"));
            mergeMetadata.addBindValue(row.id);
            mergeMetadata.addBindValue(row.id);
            mergeMetadata.addBindValue(row.id);
            mergeMetadata.addBindValue(row.onlineId);
            if (ok)
                ok = mergeMetadata.exec();
        }
        if (!ok)
            break;
        for (const QString &sql : {
                 QStringLiteral("DELETE FROM playlist_songs WHERE song_id=?"),
                 QStringLiteral("DELETE FROM recent WHERE song_id=?"),
                 QStringLiteral("DELETE FROM song_cache WHERE song_id=?"),
                 QStringLiteral("DELETE FROM songs WHERE id=?") }) {
            QSqlQuery remove(m_db);
            remove.prepare(sql);
            remove.addBindValue(row.id);
            if (!remove.exec()) {
                ok = false;
                break;
            }
        }
        if (!ok)
            break;
    }
    if (ok)
        ok = m_db.commit();
    else
        m_db.rollback();
    if (!ok)
        qWarning() << "Failed to remove imported download files from library";
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
    connect(thread, &QThread::finished, this, [this, thread] {
        if (m_scanThread == thread)
            m_scanThread = nullptr;
    });
    const QString dbPath = m_dbPath;
    const QString managedDownloadDir = SettingsService::onlineDownloadDir();
    connect(this, &LibraryService::scanStarted, worker, [worker, folders, dbPath, managedDownloadDir] {
        worker->run(folders, dbPath, managedDownloadDir);
    });
    connect(worker, &ScanWorker::progress, this, &LibraryService::scanProgress);
    connect(worker, &ScanWorker::finished, this, [this, thread](int added, int removed, const QStringList &watchDirs) {
        m_scanRunning = false;
        thread->quit();
        m_watcher->removePaths(m_watcher->directories());
        m_watcher->addPaths(watchDirs);
        removeManagedDownloadImports();
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

    reconcileManagedDownloads();

    // song_cache 是缓存的唯一事实来源。启动时清掉数据库中已经不存在的缓存记录，
    // 同时把旧版本遗留的线上 missing 标记恢复为可用状态。
    QSqlQuery cacheRows(m_db);
    cacheRows.exec(QStringLiteral("SELECT song_id,cache_path FROM song_cache"));
    while (cacheRows.next()) {
        const qint64 songId = cacheRows.value(0).toLongLong();
        const QString path = cacheRows.value(1).toString();
        if (!QFileInfo::exists(path) || QFileInfo(path).size() <= 0) {
            QSqlQuery remove(m_db);
            remove.prepare(QStringLiteral("DELETE FROM song_cache WHERE song_id=?"));
            remove.addBindValue(songId);
            remove.exec();
            QSqlQuery clear(m_db);
            clear.prepare(QStringLiteral("UPDATE songs SET cache_path='' WHERE id=?"));
            clear.addBindValue(songId);
            clear.exec();
        }
    }
    // 永久下载路径即使暂时不可访问也必须保留。Song::isDownloaded() 会根据文件
    // 是否真实存在决定当前状态；路径保留后，磁盘或权限恢复时歌曲会自动重新出现。
    QSqlQuery repair(m_db);
    repair.exec(QStringLiteral("UPDATE songs SET missing=0 WHERE source>0"));
    repair.exec(QStringLiteral("UPDATE songs SET cache_path='' WHERE source=0"));
    repair.exec(QStringLiteral("UPDATE songs SET download_path='' WHERE source=0"));

    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
        "SELECT s.id,s.path,s.title,s.artist,s.album,s.duration_ms,s.cover_path,s.missing,"
        "s.play_count,s.last_played_ms,s.source,s.online_id,s.cover_url,s.album_id,sc.cache_path,s.download_path,"
        "s.remote_id,s.album_remote_id,s.artist_remote_id "
        "FROM songs s LEFT JOIN song_cache sc ON sc.song_id=s.id ORDER BY s.id"))) {
        m_lastError = q.lastError().text();
        return;
    }
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
        s.downloadPath = q.value(15).toString();
        s.remoteId = q.value(16).toString();
        s.albumRemoteId = q.value(17).toString();
        s.artistRemoteId = q.value(18).toString();
        if (s.remoteId.isEmpty() && s.onlineId > 0)
            s.remoteId = QString::number(s.onlineId);
        if (s.albumRemoteId.isEmpty() && s.albumId > 0)
            s.albumRemoteId = QString::number(s.albumId);
        s.lyricPath = LyricsLoader::existingSidecarPathFor(s);
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
    return songByRemoteId(source, QString::number(onlineId));
}

Song LibraryService::songByRemoteId(int source, const QString &remoteId) const
{
    for (const Song &s : m_songs)
        if (s.source == source && s.effectiveRemoteId() == remoteId)
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
    const QString remoteId = song.effectiveRemoteId();
    if (!m_db.isOpen() || !song.isOnline() || remoteId.isEmpty())
        return -1;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO songs(path,title,artist,album,duration_ms,cover_path,has_cover,cover_url,source,"
        "remote_id,online_id,album_remote_id,album_id,artist_remote_id,missing) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,0) "
        "ON CONFLICT(source,remote_id) WHERE source>0 AND remote_id<>'' DO UPDATE SET "
        "title=excluded.title,artist=excluded.artist,album=excluded.album,"
        "duration_ms=excluded.duration_ms,cover_url=excluded.cover_url,"
        "cover_path=CASE WHEN excluded.cover_path<>'' THEN excluded.cover_path ELSE songs.cover_path END,"
        "has_cover=CASE WHEN excluded.cover_path<>'' THEN excluded.has_cover ELSE songs.has_cover END,"
        "online_id=CASE WHEN excluded.online_id>0 THEN excluded.online_id ELSE songs.online_id END,"
        "album_remote_id=excluded.album_remote_id,album_id=excluded.album_id,"
        "artist_remote_id=excluded.artist_remote_id,missing=0"));
    q.addBindValue(song.filePath);
    q.addBindValue(song.title);
    q.addBindValue(song.artist);
    q.addBindValue(song.album);
    q.addBindValue(song.durationMs);
    q.addBindValue(song.coverPath);
    q.addBindValue(song.coverPath.isEmpty() ? 0 : 1);
    q.addBindValue(song.coverUrl);
    q.addBindValue(song.source);
    q.addBindValue(remoteId);
    q.addBindValue(song.onlineId);
    q.addBindValue(song.effectiveAlbumRemoteId());
    q.addBindValue(song.albumId);
    q.addBindValue(song.artistRemoteId);
    if (!q.exec())
        return -1;

    QSqlQuery sel(m_db);
    sel.prepare(QStringLiteral("SELECT id FROM songs WHERE source=? AND remote_id=?"));
    sel.addBindValue(song.source);
    sel.addBindValue(remoteId);
    sel.exec();
    if (!sel.next())
        return -1;
    const qint64 id = sel.value(0).toLongLong();

    for (Song &s : m_songs) {
        if (s.source == song.source && s.effectiveRemoteId() == remoteId) {
            s.id = id;
            s.title = song.title;
            s.artist = song.artist;
            s.album = song.album;
            s.durationMs = song.durationMs;
            // 元数据刷新不能抹掉已经下载的封面、缓存和外挂歌词。
            if (!song.coverPath.isEmpty())
                s.coverPath = song.coverPath;
            s.coverUrl = song.coverUrl;
            s.remoteId = remoteId;
            s.albumId = song.albumId;
            s.albumRemoteId = song.effectiveAlbumRemoteId();
            s.artistRemoteId = song.artistRemoteId;
            s.filePath = song.filePath;
            s.missing = false;
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
    if (!m_db.isOpen() || songId <= 0 || path.isEmpty())
        return;
    const Song current = songById(songId);
    if (current.id <= 0 || current.coverPath == path)
        return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE songs SET cover_path=?,has_cover=1 WHERE id=?"));
    q.addBindValue(path);
    q.addBindValue(songId);
    if (!q.exec())
        return;
    for (Song &s : m_songs) {
        if (s.id == songId) {
            s.coverPath = path;
            break;
        }
    }
    emit songCoverChanged(songId);
}

QString LibraryService::cacheDir() const
{
    // Keep cache data beside the database.  In production this is the normal
    // application-data directory; using the database root also keeps test and
    // portable database overrides self-contained and writable.
    const QString root = m_dbPath.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        : QFileInfo(m_dbPath).absolutePath();
    const QString dir = root + QStringLiteral("/cache");
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
    if (!song.hasRemoteIdentity())
        return {};
    const QByteArray identity = song.effectiveRemoteId().toUtf8();
    const QString key = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha1).toHex());
    return coverCacheDir() + QStringLiteral("/song_%1_%2.jpg").arg(song.source).arg(key);
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
    const QByteArray key = song.stableIdentity().toUtf8();
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
            s.lyricPath.clear();
            s.cachePath = path;
            s.lyricPath = LyricsLoader::existingSidecarPathFor(s);
            break;
        }
    }
    evictCacheIfNeeded();
    emit cacheChanged();
}

void LibraryService::invalidateSongCache(qint64 songId)
{
    if (!m_db.isOpen() || songId <= 0)
        return;
    QString path;
    QSqlQuery find(m_db);
    find.prepare(QStringLiteral("SELECT cache_path FROM song_cache WHERE song_id=?"));
    find.addBindValue(songId);
    if (find.exec() && find.next())
        path = find.value(0).toString();

    QSqlQuery remove(m_db);
    remove.prepare(QStringLiteral("DELETE FROM song_cache WHERE song_id=?"));
    remove.addBindValue(songId);
    remove.exec();
    QSqlQuery clear(m_db);
    clear.prepare(QStringLiteral("UPDATE songs SET cache_path='' WHERE id=?"));
    clear.addBindValue(songId);
    clear.exec();

    if (!path.isEmpty()) {
        const QString relative = QDir(cacheDir()).relativeFilePath(path);
        if (!QFileInfo(path).isRelative() && !relative.startsWith(QStringLiteral(".."))) {
            QFile::remove(path);
            QFile::remove(LyricsLoader::sidecarPathFor(path));
            LyricsLoader::invalidate(path);
        }
    }
    for (Song &song : m_songs) {
        if (song.id == songId) {
            song.lyricPath.clear();
            song.cachePath.clear();
            song.lyricPath = LyricsLoader::existingSidecarPathFor(song);
        }
    }
    emit cacheChanged();
    emit libraryChanged();
}

void LibraryService::clearCache()
{
    QDir dir(cacheDir());
    if (m_db.isOpen()) {
        QSqlQuery paths(m_db);
        paths.exec(QStringLiteral("SELECT cache_path FROM song_cache"));
        while (paths.next()) {
            const QString path = paths.value(0).toString();
            const QString relative = QDir(cacheDir()).relativeFilePath(path);
            if (!relative.startsWith(QStringLiteral("..")) && !QFileInfo(path).isRelative())
                QFile::remove(path);
        }
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("DELETE FROM song_cache"));
        q.exec(QStringLiteral("UPDATE songs SET cache_path='' WHERE source>0"));
    }
    // 清理数据库外的残留播放文件，但不触碰 covers/ 下的本地与线上封面。
    for (const QFileInfo &fi : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot))
        QFile::remove(fi.absoluteFilePath());
    for (Song &s : m_songs) {
        s.lyricPath.clear();
        s.cachePath.clear();
        s.lyricPath = LyricsLoader::existingSidecarPathFor(s);
    }
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

QString LibraryService::downloadDir() const
{
    const QString dir = SettingsService::onlineDownloadDir();
    QDir().mkpath(dir);
    return dir;
}

QString LibraryService::downloadFilePathFor(const Song &song) const
{
    if (!song.hasRemoteIdentity())
        return {};
    if (!song.downloadPath.isEmpty() && QFileInfo(song.downloadPath).isFile()
        && QFileInfo(song.downloadPath).size() > 0)
        return song.downloadPath;

    const QString base = downloadBaseName(song.title, song.artist, song.effectiveRemoteId());
    QString path = QDir(downloadDir()).filePath(base + QStringLiteral(".mp3"));
    int suffix = 2;
    while (QFileInfo::exists(path))
        path = QDir(downloadDir()).filePath(QStringLiteral("%1 (%2).mp3").arg(base).arg(suffix++));
    return path;
}

QString LibraryService::downloadPathFor(qint64 songId) const
{
    for (const Song &s : m_songs)
        if (s.id == songId)
            return s.downloadPath;
    if (!m_db.isOpen() || songId <= 0)
        return {};
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT download_path FROM songs WHERE id=? AND source>0"));
    q.addBindValue(songId);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

bool LibraryService::isSongDownloaded(qint64 songId) const
{
    const QString path = downloadPathFor(songId);
    return !path.isEmpty() && QFileInfo(path).isFile() && QFileInfo(path).size() > 0;
}

bool LibraryService::setSongDownloaded(qint64 songId, const QString &path)
{
    if (!m_db.isOpen() || songId <= 0 || path.isEmpty()
        || !QFileInfo(path).isFile() || QFileInfo(path).size() <= 0)
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE songs SET download_path=? WHERE id=? AND source>0"));
    q.addBindValue(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
    q.addBindValue(songId);
    if (!q.exec() || q.numRowsAffected() != 1)
        return false;
    bool found = false;
    for (Song &song : m_songs) {
        if (song.id == songId) {
            song.lyricPath.clear();
            song.downloadPath = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
            song.lyricPath = LyricsLoader::existingSidecarPathFor(song);
            found = true;
            break;
        }
    }
    if (!found)
        reloadSongs();
    emit libraryChanged();
    return true;
}

bool LibraryService::removeSongDownload(qint64 songId)
{
    if (!m_db.isOpen() || songId <= 0)
        return false;
    const QString path = downloadPathFor(songId);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE songs SET download_path='' WHERE id=? AND source>0"));
    q.addBindValue(songId);
    if (!q.exec() || q.numRowsAffected() != 1)
        return false;
    if (!path.isEmpty()) {
        QFile::remove(path);
        QFile::remove(LyricsLoader::sidecarPathFor(path));
        LyricsLoader::invalidate(path);
    }
    for (Song &song : m_songs) {
        if (song.id == songId) {
            song.lyricPath.clear();
            song.downloadPath.clear();
            song.lyricPath = LyricsLoader::existingSidecarPathFor(song);
            break;
        }
    }
    emit libraryChanged();
    return true;
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
        const qint64 fileBytes = QFileInfo(v.second).size();
        QFile::remove(v.second);
        QSqlQuery del(m_db);
        del.prepare(QStringLiteral("DELETE FROM song_cache WHERE song_id=?"));
        del.addBindValue(v.first);
        del.exec();
        QSqlQuery clear(m_db);
        clear.prepare(QStringLiteral("UPDATE songs SET cache_path='' WHERE id=?"));
        clear.addBindValue(v.first);
        clear.exec();
        --removeCount;
        removeBytes -= fileBytes;
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
