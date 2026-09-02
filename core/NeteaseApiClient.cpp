#include "NeteaseApiClient.h"

#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <utility>

namespace core {
namespace {

QJsonObject parseObject(QNetworkReply *reply, QString *error)
{
    const QByteArray data = reply->readAll();
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("响应解析失败");
        return {};
    }
    return doc.object();
}

QString objectId(const QJsonObject &object, const QString &key = QStringLiteral("id"))
{
    return object.value(key).toVariant().toString().trimmed();
}

QString artistNames(const QJsonObject &object)
{
    QJsonArray artists = object.value(QStringLiteral("ar")).toArray();
    if (artists.isEmpty())
        artists = object.value(QStringLiteral("artists")).toArray();
    QStringList names;
    for (const QJsonValue &value : artists) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString().trimmed();
        if (!name.isEmpty())
            names.append(name);
    }
    if (names.isEmpty()) {
        const QString name = object.value(QStringLiteral("artist")).toObject()
                                 .value(QStringLiteral("name")).toString().trimmed();
        if (!name.isEmpty())
            names.append(name);
    }
    return names.join(QLatin1Char('/'));
}

QJsonArray firstArray(const QJsonObject &object, const QStringList &keys)
{
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isArray())
            return value.toArray();
    }
    return {};
}

QJsonObject firstObject(const QJsonObject &object, const QStringList &keys)
{
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isObject())
            return value.toObject();
    }
    return {};
}

SearchResultItem songSearchItem(const NeteaseApiClient *source, const QJsonObject &object,
                                SearchItemType type, int rank)
{
    SearchResultItem item;
    item.type = type;
    item.source = SourceId::Netease;
    item.song = source->songFromJson(object);
    item.remoteId = item.song.effectiveRemoteId();
    item.title = item.song.title;
    item.artist = item.song.artist;
    item.album = item.song.album;
    item.coverUrl = item.song.coverUrl;
    item.durationMs = item.song.durationMs;
    item.sourceRank = rank;
    item.popularity = object.value(QStringLiteral("pop")).toDouble(-1.0);
    const QJsonObject privilege = object.value(QStringLiteral("privilege")).toObject();
    const int privilegeState = privilege.value(QStringLiteral("st")).toInt(0);
    const bool noCopyright = object.value(QStringLiteral("noCopyrightRcmd")).isObject()
        || object.value(QStringLiteral("noCopyrightRcmd")).isString();
    if (privilegeState < 0 || noCopyright) {
        item.playable = false;
        item.availabilityError = QStringLiteral("网易云版权或地区限制");
    } else if (object.value(QStringLiteral("fee")).toInt(0) > 0) {
        // 会员账号可能仍可正常播放，因此这里仅保留提示；最终可播放性
        // 继续以播放前实时解析出的媒体地址为准。
        item.availabilityError = QStringLiteral("可能需要会员或付费权限");
    }
    if (type == SearchItemType::Lyric) {
        const QJsonArray lyrics = object.value(QStringLiteral("lyrics")).toArray();
        for (const QJsonValue &value : lyrics) {
            QString text = value.isString()
                ? value.toString()
                : value.toObject().value(QStringLiteral("txt")).toString();
            text.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
            text = text.trimmed();
            if (!text.isEmpty()) {
                item.subtitle = text;
                break;
            }
        }
        if (item.subtitle.isEmpty())
            item.subtitle = item.artist;
    } else {
        item.subtitle = item.artist;
    }
    return item;
}

SearchResultItem artistSearchItem(const QJsonObject &object, int rank)
{
    SearchResultItem item;
    item.type = SearchItemType::Artist;
    item.source = SourceId::Netease;
    item.remoteId = objectId(object);
    item.title = object.value(QStringLiteral("name")).toString();
    item.subtitle = firstArray(object, { QStringLiteral("alias"), QStringLiteral("alia") })
                        .toVariantList().value(0).toString();
    item.coverUrl = object.value(QStringLiteral("picUrl")).toString();
    if (item.coverUrl.isEmpty())
        item.coverUrl = object.value(QStringLiteral("img1v1Url")).toString();
    item.sourceRank = rank;
    return item;
}

