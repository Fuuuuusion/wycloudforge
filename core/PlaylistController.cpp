#include "PlaylistController.h"

#include "core/LyricsLoader.h"

#include <QDateTime>
#include <QFileInfo>
#include <QSqlQuery>
#include <QVariant>

namespace core {

PlaylistController::PlaylistController(QObject *parent)
    : QObject(parent)
{
}

void PlaylistController::setDatabase(const QSqlDatabase &db)
{
    m_db = db;
    reload();
}

void PlaylistController::reload()
{
    reloadPlaylists();
    emit playlistsChanged();
}

void PlaylistController::reloadPlaylists()
{
    m_playlists.clear();
    if (!m_db.isOpen())
        return;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral(
        "SELECT p.id,p.name,p.cover_path,p.description,COUNT(ps.song_id) FROM playlists p "
        "LEFT JOIN playlist_songs ps ON ps.playlist_id=p.id "
        "GROUP BY p.id ORDER BY p.id"));
    while (q.next()) {
        PlaylistInfo info;
        info.id = q.value(0).toInt();
        info.name = q.value(1).toString();
        info.coverPath = q.value(2).toString();
        info.description = q.value(3).toString();
        info.songCount = q.value(4).toInt();
        m_playlists.append(info);
    }
}

QList<Song> PlaylistController::songsOf(int playlistId) const
{
    QList<Song> songs;
    if (!m_db.isOpen())
        return songs;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT s.id,s.path,s.title,s.artist,s.album,s.duration_ms,s.cover_path,s.missing,s.play_count,s.last_played_ms,"
        "s.source,s.online_id,s.cover_url,s.album_id,COALESCE(NULLIF(sc.cache_path,''),s.cache_path,'') "
        "FROM playlist_songs ps JOIN songs s ON s.id=ps.song_id "
        "LEFT JOIN song_cache sc ON sc.song_id=s.id "
        "WHERE ps.playlist_id=? ORDER BY ps.position, s.id"));
    q.addBindValue(playlistId);
    q.exec();
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
        s.lyricPath = s.isOnline() ? QString() : LyricsLoader::sidecarPathFor(s.filePath);
        songs.append(s);
    }
    return songs;
}

bool PlaylistController::isInPlaylist(int playlistId, qint64 songId) const
{
    if (!m_db.isOpen())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT 1 FROM playlist_songs WHERE playlist_id=? AND song_id=?"));
    q.addBindValue(playlistId);
    q.addBindValue(songId);
    q.exec();
    return q.next();
}

bool PlaylistController::isFavorite(qint64 songId) const
{
    return isInPlaylist(favoritePlaylistId(), songId);
}

int PlaylistController::createPlaylist(const QString &name)
{
    if (!m_db.isOpen() || name.trimmed().isEmpty())
        return -1;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO playlists(name,created_ms) VALUES(?,?)"));
    q.addBindValue(name.trimmed());
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!q.exec())
        return -1;
    const int id = q.lastInsertId().toInt();
    reload();
    return id;
}

bool PlaylistController::renamePlaylist(int id, const QString &name)
{
    if (!m_db.isOpen() || name.trimmed().isEmpty() || id == favoritePlaylistId())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE playlists SET name=? WHERE id=?"));
    q.addBindValue(name.trimmed());
    q.addBindValue(id);
    if (!q.exec())
        return false;
    reload();
    return true;
}

bool PlaylistController::setPlaylistCover(int id, const QString &coverPath)
{
    const QFileInfo cover(coverPath);
    if (!m_db.isOpen() || id == favoritePlaylistId() || !cover.isFile())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE playlists SET cover_path=? WHERE id=?"));
    q.addBindValue(cover.absoluteFilePath());
    q.addBindValue(id);
    if (!q.exec())
        return false;
    reload();
    emit playlistsChanged();
    return true;
}

bool PlaylistController::setPlaylistDescription(int id, const QString &text)
{
    if (!m_db.isOpen())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE playlists SET description=? WHERE id=?"));
    q.addBindValue(text.trimmed());
    q.addBindValue(id);
    if (!q.exec())
        return false;
    reload();
    emit playlistsChanged();
    return true;
}

