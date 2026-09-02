#include "QqMusicSource.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <utility>

namespace core {

namespace {

QString searchCategoryName(SearchCategory category)
{
    switch (category) {
    case SearchCategory::All: return QStringLiteral("all");
    case SearchCategory::Songs: return QStringLiteral("songs");
    case SearchCategory::Artists: return QStringLiteral("artists");
    case SearchCategory::Albums: return QStringLiteral("albums");
    case SearchCategory::Playlists: return QStringLiteral("playlists");
    case SearchCategory::Lyrics: return QStringLiteral("lyrics");
    }
    return QStringLiteral("songs");
}

SearchItemType searchItemType(const QString &type)
{
    if (type == QStringLiteral("artist"))
        return SearchItemType::Artist;
    if (type == QStringLiteral("album"))
        return SearchItemType::Album;
    if (type == QStringLiteral("playlist"))
        return SearchItemType::Playlist;
    if (type == QStringLiteral("lyric"))
        return SearchItemType::Lyric;
    return SearchItemType::Song;
}

} // namespace

QqMusicSource::QqMusicSource(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void QqMusicSource::post(const QString &path, const QJsonObject &payload, OkFn ok, ErrFn err,
                         quint64 searchGeneration)
{
    QNetworkRequest request(QUrl(m_base + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("WyCloudForge/1.0"));
    request.setTransferTimeout(path == QStringLiteral("/auth/qr/status") ? 45000 : 15000);
    QNetworkReply *reply = m_nam->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
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
        const QByteArray bytes = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString networkError = reply->errorString();
        reply->deleteLater();
        QJsonParseError parseError;
        const QJsonObject root = QJsonDocument::fromJson(bytes, &parseError).object();
        if (parseError.error != QJsonParseError::NoError || root.isEmpty()) {
            if (err)
                err(networkError.isEmpty() ? QStringLiteral("QQ 服务响应解析失败") : networkError);
            return;
        }
        if (status >= 400 || !root.value(QStringLiteral("ok")).toBool()) {
            QString message = root.value(QStringLiteral("error")).toObject()
                                  .value(QStringLiteral("message")).toString();
            if (message.isEmpty())
                message = networkError.isEmpty() ? QStringLiteral("QQ 服务请求失败") : networkError;
            if (err)
                err(message);
            return;
        }
        if (ok)
            ok(root.value(QStringLiteral("data")).toObject());
    });
}

void QqMusicSource::startQrLogin(const QString &method, OkFn ok, ErrFn err)
{
    post(QStringLiteral("/auth/qr/start"), { { QStringLiteral("method"), method } },
         [this, ok = std::move(ok)](const QJsonObject &data) {
        const QString attemptId = data.value(QStringLiteral("loginAttemptId")).toString();
        const QString image = data.value(QStringLiteral("qrImage")).toString();
        if (!attemptId.isEmpty() && !image.isEmpty())
            m_qrImages.insert(attemptId, image);
        if (ok)
            ok(data);
    }, std::move(err));
}

void QqMusicSource::pollQrLogin(const QString &attemptId, OkFn ok, ErrFn err)
{
    post(QStringLiteral("/auth/qr/status"),
         { { QStringLiteral("loginAttemptId"), attemptId } }, std::move(ok), std::move(err));
}

void QqMusicSource::cancelQrLogin(const QString &attemptId, OkFn ok, ErrFn err)
{
    m_qrImages.remove(attemptId);
    post(QStringLiteral("/auth/qr/cancel"),
         { { QStringLiteral("loginAttemptId"), attemptId } }, std::move(ok), std::move(err));
}

void QqMusicSource::validateCredential(const QString &credential, const QString &loginMethod,
                                       OkFn ok, ErrFn err)
{
    post(QStringLiteral("/auth/validate"), {
        { QStringLiteral("credential"), credential },
        { QStringLiteral("loginMethod"), loginMethod }
    }, std::move(ok), std::move(err));
}

void QqMusicSource::vipStatus(OkFn ok, ErrFn err)
{
    post(QStringLiteral("/v1/account/vip"), {
        { QStringLiteral("credential"), m_cookie }
    }, std::move(ok), std::move(err));
}

void QqMusicSource::searchSongs(const QString &keywords, int limit, JsonArrayFn ok, ErrFn err)
{
    searchSongsPage(keywords, limit, 0, std::move(ok), std::move(err));
}

void QqMusicSource::searchSongsPage(const QString &keywords, int limit, int offset,
                                    JsonArrayFn ok, ErrFn err)
{
    post(QStringLiteral("/v1/search"), {
        { QStringLiteral("keywords"), keywords },
        { QStringLiteral("limit"), limit },
        { QStringLiteral("offset"), qMax(0, offset) }
    }, [ok = std::move(ok)](const QJsonObject &data) {
        if (ok)
            ok(data.value(QStringLiteral("songs")).toArray());
    }, std::move(err));
}

void QqMusicSource::search(const SearchRequest &request, SearchResponseFn ok, ErrFn err)
{
    if (!request.isValid()) {
        if (err)
            err(QStringLiteral("搜索关键词或分页参数无效"));
        return;
    }
    post(QStringLiteral("/v1/search"), {
        { QStringLiteral("keywords"), request.keywords.trimmed() },
        { QStringLiteral("category"), searchCategoryName(request.category) },
        { QStringLiteral("limit"), request.limit },
        { QStringLiteral("offset"), request.offset }
    }, [this, request, ok = std::move(ok)](const QJsonObject &data) mutable {
        SearchResponse response;
        response.source = SourceId::QqMusic;
        response.category = request.category;
        response.offset = request.offset;
        response.generation = request.generation;
        response.hasMore = data.value(QStringLiteral("hasMore")).toBool();
        const QJsonArray items = data.value(QStringLiteral("items")).toArray();
        response.items.reserve(items.size());
        int rank = request.offset;
        for (const QJsonValue &value : items) {
            const QJsonObject object = value.toObject();
            SearchResultItem item;
            item.source = SourceId::QqMusic;
            item.type = searchItemType(object.value(QStringLiteral("type")).toString());
            item.remoteId = object.value(QStringLiteral("remoteId")).toString().trimmed();
            item.title = object.value(QStringLiteral("title")).toString();
            item.subtitle = object.value(QStringLiteral("subtitle")).toString();
            item.artist = object.value(QStringLiteral("artist")).toString();
            item.album = object.value(QStringLiteral("album")).toString();
            item.coverUrl = object.value(QStringLiteral("coverUrl")).toString();
            item.durationMs = object.value(QStringLiteral("durationMs")).toVariant().toLongLong();
            item.popularity = object.value(QStringLiteral("popularity")).toDouble(-1.0);
            item.sourceRank = rank++;
            if (item.type == SearchItemType::Song || item.type == SearchItemType::Lyric) {
                item.song = songFromJson(object);
                if (item.remoteId.isEmpty())
                    item.remoteId = item.song.effectiveRemoteId();
                if (item.title.isEmpty())
                    item.title = item.song.title;
                if (item.artist.isEmpty())
                    item.artist = item.song.artist;
                if (item.album.isEmpty())
                    item.album = item.song.album;
                if (item.subtitle.isEmpty())
                    item.subtitle = item.artist;
            }
            if (!item.remoteId.isEmpty())
                response.items.append(item);
        }
        if (ok)
            ok(response);
    }, std::move(err), request.generation);
}

void QqMusicSource::searchSuggestions(const QString &keywords, int limit,
                                      SearchSuggestionsFn ok, ErrFn err)
{
    const QString text = keywords.trimmed();
    if (text.isEmpty() || limit <= 0) {
        if (ok)
            ok({});
        return;
    }
    post(QStringLiteral("/v1/search/suggest"), {
        { QStringLiteral("keywords"), text },
        { QStringLiteral("limit"), limit }
    }, [ok = std::move(ok)](const QJsonObject &data) mutable {
        QList<SearchSuggestion> suggestions;
        const QJsonArray items = data.value(QStringLiteral("suggestions")).toArray();
        for (const QJsonValue &value : items) {
            const QJsonObject object = value.toObject();
            const QString text = object.value(QStringLiteral("text")).toString().trimmed();
            if (text.isEmpty())
                continue;
            SearchSuggestion suggestion;
            suggestion.source = SourceId::QqMusic;
            suggestion.type = searchItemType(object.value(QStringLiteral("type")).toString());
            suggestion.text = text;
            suggestion.subtitle = object.value(QStringLiteral("subtitle")).toString();
            suggestion.remoteId = object.value(QStringLiteral("remoteId")).toString();
            suggestions.append(suggestion);
        }
        if (ok)
            ok(suggestions);
    }, std::move(err));
}

void QqMusicSource::hotSearch(int limit, HotSearchFn ok, ErrFn err)
{
    if (limit <= 0) {
        if (ok)
            ok({});
        return;
    }
    post(QStringLiteral("/v1/search/hot"), {
        { QStringLiteral("limit"), limit }
    }, [ok = std::move(ok)](const QJsonObject &data) mutable {
        QList<HotSearchTerm> terms;
        const QJsonArray items = data.value(QStringLiteral("terms")).toArray();
        for (int i = 0; i < items.size(); ++i) {
            const QJsonObject object = items.at(i).toObject();
            const QString text = object.value(QStringLiteral("text")).toString().trimmed();
            if (text.isEmpty())
                continue;
            HotSearchTerm term;
            term.source = SourceId::QqMusic;
            term.text = text;
            term.description = object.value(QStringLiteral("description")).toString();
            term.score = object.value(QStringLiteral("score")).toDouble(-1.0);
            term.rank = i;
            terms.append(term);
        }
        if (ok)
            ok(terms);
    }, std::move(err));
}

void QqMusicSource::cancelSearch(quint64 generation)
{
    const QList<QPointer<QNetworkReply>> replies = m_searchReplies.take(generation);
    for (const QPointer<QNetworkReply> &reply : replies) {
        if (reply)
            reply->abort();
    }
}

void QqMusicSource::songUrls(const QStringList &ids, JsonArrayFn ok, ErrFn err)
{
    QJsonArray jsonIds;
    for (const QString &id : ids)
        jsonIds.append(id);
    post(QStringLiteral("/v1/media"), {
        { QStringLiteral("ids"), jsonIds },
        { QStringLiteral("credential"), m_cookie }
    }, [ok = std::move(ok)](const QJsonObject &data) {
        if (ok)
            ok(data.value(QStringLiteral("addresses")).toArray());
    }, std::move(err));
}

void QqMusicSource::songUrls(const QList<Song> &songs, JsonArrayFn ok, ErrFn err)
{
    // file.media_mid is metadata rather than the mediaId contract expected by
    // the current QQ SDK. Resolve with the stable songmid list, matching the
    // playback path that is known to work for authenticated VIP accounts.
    MusicSource::songUrls(songs, std::move(ok), std::move(err));
}

void QqMusicSource::lyric(const QString &id, String3Fn ok, ErrFn err)
{
    post(QStringLiteral("/v1/lyrics"), {
        { QStringLiteral("remoteId"), id },
        { QStringLiteral("credential"), m_cookie }
    }, [ok = std::move(ok)](const QJsonObject &data) {
        if (ok)
            ok(data.value(QStringLiteral("original")).toString(),
               data.value(QStringLiteral("translated")).toString(),
               data.value(QStringLiteral("romanized")).toString());
    }, std::move(err));
}

void QqMusicSource::unsupported(const QString &feature, ErrFn err)
{
    if (err)
        err(QStringLiteral("QQ 音乐暂不支持%1").arg(feature));
}

void QqMusicSource::songDetail(const QString &, OkFn, ErrFn err) { unsupported(QStringLiteral("歌曲详情"), std::move(err)); }
void QqMusicSource::albumDetail(const QString &id, OkFn ok, ErrFn err)
{
    post(QStringLiteral("/v1/album/detail"), { { QStringLiteral("remoteId"), id } },
         std::move(ok), std::move(err));
}

void QqMusicSource::artistDetail(const QString &id, OkFn ok, ErrFn err)
{
    post(QStringLiteral("/v1/artist/detail"), { { QStringLiteral("remoteId"), id } },
         std::move(ok), std::move(err));
}

void QqMusicSource::artistSongs(const QString &id, JsonArrayFn ok, ErrFn err)
{
    post(QStringLiteral("/v1/artist/songs"), { { QStringLiteral("remoteId"), id } },
         [ok = std::move(ok)](const QJsonObject &data) {
        if (ok)
            ok(data.value(QStringLiteral("songs")).toArray());
    }, std::move(err));
}

void QqMusicSource::playlistDetail(const QString &id, OkFn ok, ErrFn err)
{
    post(QStringLiteral("/v1/playlist/detail"), { { QStringLiteral("remoteId"), id } },
         std::move(ok), std::move(err));
}

void QqMusicSource::playlistTracks(const QString &id, JsonArrayFn ok, ErrFn err)
{
    playlistDetail(id, [ok = std::move(ok)](const QJsonObject &data) {
        if (ok)
            ok(data.value(QStringLiteral("songs")).toArray());
    }, std::move(err));
}

void QqMusicSource::topPlaylists(const QString &, int offset, JsonArrayFn ok, ErrFn err)
{
    post(QStringLiteral("/v1/playlists"), {
        { QStringLiteral("offset"), qMax(0, offset) },
        { QStringLiteral("limit"), 20 }
    }, [ok = std::move(ok)](const QJsonObject &data) {
        if (ok)
            ok(data.value(QStringLiteral("playlists")).toArray());
    }, std::move(err));
}

void QqMusicSource::recommendSongs(JsonArrayFn ok, ErrFn err)
{
    post(QStringLiteral("/v1/recommend"), { { QStringLiteral("credential"), m_cookie } },
         [ok = std::move(ok)](const QJsonObject &data) {
        if (ok)
            ok(data.value(QStringLiteral("songs")).toArray());
    }, std::move(err));
}

void QqMusicSource::qrKey(StringFn ok, ErrFn err)
{
    startQrLogin(QStringLiteral("qq"), [ok = std::move(ok)](const QJsonObject &data) {
        if (ok)
            ok(data.value(QStringLiteral("loginAttemptId")).toString());
    }, std::move(err));
}

void QqMusicSource::qrCreate(const QString &key, StringFn ok, ErrFn err)
{
    const QString image = m_qrImages.value(key);
    if (image.isEmpty()) {
        if (err)
            err(QStringLiteral("QQ 二维码任务不存在"));
        return;
    }
    if (ok)
        ok(image);
}

void QqMusicSource::qrCheck(const QString &key, OkFn ok, ErrFn err)
{
    pollQrLogin(key, [ok = std::move(ok)](const QJsonObject &data) {
        QJsonObject compatibility = data;
        const QString state = data.value(QStringLiteral("state")).toString();
        compatibility.insert(QStringLiteral("code"),
                             state == QStringLiteral("AUTHORIZED") ? 803
                             : state == QStringLiteral("WAITING_CONFIRM") ? 802
                             : state == QStringLiteral("WAITING_SCAN") ? 801 : 800);
        compatibility.insert(QStringLiteral("cookie"), data.value(QStringLiteral("credential")));
        if (ok)
            ok(compatibility);
    }, std::move(err));
}

void QqMusicSource::loginStatus(OkFn ok, ErrFn err)
{
    if (m_cookie.isEmpty()) {
        if (err)
            err(QStringLiteral("QQ 音乐尚未登录"));
        return;
    }
    validateCredential(m_cookie, QStringLiteral("saved"), [ok = std::move(ok)](const QJsonObject &profile) {
        if (ok)
            ok({ { QStringLiteral("profile"), profile } });
    }, std::move(err));
}

void QqMusicSource::logout(OkFn ok, ErrFn err)
{
    post(QStringLiteral("/auth/logout"), {}, std::move(ok), std::move(err));
}

void QqMusicSource::userPlaylists(const QString &uid, JsonArrayFn ok, ErrFn err)
{
    post(QStringLiteral("/v1/account/playlists"), {
        { QStringLiteral("userId"), uid },
        { QStringLiteral("credential"), m_cookie },
        { QStringLiteral("limit"), 50 },
        { QStringLiteral("offset"), 0 }
    }, [ok = std::move(ok)](const QJsonObject &data) {
        if (ok)
            ok(data.value(QStringLiteral("playlists")).toArray());
    }, std::move(err));
}

void QqMusicSource::downloadToFile(const QUrl &url, const QString &filePath, BoolFn done)
{
    downloadToFileWithProgress(url, filePath, {}, [done = std::move(done)](const DownloadResult &result) {
        if (done)
            done(result.ok);
    });
}

MusicSource::DownloadId QqMusicSource::downloadToFileWithProgress(const QUrl &url,
                                                                  const QString &filePath,
                                                                  DownloadProgressFn progress,
                                                                  DownloadDoneFn done)
{
    if (!url.isValid() || url.isEmpty() || filePath.isEmpty()) {
        QTimer::singleShot(0, this, [done = std::move(done)] {
            if (done)
                done({ false, QStringLiteral("下载地址或目标路径无效"), 0 });
        });
        return 0;
    }
    const DownloadId id = m_nextDownloadId++;
    auto file = std::make_shared<QSaveFile>(filePath);
    if (!file->open(QIODevice::WriteOnly)) {
        const QString error = file->errorString();
        QTimer::singleShot(0, this, [done = std::move(done), error] {
            if (done)
                done({ false, QStringLiteral("无法创建下载文件：%1").arg(error), 0 });
        });
        return 0;
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64)"));
    request.setTransferTimeout(60000);
    QNetworkReply *reply = m_nam->get(request);
    m_downloads.insert(id, reply);
    auto writeFailed = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::readyRead, this, [reply, file, writeFailed] {
        const QByteArray data = reply->readAll();
        if (!data.isEmpty() && file->write(data) != data.size())
            *writeFailed = true;
        if (*writeFailed)
            reply->abort();
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [progress](qint64 received, qint64 total) {
        if (progress)
            progress(received, total);
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, id, reply, file, writeFailed, done = std::move(done)] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString networkError = reply->errorString();
        const QByteArray tail = reply->readAll();
        if (!tail.isEmpty() && file->write(tail) != tail.size())
            *writeFailed = true;
        DownloadResult result;
        if (*writeFailed)
            result.error = QStringLiteral("写入下载文件失败");
        else if (status >= 400)
            result.error = QStringLiteral("服务器返回 HTTP %1").arg(status);
        else if (reply->error() != QNetworkReply::NoError)
            result.error = networkError.isEmpty() ? QStringLiteral("网络连接失败") : networkError;
        else if (!file->commit())
            result.error = QStringLiteral("保存下载文件失败：%1").arg(file->errorString());
        else {
            result.sizeBytes = QFileInfo(file->fileName()).size();
            result.ok = result.sizeBytes > 0;
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

void QqMusicSource::cancelDownload(DownloadId id)
{
    const QPointer<QNetworkReply> reply = m_downloads.value(id);
    if (reply)
        reply->abort();
}

Song QqMusicSource::songFromJson(const QJsonObject &obj) const
{
    Song song = MusicSource::makeOnlineSong(
        SourceId::QqMusic, sourceScheme(), obj.value(QStringLiteral("remoteId")).toString(),
        obj.value(QStringLiteral("title")).toString(), obj.value(QStringLiteral("artist")).toString(),
        obj.value(QStringLiteral("album")).toString(), obj.value(QStringLiteral("durationMs")).toVariant().toLongLong(),
        obj.value(QStringLiteral("coverUrl")).toString(), obj.value(QStringLiteral("albumRemoteId")).toString(),
        obj.value(QStringLiteral("artistRemoteId")).toString());
    song.mediaRemoteId = obj.value(QStringLiteral("mediaRemoteId")).toString().trimmed();
    return song;
}

} // namespace core