SearchResultItem albumSearchItem(const QJsonObject &object, int rank)
{
    SearchResultItem item;
    item.type = SearchItemType::Album;
    item.source = SourceId::Netease;
    item.remoteId = objectId(object);
    item.title = object.value(QStringLiteral("name")).toString();
    item.artist = artistNames(object);
    item.subtitle = item.artist;
    item.coverUrl = object.value(QStringLiteral("picUrl")).toString();
    item.sourceRank = rank;
    return item;
}

SearchResultItem playlistSearchItem(const QJsonObject &object, int rank)
{
    SearchResultItem item;
    item.type = SearchItemType::Playlist;
    item.source = SourceId::Netease;
    item.remoteId = objectId(object);
    item.title = object.value(QStringLiteral("name")).toString();
    item.subtitle = object.value(QStringLiteral("creator")).toObject()
                        .value(QStringLiteral("nickname")).toString();
    item.coverUrl = object.value(QStringLiteral("coverImgUrl")).toString();
    item.sourceRank = rank;
    item.popularity = object.value(QStringLiteral("playCount")).toDouble(-1.0);
    return item;
}

} // namespace

NeteaseApiClient::NeteaseApiClient(QObject *parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

void NeteaseApiClient::get(const QString &path, const QUrlQuery &query, OkFn ok, ErrFn err,
                           quint64 searchGeneration)
{
    QUrl url(m_base + path);
    QUrlQuery q = query;
    if (!m_cookie.isEmpty())
        q.addQueryItem(QStringLiteral("cookie"), m_cookie);
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
    req.setTransferTimeout(15000);

    QNetworkReply *reply = m_nam->get(req);
    if (searchGeneration > 0)
        m_searchReplies[searchGeneration].append(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, searchGeneration, ok = std::move(ok), err = std::move(err)]() mutable {
        if (searchGeneration > 0) {
            auto it = m_searchReplies.find(searchGeneration);
            if (it != m_searchReplies.end()) {
                it->removeAll(reply);
                if (it->isEmpty())
                    m_searchReplies.erase(it);
            }
        }
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (err)
                err(reply->errorString());
            return;
        }
        QString error;
        const QJsonObject obj = parseObject(reply, &error);
        if (!error.isEmpty()) {
            if (err)
                err(error);
            return;
        }
        if (ok)
            ok(obj);
    });
}

void NeteaseApiClient::checkReachable(BoolFn done)
{
    QNetworkRequest req(QUrl(m_base + QStringLiteral("/")));
    req.setTransferTimeout(1200);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        if (done)
            done(reply->error() == QNetworkReply::NoError);
    });
}

void NeteaseApiClient::searchSongs(const QString &keywords, int limit, JsonArrayFn ok, ErrFn err)
{
    searchSongsPage(keywords, limit, 0, std::move(ok), std::move(err));
}

void NeteaseApiClient::searchSongsPage(const QString &keywords, int limit, int offset,
                                       JsonArrayFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("keywords"), keywords);
    q.addQueryItem(QStringLiteral("type"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    q.addQueryItem(QStringLiteral("offset"), QString::number(qMax(0, offset)));
    get(QStringLiteral("/search"), q,
        [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("result")).toObject().value(QStringLiteral("songs")).toArray()); },
        err);
}

