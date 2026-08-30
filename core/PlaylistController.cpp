#include "PlaylistController.h"

#include "core/LyricsLoader.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QVariant>

namespace core {

PlaylistController::PlaylistController(QObject *parent)
    : QObject(parent)
{
}

void PlaylistController::setDatabase(const QSqlDatabase &db)
{
    m_db = db;
    m_lastError.clear();
    reload();
}

void PlaylistController::reload()
{
    if (reloadPlaylists())
        emit playlistsChanged();
}

bool PlaylistController::reloadPlaylists()
{
    m_playlists.clear();
    if (!m_db.isOpen())
        return fail(QStringLiteral("读取歌单"), QStringLiteral("数据库未打开"));
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
        "SELECT p.id,p.name,p.cover_path,p.description,COUNT(ps.song_id) FROM playlists p "
        "LEFT JOIN playlist_songs ps ON ps.playlist_id=p.id "
        "GROUP BY p.id ORDER BY p.id")))
        return fail(QStringLiteral("读取歌单"), q.lastError().text());
    while (q.next()) {
        PlaylistInfo info;
        info.id = q.value(0).toInt();
        info.name = q.value(1).toString();
        info.coverPath = q.value(2).toString();
        info.description = q.value(3).toString();
        info.songCount = q.value(4).toInt();
        m_playlists.append(info);
    }
    m_lastError.clear();
    return true;
}

bool PlaylistController::fail(const QString &action, const QString &detail)
{
    m_lastError = detail.isEmpty() ? action : QStringLiteral("%1失败：%2").arg(action, detail);
    emit operationFailed(m_lastError);
    return false;
}

QList<Song> PlaylistController::songsOf(int playlistId) const
{
    QList<Song> songs;
    if (!m_db.isOpen())
        return songs;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT s.id,s.path,s.title,s.artist,s.album,s.duration_ms,s.cover_path,s.missing,s.play_count,s.last_played_ms,"
        "s.source,s.online_id,s.cover_url,s.album_id,COALESCE(NULLIF(sc.cache_path,''),s.cache_path,''),s.download_path,"
        "s.remote_id,s.album_remote_id,s.artist_remote_id "
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
        s.downloadPath = q.value(15).toString();
        s.remoteId = q.value(16).toString();
        s.albumRemoteId = q.value(17).toString();
        s.artistRemoteId = q.value(18).toString();
        if (s.remoteId.isEmpty() && s.onlineId > 0)
            s.remoteId = QString::number(s.onlineId);
        if (s.albumRemoteId.isEmpty() && s.albumId > 0)
            s.albumRemoteId = QString::number(s.albumId);
        s.lyricPath = LyricsLoader::existingSidecarPathFor(s);
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
    if (!m_db.isOpen() || name.trimmed().isEmpty()) {
        fail(QStringLiteral("创建歌单"), QStringLiteral("名称为空或数据库未打开"));
        return -1;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO playlists(name,created_ms) VALUES(?,?)"));
    q.addBindValue(name.trimmed());
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!q.exec()) {
        fail(QStringLiteral("创建歌单"), q.lastError().text());
        return -1;
    }
    const int id = q.lastInsertId().toInt();
    reload();
    return id;
}

bool PlaylistController::renamePlaylist(int id, const QString &name)
{
    if (!m_db.isOpen() || name.trimmed().isEmpty() || id == favoritePlaylistId())
        return fail(QStringLiteral("重命名歌单"), QStringLiteral("歌单或名称无效"));
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE playlists SET name=? WHERE id=?"));
    q.addBindValue(name.trimmed());
    q.addBindValue(id);
    if (!q.exec())
        return fail(QStringLiteral("重命名歌单"), q.lastError().text());
    if (q.numRowsAffected() != 1)
        return fail(QStringLiteral("重命名歌单"), QStringLiteral("目标歌单不存在"));
    reload();
    return true;
}

bool PlaylistController::setPlaylistCover(int id, const QString &coverPath)
{
    const QFileInfo cover(coverPath);
    if (!m_db.isOpen() || id == favoritePlaylistId() || !cover.isFile())
        return fail(QStringLiteral("设置歌单封面"), QStringLiteral("歌单或封面文件无效"));
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE playlists SET cover_path=? WHERE id=?"));
    q.addBindValue(cover.absoluteFilePath());
    q.addBindValue(id);
    if (!q.exec())
        return fail(QStringLiteral("设置歌单封面"), q.lastError().text());
    reload();
    return true;
}

