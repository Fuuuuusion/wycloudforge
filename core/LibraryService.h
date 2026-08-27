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
    int songCount() const { return m_songs.size(); }

    void markPlayed(qint64 songId);
    void removeSong(qint64 songId);

    static void setDatabasePathOverride(const QString &path);

signals:
    void scanStarted(int totalFolders);
    void scanProgress(int scanned, int total);
    void scanFinished(int added, int removed);
    void libraryChanged();

private:
    void reloadSongs();
    void startWorker(const QStringList &folders);
    void onWatchChange(const QString &path);

    QString m_dbPath;
    QSqlDatabase m_db;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_watchTimer = nullptr;
    QStringList m_pendingFolders;
    QList<Song> m_songs;
    bool m_scanRunning = false;
    QThread *m_scanThread = nullptr;

    static QString s_dbOverride;
};

} // namespace core