void NeteaseApiClient::search(const SearchRequest &request, SearchResponseFn ok, ErrFn err)
{
    if (!request.isValid()) {
        if (err)
            err(QStringLiteral("搜索关键词或分页参数无效"));
        return;
    }

    if (request.category == SearchCategory::All) {
        struct AggregateState {
            int pending = 0;
            int succeeded = 0;
            QHash<int, SearchResponse> responses;
            QStringList errors;
        };
        const QList<SearchCategory> categories = {
            SearchCategory::Songs,
            SearchCategory::Artists,
            SearchCategory::Albums,
            SearchCategory::Playlists,
            SearchCategory::Lyrics
        };
        auto state = std::make_shared<AggregateState>();
        state->pending = categories.size();
        const auto finish = [request, state, ok, err] {
            if (--state->pending > 0)
                return;
            if (state->succeeded == 0) {
                if (err)
                    err(state->errors.isEmpty() ? QStringLiteral("网易云综合搜索失败")
                                                : state->errors.join(QStringLiteral("；")));
                return;
            }
            SearchResponse response;
            response.source = SourceId::Netease;
            response.category = SearchCategory::All;
            response.offset = request.offset;
            response.generation = request.generation;
            const QList<SearchCategory> order = {
                SearchCategory::Artists,
                SearchCategory::Songs,
                SearchCategory::Albums,
                SearchCategory::Playlists,
                SearchCategory::Lyrics
            };
            for (SearchCategory category : order) {
                const auto it = state->responses.constFind(int(category));
                if (it == state->responses.constEnd())
                    continue;
                response.items.append(it->items);
                response.hasMore = response.hasMore || it->hasMore;
            }
            if (ok)
                ok(response);
        };
        const int categoryLimit = qMax(3, (request.limit + categories.size() - 1)
                                               / categories.size());
        const int categoryOffset = (request.offset / request.limit) * categoryLimit;
        for (SearchCategory category : categories) {
            SearchRequest child = request;
            child.category = category;
            child.limit = categoryLimit;
            child.offset = categoryOffset;
            search(child, [state, category, finish](const SearchResponse &response) {
                ++state->succeeded;
                state->responses.insert(int(category), response);
                finish();
            }, [state, finish](const QString &message) {
                state->errors.append(message);
                finish();
            });
        }
        return;
    }

    int type = 1;
    switch (request.category) {
    case SearchCategory::All: break;
    case SearchCategory::Songs: type = 1; break;
    case SearchCategory::Artists: type = 100; break;
    case SearchCategory::Albums: type = 10; break;
    case SearchCategory::Playlists: type = 1000; break;
    case SearchCategory::Lyrics: type = 1006; break;
    }

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("keywords"), request.keywords.trimmed());
    query.addQueryItem(QStringLiteral("type"), QString::number(type));
    query.addQueryItem(QStringLiteral("limit"), QString::number(request.limit));
    query.addQueryItem(QStringLiteral("offset"), QString::number(request.offset));
    get(QStringLiteral("/cloudsearch"), query,
        [this, request, ok = std::move(ok)](const QJsonObject &root) mutable {
        const QJsonObject result = root.value(QStringLiteral("result")).toObject();
        SearchResponse response;
        response.source = SourceId::Netease;
        response.category = request.category;
        response.offset = request.offset;
        response.generation = request.generation;
        int rank = request.offset;

        const auto appendSongs = [this, &response, &rank](const QJsonArray &items,
                                                          SearchItemType type) {
            for (const QJsonValue &value : items) {
                SearchResultItem item = songSearchItem(this, value.toObject(), type, rank++);
                if (!item.remoteId.isEmpty())
                    response.items.append(item);
            }
        };
        const auto appendArtists = [&response, &rank](const QJsonArray &items) {
            for (const QJsonValue &value : items) {
                SearchResultItem item = artistSearchItem(value.toObject(), rank++);
                if (!item.remoteId.isEmpty())
                    response.items.append(item);
            }
        };
        const auto appendAlbums = [&response, &rank](const QJsonArray &items) {
            for (const QJsonValue &value : items) {
                SearchResultItem item = albumSearchItem(value.toObject(), rank++);
                if (!item.remoteId.isEmpty())
                    response.items.append(item);
            }
        };
        const auto appendPlaylists = [&response, &rank](const QJsonArray &items) {
            for (const QJsonValue &value : items) {
                SearchResultItem item = playlistSearchItem(value.toObject(), rank++);
                if (!item.remoteId.isEmpty())
                    response.items.append(item);
            }
        };

        qint64 total = 0;
        if (request.category == SearchCategory::All) {
            const QJsonObject songBlock = firstObject(
                result, { QStringLiteral("song"), QStringLiteral("songs") });
            const QJsonObject artistBlock = firstObject(
                result, { QStringLiteral("artist"), QStringLiteral("artists") });
            const QJsonObject albumBlock = firstObject(
                result, { QStringLiteral("album"), QStringLiteral("albums") });
            const QJsonObject playlistBlock = firstObject(
                result, { QStringLiteral("playList"), QStringLiteral("playlist"),
                          QStringLiteral("playlists") });
            appendSongs(firstArray(songBlock, { QStringLiteral("songs"),
                                                QStringLiteral("items") }),
                        SearchItemType::Song);
            appendArtists(firstArray(artistBlock, { QStringLiteral("artists"),
                                                    QStringLiteral("items") }));
            appendAlbums(firstArray(albumBlock, { QStringLiteral("albums"),
                                                  QStringLiteral("items") }));
            appendPlaylists(firstArray(playlistBlock, { QStringLiteral("playLists"),
                                                        QStringLiteral("playlists"),
                                                        QStringLiteral("items") }));
        } else if (request.category == SearchCategory::Songs) {
            const QJsonArray items = firstArray(result, { QStringLiteral("songs") });
            appendSongs(items, SearchItemType::Song);
            total = result.value(QStringLiteral("songCount")).toVariant().toLongLong();
        } else if (request.category == SearchCategory::Artists) {
            const QJsonArray items = firstArray(result, { QStringLiteral("artists") });
            appendArtists(items);
            total = result.value(QStringLiteral("artistCount")).toVariant().toLongLong();
        } else if (request.category == SearchCategory::Albums) {
            const QJsonArray items = firstArray(result, { QStringLiteral("albums") });
            appendAlbums(items);
            total = result.value(QStringLiteral("albumCount")).toVariant().toLongLong();
        } else if (request.category == SearchCategory::Playlists) {
            const QJsonArray items = firstArray(
                result, { QStringLiteral("playlists"), QStringLiteral("playLists") });
            appendPlaylists(items);
            total = result.value(QStringLiteral("playlistCount")).toVariant().toLongLong();
        } else {
            const QJsonArray items = firstArray(result, { QStringLiteral("songs") });
            appendSongs(items, SearchItemType::Lyric);
            total = result.value(QStringLiteral("songCount")).toVariant().toLongLong();
        }
        response.hasMore = total > request.offset + response.items.size()
            || (total <= 0 && response.items.size() >= request.limit);
        if (ok)
            ok(response);
    }, std::move(err), request.generation);
}