bool PlaylistController::setPlaylistDescription(int id, const QString &text)
{
    if (!m_db.isOpen())
        return fail(QStringLiteral("保存歌单简介"), QStringLiteral("数据库未打开"));
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE playlists SET description=? WHERE id=?"));
    q.addBindValue(text.trimmed());
    q.addBindValue(id);
    if (!q.exec())
        return fail(QStringLiteral("保存歌单简介"), q.lastError().text());
    reload();
    return true;
}

bool PlaylistController::deletePlaylist(int id)
{
    if (!m_db.isOpen() || id == favoritePlaylistId())
        return fail(QStringLiteral("删除歌单"), QStringLiteral("歌单无效"));
    if (!m_db.transaction())
        return fail(QStringLiteral("删除歌单"), m_db.lastError().text());
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM playlist_songs WHERE playlist_id=?"));
    q.addBindValue(id);
    if (!q.exec()) {
        m_db.rollback();
        return fail(QStringLiteral("删除歌单"), q.lastError().text());
    }
    QSqlQuery q2(m_db);
    q2.prepare(QStringLiteral("DELETE FROM playlists WHERE id=?"));
    q2.addBindValue(id);
    if (!q2.exec() || q2.numRowsAffected() != 1) {
        const QString detail = q2.lastError().text().isEmpty()
            ? QStringLiteral("目标歌单不存在") : q2.lastError().text();
        m_db.rollback();
        return fail(QStringLiteral("删除歌单"), detail);
    }
    if (!m_db.commit()) {
        m_db.rollback();
        return fail(QStringLiteral("删除歌单"), m_db.lastError().text());
    }
    reload();
    return true;
}

bool PlaylistController::addSong(int playlistId, qint64 songId)
{
    if (!m_db.isOpen())
        return fail(QStringLiteral("添加歌曲到歌单"), QStringLiteral("数据库未打开"));
    if (!m_db.transaction())
        return fail(QStringLiteral("添加歌曲到歌单"), m_db.lastError().text());
    QSqlQuery exists(m_db);
    exists.prepare(QStringLiteral(
        "SELECT EXISTS(SELECT 1 FROM playlists WHERE id=?),"
        "EXISTS(SELECT 1 FROM songs WHERE id=?)"));
    exists.addBindValue(playlistId);
    exists.addBindValue(songId);
    if (!exists.exec() || !exists.next()) {
        const QString detail = exists.lastError().text();
        m_db.rollback();
        return fail(QStringLiteral("添加歌曲到歌单"), detail);
    }
    if (!exists.value(0).toBool() || !exists.value(1).toBool()) {
        m_db.rollback();
        return fail(QStringLiteral("添加歌曲到歌单"), QStringLiteral("歌单或歌曲记录不存在"));
    }
    QSqlQuery membership(m_db);
    membership.prepare(QStringLiteral("SELECT 1 FROM playlist_songs WHERE playlist_id=? AND song_id=?"));
    membership.addBindValue(playlistId);
    membership.addBindValue(songId);
    if (!membership.exec()) {
        const QString detail = membership.lastError().text();
        m_db.rollback();
        return fail(QStringLiteral("添加歌曲到歌单"), detail);
    }
    if (membership.next()) {
        m_db.rollback();
        return true;
    }
    int pos = 0;
    QSqlQuery count(m_db);
    count.prepare(QStringLiteral("SELECT COALESCE(MAX(position),-1)+1 FROM playlist_songs WHERE playlist_id=?"));
    count.addBindValue(playlistId);
    if (!count.exec() || !count.next()) {
        const QString detail = count.lastError().text();
        m_db.rollback();
        return fail(QStringLiteral("添加歌曲到歌单"), detail);
    }
    pos = count.value(0).toInt();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO playlist_songs(playlist_id,song_id,position) VALUES(?,?,?)"));
    q.addBindValue(playlistId);
    q.addBindValue(songId);
    q.addBindValue(pos);
    if (!q.exec()) {
        const QString detail = q.lastError().text();
        m_db.rollback();
        return fail(QStringLiteral("添加歌曲到歌单"), detail);
    }
    if (!m_db.commit()) {
        m_db.rollback();
        return fail(QStringLiteral("添加歌曲到歌单"), m_db.lastError().text());
    }
    if (!isInPlaylist(playlistId, songId))
        return fail(QStringLiteral("添加歌曲到歌单"), QStringLiteral("写入后校验失败"));
    reload();
    emit playlistSongsChanged(playlistId);
    return true;
}

