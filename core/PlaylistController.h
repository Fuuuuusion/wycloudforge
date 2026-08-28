#pragma once

#include "core/Song.h"

#include <QObject>
#include <QSqlDatabase>

namespace core {

class PlaylistController : public QObject
{
    Q_OBJECT
public:
    struct PlaylistInfo
    {
        int id = -1;
        QString name;
        QString coverPath;
        QString description;
        int songCount = 0;
    };

    explicit PlaylistController(QObject *parent = nullptr);

    void setDatabase(const QSqlDatabase &db);
    void reload();
    QString lastError() const { return m_lastError; }

    QList<PlaylistInfo> playlists() const { return m_playlists; }
    int favoritePlaylistId() const { return 1; }

    QList<Song> songsOf(int playlistId) const;
    bool isInPlaylist(int playlistId, qint64 songId) const;
    bool isFavorite(qint64 songId) const;

    int createPlaylist(const QString &name);
    bool renamePlaylist(int id, const QString &name);
    bool setPlaylistCover(int id, const QString &coverPath);
    bool setPlaylistDescription(int id, const QString &text);
    bool deletePlaylist(int id);

    bool addSong(int playlistId, qint64 songId);
    bool removeSong(int playlistId, qint64 songId);
    bool moveSong(int playlistId, int from, int to);
    bool setFavorite(qint64 songId, bool favorite);

    QList<Song> recentSongs(int limit = 40) const;
    void recordPlay(qint64 songId);

signals:
    void playlistsChanged();
    void playlistSongsChanged(int playlistId);
    void favoritesChanged(qint64 songId, bool favorite);
    void operationFailed(const QString &message);

private:
    QSqlDatabase db() const { return m_db; }
    bool reloadPlaylists();
    bool fail(const QString &action, const QString &detail = QString());

    QSqlDatabase m_db;
    QList<PlaylistInfo> m_playlists;
    QString m_lastError;
};

} // namespace core