void NeteaseApiClient::searchSuggestions(const QString &keywords, int limit,
                                         SearchSuggestionsFn ok, ErrFn err)
{
    const QString text = keywords.trimmed();
    if (text.isEmpty() || limit <= 0) {
        if (ok)
            ok({});
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("keywords"), text);
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("mobile"));
    get(QStringLiteral("/search/suggest"), query,
        [limit, ok = std::move(ok)](const QJsonObject &root) mutable {
        QList<SearchSuggestion> suggestions;
        const QJsonArray matches = root.value(QStringLiteral("result")).toObject()
                                       .value(QStringLiteral("allMatch")).toArray();
        for (const QJsonValue &value : matches) {
            const QJsonObject object = value.toObject();
            const QString keyword = object.value(QStringLiteral("keyword")).toString().trimmed();
            if (keyword.isEmpty())
                continue;
            SearchSuggestion suggestion;
            suggestion.source = SourceId::Netease;
            suggestion.text = keyword;
            const int type = object.value(QStringLiteral("type")).toInt(1);
            suggestion.type = type == 100 ? SearchItemType::Artist
                : type == 10 ? SearchItemType::Album
                : type == 1000 ? SearchItemType::Playlist : SearchItemType::Song;
            suggestions.append(suggestion);
            if (suggestions.size() >= limit)
                break;
        }
        if (ok)
            ok(suggestions);
    }, std::move(err));
}