PlaylistController::BatchResult PlaylistController::addSongsBatch(
    int playlistId, const QList<qint64> &songIds)
{
    BatchResult result;
    result.requested = songIds.size();
    QList<qint64> uniqueIds;
    QSet<qint64> seen;
    for (qint64 songId : songIds) {
        if (songId <= 0) {
            fail(QStringLiteral("批量添加歌曲到歌单"), QStringLiteral("歌曲记录无效"));
            return result;
        }
        if (seen.contains(songId)) {
            ++result.unchanged;
            continue;
        }
        seen.insert(songId);
        uniqueIds.append(songId);
    }
    if (uniqueIds.isEmpty()) {
        result.success = true;
        return result;
    }
    if (!m_db.isOpen()) {
        fail(QStringLiteral("批量添加歌曲到歌单"), QStringLiteral("数据库未打开"));
        return result;
    }
    if (!m_db.transaction()) {
        fail(QStringLiteral("批量添加歌曲到歌单"), m_db.lastError().text());
        return result;
    }

    auto rollbackFailure = [this, &result](const QString &detail) {
        m_db.rollback();
        result.changed = 0;
        result.success = false;
        fail(QStringLiteral("批量添加歌曲到歌单"), detail);
        return result;
    };

    QSqlQuery playlistExists(m_db);
    playlistExists.prepare(QStringLiteral("SELECT 1 FROM playlists WHERE id=?"));
    playlistExists.addBindValue(playlistId);
    if (!playlistExists.exec() || !playlistExists.next()) {
        const QString detail = playlistExists.lastError().text().isEmpty()
            ? QStringLiteral("目标歌单不存在") : playlistExists.lastError().text();
        playlistExists.finish();
        return rollbackFailure(detail);
    }
    playlistExists.finish();

    QSqlQuery nextPosition(m_db);
    nextPosition.prepare(QStringLiteral(
        "SELECT COALESCE(MAX(position),-1)+1 FROM playlist_songs WHERE playlist_id=?"));
    nextPosition.addBindValue(playlistId);
    if (!nextPosition.exec() || !nextPosition.next()) {
        const QString detail = nextPosition.lastError().text();
        nextPosition.finish();
        return rollbackFailure(detail);
    }
    int position = nextPosition.value(0).toInt();
    nextPosition.finish();

    QSqlQuery songExists(m_db);
    songExists.prepare(QStringLiteral("SELECT 1 FROM songs WHERE id=?"));
    QSqlQuery membership(m_db);
    membership.prepare(QStringLiteral(
        "SELECT 1 FROM playlist_songs WHERE playlist_id=? AND song_id=?"));
    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral(
        "INSERT INTO playlist_songs(playlist_id,song_id,position) VALUES(?,?,?)"));

    for (qint64 songId : uniqueIds) {
        songExists.bindValue(0, songId);
        if (!songExists.exec() || !songExists.next()) {
            const QString detail = songExists.lastError().text().isEmpty()
                ? QStringLiteral("歌曲记录不存在：%1").arg(songId)
                : songExists.lastError().text();
            songExists.finish();
            return rollbackFailure(detail);
        }
        songExists.finish();

        membership.bindValue(0, playlistId);
        membership.bindValue(1, songId);
        if (!membership.exec()) {
            const QString detail = membership.lastError().text();
            membership.finish();
            return rollbackFailure(detail);
        }
        const bool alreadyAdded = membership.next();
        membership.finish();
        if (alreadyAdded) {
            ++result.unchanged;
            continue;
        }

        insert.bindValue(0, playlistId);
        insert.bindValue(1, songId);
        insert.bindValue(2, position++);
        if (!insert.exec())
            return rollbackFailure(insert.lastError().text());
        ++result.changed;
    }
    songExists.finish();
    membership.finish();
    insert.finish();

    if (!m_db.commit())
        return rollbackFailure(m_db.lastError().text());
    result.success = true;
    reloadPlaylists();
    emit playlistSongsChanged(playlistId);
    return result;
}

