#pragma once

#include "core/Song.h"

#include <QObject>
#include <QSqlDatabase>
#include <QStringList>

class QFileSystemWatcher;
class QTimer;

namespace core {

class LibraryService : public QObject
{
    Q_OBJECT
public:
    explicit LibraryService(QObject *parent = nullptr);
    ~LibraryService() override;

    bool openDatabase();
    void reloadDatabase();
    QString databasePath() const { return m_dbPath; }
    QSqlDatabase database() const { return m_db; }
    QString lastError() const;

    void startScan();
    void scanFolderNow(const QString &folder);

    QStringList folders() const;
    void setFolders(const QStringList &folders);

    QList<Song> allSongs() const { return m_songs; }
    Song songById(qint64 id) const;
    Song songByPath(const QString &path) const;
    Song songByRemoteId(int source, const QString &remoteId) const;
    Song songByOnlineId(int source, qint64 onlineId) const;
    int songCount() const { return m_songs.size(); }

    void markPlayed(qint64 songId);
    void removeSong(qint64 songId);

    // 在线歌曲
    qint64 upsertOnlineSong(const Song &song);
    void fillMissingSongMetadata(qint64 songId, const QString &artist, const QString &album);
    void setSongCoverPath(qint64 songId, const QString &path);
    QString songCoverCachePath(const Song &song) const;
    QString playlistCoverCachePath(qint64 playlistId) const;

    // 播放缓存
    QString cacheDir() const;
    QString coverCacheDir() const;
    QString cacheFilePathFor(const Song &song) const;
    QString cachePathFor(qint64 songId) const;
    bool isSongCached(qint64 songId) const;
    void setSongCached(qint64 songId, const QString &path, qint64 sizeBytes);
    void invalidateSongCache(qint64 songId);
    void clearCache();
    void cacheUsage(qint64 *bytes, int *count) const;

    // 用户主动下载(与自动播放缓存完全分离)
    QString downloadDir() const;
    QString downloadFilePathFor(const Song &song) const;
    QString downloadPathFor(qint64 songId) const;
    bool isSongDownloaded(qint64 songId) const;
    bool setSongDownloaded(qint64 songId, const QString &path);
    bool removeSongDownload(qint64 songId);

    static void setDatabasePathOverride(const QString &path);

signals:
    void scanStarted(int totalFolders);
    void scanProgress(int scanned, int total);
    void scanFinished(int added, int removed);
    void libraryChanged();
    void songCoverChanged(qint64 songId);
    void cacheChanged();

private:
    void reloadSongs();
    void reconcileManagedDownloads();
    void removeManagedDownloadImports();
    bool recoverCorruptDatabase(const QString &backupPath, QString *error);
    void startWorker(const QStringList &folders);
    void onWatchChange(const QString &path);
    void evictCacheIfNeeded();

    QString m_dbPath;
    QSqlDatabase m_db;
    QString m_lastError;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_watchTimer = nullptr;
    QStringList m_pendingFolders;
    QList<Song> m_songs;
    bool m_scanRunning = false;
    QThread *m_scanThread = nullptr;

    static QString s_dbOverride;
};

} // namespace core
