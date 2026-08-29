#include "MusicSource.h"

#include <utility>

namespace core {

namespace {
QStringList stringIds(const QList<qint64> &ids)
{
    QStringList result;
    result.reserve(ids.size());
    for (qint64 id : ids)
        result.append(QString::number(id));
    return result;
}
}

void MusicSource::songUrls(const QList<qint64> &ids, JsonArrayFn ok, ErrFn err)
{
    songUrls(stringIds(ids), std::move(ok), std::move(err));
}

void MusicSource::lyric(qint64 id, String3Fn ok, ErrFn err) { lyric(QString::number(id), std::move(ok), std::move(err)); }
void MusicSource::songDetail(qint64 id, OkFn ok, ErrFn err) { songDetail(QString::number(id), std::move(ok), std::move(err)); }
void MusicSource::albumDetail(qint64 id, OkFn ok, ErrFn err) { albumDetail(QString::number(id), std::move(ok), std::move(err)); }
void MusicSource::artistDetail(qint64 id, OkFn ok, ErrFn err) { artistDetail(QString::number(id), std::move(ok), std::move(err)); }
void MusicSource::artistSongs(qint64 id, JsonArrayFn ok, ErrFn err) { artistSongs(QString::number(id), std::move(ok), std::move(err)); }
void MusicSource::playlistDetail(qint64 id, OkFn ok, ErrFn err) { playlistDetail(QString::number(id), std::move(ok), std::move(err)); }
void MusicSource::playlistTracks(qint64 id, JsonArrayFn ok, ErrFn err) { playlistTracks(QString::number(id), std::move(ok), std::move(err)); }
void MusicSource::comments(qint64 id, int offset, int limit, OkFn ok, ErrFn err) { comments(QString::number(id), offset, limit, std::move(ok), std::move(err)); }
void MusicSource::userPlaylists(qint64 uid, JsonArrayFn ok, ErrFn err) { userPlaylists(QString::number(uid), std::move(ok), std::move(err)); }
void MusicSource::like(qint64 id, bool liked, OkFn ok, ErrFn err) { like(QString::number(id), liked, std::move(ok), std::move(err)); }
void MusicSource::likeList(qint64 uid, JsonArrayFn ok, ErrFn err) { likeList(QString::number(uid), std::move(ok), std::move(err)); }

Song MusicSource::makeOnlineSong(SourceId source, const QString &scheme, const QString &remoteId,
                                 const QString &title, const QString &artist, const QString &album,
                                 qint64 durationMs, const QString &coverUrl,
                                 const QString &albumRemoteId, const QString &artistRemoteId)
{
    Song song;
    song.source = int(source);
    song.remoteId = remoteId;
    song.albumRemoteId = albumRemoteId;
    song.artistRemoteId = artistRemoteId;
    if (source == SourceId::Netease) {
        song.onlineId = remoteId.toLongLong();
        song.albumId = albumRemoteId.toLongLong();
    }
    song.title = title;
    song.artist = artist;
    song.album = album;
    song.durationMs = durationMs;
    song.coverUrl = coverUrl;
    song.filePath = QStringLiteral("%1://%2").arg(scheme, remoteId);
    return song;
}

Song MusicSource::makeOnlineSong(int source, const QString &scheme, qint64 onlineId,
                                 const QString &title, const QString &artist, const QString &album,
                                 qint64 durationMs, const QString &coverUrl, qint64 albumId)
{
    return makeOnlineSong(static_cast<SourceId>(source), scheme, QString::number(onlineId), title,
                          artist, album, durationMs, coverUrl,
                          albumId > 0 ? QString::number(albumId) : QString());
}

} // namespace core