void NeteaseApiClient::hotSearch(int limit, HotSearchFn ok, ErrFn err)
{
    if (limit <= 0) {
        if (ok)
            ok({});
        return;
    }
    get(QStringLiteral("/search/hot/detail"), QUrlQuery(),
        [limit, ok = std::move(ok)](const QJsonObject &root) mutable {
        QList<HotSearchTerm> terms;
        const QJsonArray data = root.value(QStringLiteral("data")).toArray();
        for (int i = 0; i < data.size() && terms.size() < limit; ++i) {
            const QJsonObject object = data.at(i).toObject();
            const QString text = object.value(QStringLiteral("searchWord")).toString().trimmed();
            if (text.isEmpty())
                continue;
            HotSearchTerm term;
            term.source = SourceId::Netease;
            term.text = text;
            term.description = object.value(QStringLiteral("content")).toString();
            term.score = object.value(QStringLiteral("score")).toDouble(-1.0);
            term.rank = i;
            terms.append(term);
        }
        if (ok)
            ok(terms);
    }, std::move(err));
}

void NeteaseApiClient::defaultSearchText(StringFn ok, ErrFn err)
{
    get(QStringLiteral("/search/default"), QUrlQuery(),
        [ok = std::move(ok)](const QJsonObject &root) mutable {
        const QJsonObject data = root.value(QStringLiteral("data")).toObject();
        QString text = data.value(QStringLiteral("showKeyword")).toString().trimmed();
        if (text.isEmpty())
            text = data.value(QStringLiteral("realkeyword")).toString().trimmed();
        if (ok)
            ok(text);
    }, std::move(err));
}

void NeteaseApiClient::cancelSearch(quint64 generation)
{
    const QList<QPointer<QNetworkReply>> replies = m_searchReplies.take(generation);
    for (const QPointer<QNetworkReply> &reply : replies) {
        if (reply)
            reply->abort();
    }
}

void NeteaseApiClient::matchSong(const QString &title, const QString &artist, const QString &album,
                                 qint64 durationMs, const QString &md5, JsonArrayFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("title"), title);
    q.addQueryItem(QStringLiteral("artist"), artist);
    q.addQueryItem(QStringLiteral("album"), album);
    q.addQueryItem(QStringLiteral("duration"), QString::number(double(durationMs) / 1000.0, 'f', 3));
    q.addQueryItem(QStringLiteral("md5"), md5);
    get(QStringLiteral("/search/match"), q,
        [ok](const QJsonObject &obj) {
            ok(obj.value(QStringLiteral("result")).toObject().value(QStringLiteral("songs")).toArray());
        },
        err);
}

void NeteaseApiClient::songUrls(const QStringList &ids, JsonArrayFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), ids.join(QLatin1Char(',')));
    q.addQueryItem(QStringLiteral("level"), QStringLiteral("standard"));
    get(QStringLiteral("/song/url/v1"), q, [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("data")).toArray()); }, err);
}

void NeteaseApiClient::songDetails(const QList<qint64> &ids, JsonArrayFn ok, ErrFn err)
{
    QStringList values;
    for (qint64 id : ids) {
        if (id > 0)
            values << QString::number(id);
    }
    if (values.isEmpty()) {
        if (ok)
            ok({});
        return;
    }
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("ids"), values.join(QLatin1Char(',')));
    get(QStringLiteral("/song/detail"), q,
        [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("songs")).toArray()); }, err);
}