bool PlaylistController::removeSong(int playlistId, qint64 songId)
{
    if (!m_db.isOpen())
        return fail(QStringLiteral("从歌单移除歌曲"), QStringLiteral("数据库未打开"));
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM playlist_songs WHERE playlist_id=? AND song_id=?"));
    q.addBindValue(playlistId);
    q.addBindValue(songId);
    const bool ok = q.exec();
    if (!ok)
        return fail(QStringLiteral("从歌单移除歌曲"), q.lastError().text());
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
        upd.bindValue(0, i);
        upd.bindValue(1, playlistId);
        upd.bindValue(2, ids[i]);
        if (!upd.exec())
            return fail(QStringLiteral("调整歌单顺序"), upd.lastError().text());
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

PlaylistController::BatchResult PlaylistController::setFavoritesBatch(
    const QList<qint64> &songIds, bool favorite)
{
    if (favorite) {
        BatchResult result = addSongsBatch(favoritePlaylistId(), songIds);
        if (result.success) {
            QSet<qint64> emitted;
            for (qint64 songId : songIds) {
                if (songId > 0 && !emitted.contains(songId)) {
                    emitted.insert(songId);
                    emit favoritesChanged(songId, true);
                }
            }
        }
        return result;
    }

    BatchResult result;
    result.requested = songIds.size();
    QList<qint64> uniqueIds;
    QSet<qint64> seen;
    for (qint64 songId : songIds) {
        if (songId <= 0) {
            fail(QStringLiteral("批量取消收藏"), QStringLiteral("歌曲记录无效"));
            return result;
        }
        if (seen.contains(songId)) {
            ++result.unchanged;
            continue;
        }
        seen.insert(songId);
        uniqueIds.append(songId);
    }
    if (uniqueIds.isEmpty()) {
        result.success = true;
        return result;
    }
    if (!m_db.isOpen()) {
        fail(QStringLiteral("批量取消收藏"), QStringLiteral("数据库未打开"));
        return result;
    }
    if (!m_db.transaction()) {
        fail(QStringLiteral("批量取消收藏"), m_db.lastError().text());
        return result;
    }

    QSqlQuery remove(m_db);
    remove.prepare(QStringLiteral(
        "DELETE FROM playlist_songs WHERE playlist_id=? AND song_id=?"));
    for (qint64 songId : uniqueIds) {
        remove.bindValue(0, favoritePlaylistId());
        remove.bindValue(1, songId);
        if (!remove.exec()) {
            const QString detail = remove.lastError().text();
            remove.finish();
            m_db.rollback();
            result.changed = 0;
            fail(QStringLiteral("批量取消收藏"), detail);
            return result;
        }
        if (remove.numRowsAffected() > 0)
            ++result.changed;
        else
            ++result.unchanged;
    }
    remove.finish();
    if (!m_db.commit()) {
        const QString detail = m_db.lastError().text();
        m_db.rollback();
        result.changed = 0;
        fail(QStringLiteral("批量取消收藏"), detail);
        return result;
    }
    result.success = true;
    reloadPlaylists();
    emit playlistSongsChanged(favoritePlaylistId());
    for (qint64 songId : uniqueIds)
        emit favoritesChanged(songId, false);
    return result;
}

QList<Song> PlaylistController::recentSongs(int limit) const
{
    QList<Song> songs;
    if (!m_db.isOpen())
        return songs;
    QSqlQuery q(m_db);
    const int safeLimit = qBound(1, limit, 1000);
    const QString sql = QStringLiteral(
        "SELECT s.id,s.path,s.title,s.artist,s.album,s.duration_ms,s.cover_path,s.missing,s.play_count,s.last_played_ms,"
        "s.source,s.online_id,s.cover_url,s.album_id,COALESCE(NULLIF(sc.cache_path,''),s.cache_path,''),s.download_path,"
        "s.remote_id,s.album_remote_id,s.artist_remote_id "
        "FROM recent r JOIN songs s ON s.id=r.song_id "
        "LEFT JOIN song_cache sc ON sc.song_id=s.id "
        "ORDER BY r.played_ms DESC, r.rowid DESC LIMIT %1").arg(safeLimit);
    if (!q.exec(sql)) {
        qWarning() << "Failed to load recent songs:" << q.lastError().text();
        return songs;
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
        "INSERT OR REPLACE INTO recent(song_id,played_ms) VALUES(?,?)"));
    q.addBindValue(songId);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!q.exec())
        fail(QStringLiteral("记录最近播放"), q.lastError().text());
}

} // namespace core
