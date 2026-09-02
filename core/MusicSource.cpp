#include "MusicSource.h"

#include <QSet>

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

void MusicSource::songUrls(const QList<Song> &songs, JsonArrayFn ok, ErrFn err)
{
    QStringList ids;
    ids.reserve(songs.size());
    for (const Song &song : songs) {
        const QString id = song.effectiveRemoteId();
        if (!id.isEmpty())
            ids.append(id);
    }
    songUrls(ids, std::move(ok), std::move(err));
}

OnlinePlaylist MusicSource::playlistFromJson(const QJsonObject &obj) const
{
    OnlinePlaylist playlist;
    playlist.source = sourceId();
    playlist.remoteId = obj.value(QStringLiteral("remoteId")).toVariant().toString().trimmed();
    if (playlist.remoteId.isEmpty())
        playlist.remoteId = obj.value(QStringLiteral("id")).toVariant().toString().trimmed();
    playlist.name = obj.value(QStringLiteral("name")).toString().trimmed();
    if (playlist.name.isEmpty())
        playlist.name = obj.value(QStringLiteral("title")).toString().trimmed();
    playlist.coverUrl = obj.value(QStringLiteral("coverUrl")).toString().trimmed();
    if (playlist.coverUrl.isEmpty())
        playlist.coverUrl = obj.value(QStringLiteral("coverImgUrl")).toString().trimmed();
    if (playlist.coverUrl.isEmpty())
        playlist.coverUrl = obj.value(QStringLiteral("picUrl")).toString().trimmed();
    playlist.description = obj.value(QStringLiteral("description")).toString().trimmed();
    return playlist;
}

void MusicSource::userPlaylistItems(const QString &uid, OnlinePlaylistsFn ok, ErrFn err)
{
    userPlaylists(uid, [this, ok = std::move(ok)](const QJsonArray &array) {
        QList<OnlinePlaylist> playlists;
        playlists.reserve(array.size());
        QSet<QString> identities;
        for (const QJsonValue &value : array) {
            const OnlinePlaylist playlist = playlistFromJson(value.toObject());
            if (!playlist.isValid() || identities.contains(playlist.stableIdentity()))
                continue;
            identities.insert(playlist.stableIdentity());
            playlists.append(playlist);
        }
        if (ok)
            ok(playlists);
    }, std::move(err));
}

void MusicSource::search(const SearchRequest &request, SearchResponseFn ok, ErrFn err)
{
    if (!request.isValid()) {
        if (err)
            err(QStringLiteral("搜索关键词或分页参数无效"));
        return;
    }
    if (request.category != SearchCategory::All
        && request.category != SearchCategory::Songs) {
        if (err)
            err(QStringLiteral("%1暂不支持该搜索分类").arg(sourceName()));
        return;
    }

    searchSongsPage(request.keywords.trimmed(), request.limit, request.offset,
                    [this, request, ok = std::move(ok)](const QJsonArray &array) mutable {
        SearchResponse response;
        response.source = sourceId();
        response.category = request.category;
        response.offset = request.offset;
        response.generation = request.generation;
        response.hasMore = array.size() >= request.limit;
        response.items.reserve(array.size());
        int rank = request.offset;
        for (const QJsonValue &value : array) {
            const Song song = songFromJson(value.toObject());
            if (!song.hasRemoteIdentity())
                continue;
            SearchResultItem item;
            item.type = SearchItemType::Song;
            item.source = sourceId();
            item.remoteId = song.effectiveRemoteId();
            item.title = song.title;
            item.subtitle = song.artist;
            item.artist = song.artist;
            item.album = song.album;
            item.coverUrl = song.coverUrl;
            item.durationMs = song.durationMs;
            item.sourceRank = rank++;
            item.song = song;
            response.items.append(item);
        }
        if (ok)
            ok(response);
    }, std::move(err));
}

void MusicSource::cancelSearch(quint64 generation)
{
    Q_UNUSED(generation)
}

void MusicSource::searchSuggestions(const QString &, int, SearchSuggestionsFn, ErrFn err)
{
    if (err)
        err(QStringLiteral("%1暂不支持搜索联想").arg(sourceName()));
}

void MusicSource::hotSearch(int, HotSearchFn, ErrFn err)
{
    if (err)
        err(QStringLiteral("%1暂不支持热搜").arg(sourceName()));
}

void MusicSource::defaultSearchText(StringFn, ErrFn err)
{
    if (err)
        err(QStringLiteral("%1暂不支持默认搜索词").arg(sourceName()));
}

void MusicSource::lyric(qint64 id, String3Fn ok, ErrFn err) { lyric(QString::number(id), std::move(ok), std::move(err)); }
void MusicSource::songDetail(qint64 id, OkFn ok, ErrFn err) { songDetail(QString::number(id), std::move(ok), std::move(err)); }
void MusicSource::albumDetail(qint64 id, OkFn ok, ErrFn err) { albumDetail(QString::number(id), std::move(ok), std::move(err)); }
void MusicSource::artistDetail(qint64 id, OkFn ok, ErrFn err) { artistDetail(QString::number(id), std::move(ok), std::move(err)); }
void MusicSource::artistSongs(qint64 id, JsonArrayFn ok, ErrFn err) { artistSongs(QString::number(id), std::move(ok), std::move(err)); }
void MusicSource::playlistDetail(qint64 id, OkFn ok, ErrFn err) { playlistDetail(QString::number(id), std::move(ok), std::move(err)); }
void MusicSource::playlistTracks(qint64 id, JsonArrayFn ok, ErrFn err) { playlistTracks(QString::number(id), std::move(ok), std::move(err)); }
void MusicSource::userPlaylists(qint64 uid, JsonArrayFn ok, ErrFn err) { userPlaylists(QString::number(uid), std::move(ok), std::move(err)); }

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