void NeteaseApiClient::lyric(const QString &id, String3Fn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), id);
    get(QStringLiteral("/lyric"), q, [ok](const QJsonObject &obj) {
        const QString lrc = obj.value(QStringLiteral("lrc")).toObject().value(QStringLiteral("lyric")).toString();
        const QString tlyrc = obj.value(QStringLiteral("tlyric")).toObject().value(QStringLiteral("lyric")).toString();
        const QString romalrc = obj.value(QStringLiteral("romalrc")).toObject().value(QStringLiteral("lyric")).toString();
        ok(lrc, tlyrc, romalrc);
    }, err);
}

void NeteaseApiClient::songDetail(const QString &id, OkFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("ids"), id);
    get(QStringLiteral("/song/detail"), q, ok, err);
}

void NeteaseApiClient::albumDetail(const QString &id, OkFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), id);
    get(QStringLiteral("/album"), q, ok, err);
}

void NeteaseApiClient::artistDetail(const QString &id, OkFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), id);
    get(QStringLiteral("/artist/detail"), q, ok, err);
}

void NeteaseApiClient::artistSongs(const QString &id, JsonArrayFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), id);
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("50"));
    get(QStringLiteral("/artist/songs"), q, [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("songs")).toArray()); }, err);
}

void NeteaseApiClient::playlistDetail(const QString &id, OkFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), id);
    get(QStringLiteral("/playlist/detail"), q,
        [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("playlist")).toObject()); }, err);
}

void NeteaseApiClient::playlistTracks(const QString &id, JsonArrayFn ok, ErrFn err)
{
    QUrlQuery detailQuery;
    detailQuery.addQueryItem(QStringLiteral("id"), id);
    get(QStringLiteral("/playlist/detail"), detailQuery,
        [this, id, ok, err](const QJsonObject &obj) {
            const QJsonObject playlist = obj.value(QStringLiteral("playlist")).toObject();
            const QJsonArray trackIds = playlist.value(QStringLiteral("trackIds")).toArray();
            if (trackIds.isEmpty()) {
                QUrlQuery q;
                q.addQueryItem(QStringLiteral("id"), id);
                q.addQueryItem(QStringLiteral("limit"), QStringLiteral("1000"));
                q.addQueryItem(QStringLiteral("offset"), QStringLiteral("0"));
                get(QStringLiteral("/playlist/track/all"), q,
                    [ok](const QJsonObject &fallback) {
                        ok(fallback.value(QStringLiteral("songs")).toArray());
                    }, err);
                return;
            }

            QList<qint64> ids;
            for (const QJsonValue &value : trackIds)
                ids.append(value.toObject().value(QStringLiteral("id")).toVariant().toLongLong());

            const int batchSize = 300;
            auto songs = std::make_shared<QHash<qint64, QJsonObject>>();
            auto fetch = std::make_shared<std::function<void(int)>>();
            *fetch = [this, ids, batchSize, songs, fetch, ok, err](int offset) {
                if (offset >= ids.size()) {
                    QJsonArray ordered;
                    for (qint64 songId : ids) {
                        const auto it = songs->constFind(songId);
                        if (it != songs->constEnd())
                            ordered.append(it.value());
                    }
                    ok(ordered);
                    return;
                }
                const int end = qMin(offset + batchSize, ids.size());
                QList<qint64> batch;
                for (int i = offset; i < end; ++i)
                    batch.append(ids[i]);
                songDetails(batch,
                            [songs, fetch, offset, end](const QJsonArray &arr) {
                                for (const QJsonValue &value : arr) {
                                    const QJsonObject song = value.toObject();
                                    songs->insert(song.value(QStringLiteral("id")).toVariant().toLongLong(), song);
                                }
                                (*fetch)(end);
                            },
                            [err](const QString &message) {
                                if (err)
                                    err(message);
                            });
            };
            (*fetch)(0);
        }, err);
}