bool PlaylistController::deletePlaylist(int id)
{
    if (!m_db.isOpen() || id == favoritePlaylistId())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM playlists WHERE id=?"));
    q.addBindValue(id);
    q.exec();
    QSqlQuery q2(m_db);
    q2.prepare(QStringLiteral("DELETE FROM playlist_songs WHERE playlist_id=?"));
    q2.addBindValue(id);
    q2.exec();
    reload();
    return true;
}

bool PlaylistController::addSong(int playlistId, qint64 songId)
{
    if (!m_db.isOpen())
        return false;
    QSqlQuery exists(m_db);
    exists.prepare(QStringLiteral(
        "SELECT EXISTS(SELECT 1 FROM playlists WHERE id=?),"
        "EXISTS(SELECT 1 FROM songs WHERE id=?)"));
    exists.addBindValue(playlistId);
    exists.addBindValue(songId);
    if (!exists.exec() || !exists.next() || !exists.value(0).toBool() || !exists.value(1).toBool())
        return false;
    int pos = 0;
    QSqlQuery count(m_db);
    count.prepare(QStringLiteral("SELECT COUNT(*) FROM playlist_songs WHERE playlist_id=?"));
    count.addBindValue(playlistId);
    count.exec();
    if (count.next())
        pos = count.value(0).toInt();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO playlist_songs(playlist_id,song_id,position) VALUES(?,?,?)"));
    q.addBindValue(playlistId);
    q.addBindValue(songId);
    q.addBindValue(pos);
    const bool ok = q.exec();
    if (ok) {
        reload();
        emit playlistSongsChanged(playlistId);
    }
    return ok;
}

bool PlaylistController::removeSong(int playlistId, qint64 songId)
{
    if (!m_db.isOpen())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM playlist_songs WHERE playlist_id=? AND song_id=?"));
    q.addBindValue(playlistId);
    q.addBindValue(songId);
    const bool ok = q.exec();
    if (ok) {
        reload();
        emit playlistSongsChanged(playlistId);
    }
    return ok;
}

bool PlaylistController::moveSong(int playlistId, int from, int to)
{
    if (!m_db.isOpen() || from == to)
        return false;
    QList<qint64> ids;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT song_id FROM playlist_songs WHERE playlist_id=? ORDER BY position, song_id"));
    q.addBindValue(playlistId);
    q.exec();
    while (q.next())
        ids.append(q.value(0).toLongLong());
    if (from < 0 || from >= ids.size() || to < 0 || to >= ids.size())
        return false;
    const qint64 id = ids.takeAt(from);
    ids.insert(to, id);
    QSqlQuery upd(m_db);
    upd.prepare(QStringLiteral("UPDATE playlist_songs SET position=? WHERE playlist_id=? AND song_id=?"));
    for (int i = 0; i < ids.size(); ++i) {
        upd.addBindValue(i);
        upd.addBindValue(playlistId);
        upd.addBindValue(ids[i]);
        upd.exec();
    }
    reload();
    emit playlistSongsChanged(playlistId);
    return true;
}

bool PlaylistController::setFavorite(qint64 songId, bool favorite)
{
    const bool ok = favorite ? addSong(favoritePlaylistId(), songId)
                             : removeSong(favoritePlaylistId(), songId);
    if (ok)
        emit favoritesChanged(songId, favorite);
    return ok;
}

QList<Song> PlaylistController::recentSongs(int limit) const
{
    QList<Song> songs;
    if (!m_db.isOpen())
        return songs;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT s.id,s.path,s.title,s.artist,s.album,s.duration_ms,s.cover_path,s.missing,s.play_count,s.last_played_ms,"
        "s.source,s.online_id,s.cover_url,s.album_id,COALESCE(NULLIF(sc.cache_path,''),s.cache_path,'') "
        "FROM recent r JOIN songs s ON s.id=r.song_id ORDER BY r.played_ms DESC LIMIT ?"));
    q.addBindValue(limit);
    q.exec();
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
        s.lyricPath = s.isOnline() ? QString() : LyricsLoader::sidecarPathFor(s.filePath);
        songs.append(s);
    }
    return songs;
}

void PlaylistController::recordPlay(qint64 songId)
{
    if (!m_db.isOpen())
        return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO recent(song_id,played_ms) VALUES(?,?) "
        "ON CONFLICT(song_id) DO UPDATE SET played_ms=excluded.played_ms"));
    q.addBindValue(songId);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.exec();
}

} // namespace core