void NeteaseApiClient::topPlaylists(const QString &cat, int offset, JsonArrayFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("cat"), cat);
    q.addQueryItem(QStringLiteral("order"), QStringLiteral("hot"));
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("30"));
    q.addQueryItem(QStringLiteral("offset"), QString::number(offset));
    get(QStringLiteral("/top/playlist"), q, [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("playlists")).toArray()); }, err);
}

void NeteaseApiClient::recommendSongs(JsonArrayFn ok, ErrFn err)
{
    get(QStringLiteral("/recommend/songs"), QUrlQuery(),
        [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("dailySongs")).toArray()); }, err);
}

void NeteaseApiClient::qrKey(StringFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("timestamp"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    get(QStringLiteral("/login/qr/key"), q,
        [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("unikey")).toString()); }, err);
}

void NeteaseApiClient::qrCreate(const QString &key, StringFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("key"), key);
    q.addQueryItem(QStringLiteral("qrimg"), QStringLiteral("true"));
    q.addQueryItem(QStringLiteral("timestamp"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    get(QStringLiteral("/login/qr/create"), q,
        [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("qrimg")).toString()); }, err);
}

void NeteaseApiClient::qrCheck(const QString &key, OkFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("key"), key);
    q.addQueryItem(QStringLiteral("timestamp"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    get(QStringLiteral("/login/qr/check"), q, ok, err);
}

void NeteaseApiClient::loginStatus(OkFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("timestamp"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    get(QStringLiteral("/login/status"), q, ok, err);
}

void NeteaseApiClient::vipStatus(qint64 uid, OkFn ok, ErrFn err)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("uid"), QString::number(uid));
    query.addQueryItem(QStringLiteral("timestamp"),
                       QString::number(QDateTime::currentMSecsSinceEpoch()));
    get(QStringLiteral("/vip/info"), query, std::move(ok), std::move(err));
}

void NeteaseApiClient::logout(OkFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("timestamp"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    get(QStringLiteral("/logout"), q, ok, err);
}

void NeteaseApiClient::userPlaylists(const QString &uid, JsonArrayFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("uid"), uid);
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("50"));
    get(QStringLiteral("/user/playlist"), q, [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("playlist")).toArray()); }, err);
}

void NeteaseApiClient::downloadToFile(const QUrl &url, const QString &filePath, BoolFn done)
{
    downloadToFileWithProgress(url, filePath, {}, [done](const DownloadResult &result) {
        if (done)
            done(result.ok);
    });
}

MusicSource::DownloadId NeteaseApiClient::downloadToFileWithProgress(const QUrl &url,
                                                                      const QString &filePath,
                                                                      DownloadProgressFn progress,
                                                                      DownloadDoneFn done)
{
    if (!url.isValid() || url.isEmpty() || filePath.isEmpty()) {
        QTimer::singleShot(0, this, [done] {
            if (done)
                done({ false, QStringLiteral("下载地址或目标路径无效"), 0 });
        });
        return 0;
    }

    const DownloadId id = m_nextDownloadId++;
    auto file = std::make_shared<QSaveFile>(filePath);
    if (!file->open(QIODevice::WriteOnly)) {
        const QString error = file->errorString();
        QTimer::singleShot(0, this, [done, error] {
            if (done)
                done({ false, QStringLiteral("无法创建下载文件：%1").arg(error), 0 });
        });
        return 0;
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
    req.setTransferTimeout(60000);
    QNetworkReply *reply = m_nam->get(req);
    m_downloads.insert(id, reply);
    auto writeFailed = std::make_shared<bool>(false);

    connect(reply, &QNetworkReply::readyRead, this, [reply, file, writeFailed] {
        const QByteArray data = reply->readAll();
        if (!data.isEmpty() && file->write(data) != data.size())
            *writeFailed = true;
        if (*writeFailed)
            reply->abort();
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [progress](qint64 received, qint64 total) {
        if (progress)
            progress(received, total);
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, id, reply, file, writeFailed, done] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString networkError = reply->errorString();
        const QByteArray tail = reply->readAll();
        if (!tail.isEmpty() && file->write(tail) != tail.size())
            *writeFailed = true;

        DownloadResult result;
        if (*writeFailed) {
            result.error = QStringLiteral("写入下载文件失败");
        } else if (status >= 400) {
            result.error = QStringLiteral("服务器返回 HTTP %1").arg(status);
        } else if (reply->error() != QNetworkReply::NoError) {
            result.error = networkError.isEmpty() ? QStringLiteral("网络连接失败") : networkError;
        } else if (!file->commit()) {
            result.error = QStringLiteral("保存下载文件失败：%1").arg(file->errorString());
        } else {
            result.ok = QFileInfo(file->fileName()).size() > 0;
            result.sizeBytes = QFileInfo(file->fileName()).size();
            if (!result.ok)
                result.error = QStringLiteral("下载内容为空");
        }
        m_downloads.remove(id);
        reply->deleteLater();
        if (done)
            done(result);
    });
    return id;
}

void NeteaseApiClient::cancelDownload(DownloadId id)
{
    if (id == 0)
        return;
    const QPointer<QNetworkReply> reply = m_downloads.value(id);
    if (reply)
        reply->abort();
}

Song NeteaseApiClient::songFromJson(const QJsonObject &obj) const
{
    QStringList artists;
    QString artistRemoteId;
    QJsonArray ar = obj.value(QStringLiteral("ar")).toArray();
    if (ar.isEmpty())
        ar = obj.value(QStringLiteral("artists")).toArray();
    for (const QJsonValue &v : ar) {
        artists << v.toObject().value(QStringLiteral("name")).toString();
        if (artistRemoteId.isEmpty())
            artistRemoteId = v.toObject().value(QStringLiteral("id")).toVariant().toString();
    }
    QJsonObject al = obj.value(QStringLiteral("al")).toObject();
    if (al.isEmpty())
        al = obj.value(QStringLiteral("album")).toObject();
    qint64 durationMs = obj.value(QStringLiteral("dt")).toVariant().toLongLong();
    if (durationMs <= 0)
        durationMs = obj.value(QStringLiteral("duration")).toVariant().toLongLong();
    QString coverUrl = al.value(QStringLiteral("picUrl")).toString();
    if (coverUrl.isEmpty())
        coverUrl = obj.value(QStringLiteral("picUrl")).toString();
    if (coverUrl.isEmpty())
        coverUrl = obj.value(QStringLiteral("albumPicUrl")).toString();
    Song song = MusicSource::makeOnlineSong(
        sourceId(), sourceScheme(),
        obj.value(QStringLiteral("id")).toVariant().toString(),
        obj.value(QStringLiteral("name")).toString(),
        artists.join(QLatin1Char('/')),
        al.value(QStringLiteral("name")).toString(),
        durationMs,
        coverUrl,
        al.value(QStringLiteral("id")).toVariant().toString(),
        artistRemoteId);
    const QJsonObject privilege = obj.value(QStringLiteral("privilege")).toObject();
    const bool noCopyright = privilege.value(QStringLiteral("st")).toInt(0) < 0
        || obj.value(QStringLiteral("noCopyrightRcmd")).isObject()
        || obj.value(QStringLiteral("noCopyrightRcmd")).isString();
    const int fee = obj.value(QStringLiteral("fee")).toInt(0);
    if (noCopyright)
        song.accessRequirement = AccessRequirement::Unavailable;
    else if (fee == 1)
        song.accessRequirement = AccessRequirement::Vip;
    else if (fee == 4)
        song.accessRequirement = AccessRequirement::Purchase;
    else
        song.accessRequirement = AccessRequirement::Free;
    return song;
}

} // namespace core
