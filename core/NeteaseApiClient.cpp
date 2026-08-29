#include "NeteaseApiClient.h"

#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>
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

} // namespace

NeteaseApiClient::NeteaseApiClient(QObject *parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

void NeteaseApiClient::get(const QString &path, const QUrlQuery &query, OkFn ok, ErrFn err)
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
    connect(reply, &QNetworkReply::finished, this, [reply, ok = std::move(ok), err = std::move(err)]() mutable {
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

void NeteaseApiClient::topLists(JsonArrayFn ok, ErrFn err)
{
    get(QStringLiteral("/toplist"), QUrlQuery(),
        [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("list")).toArray()); }, err);
}

void NeteaseApiClient::recommendSongs(JsonArrayFn ok, ErrFn err)
{
    get(QStringLiteral("/recommend/songs"), QUrlQuery(),
        [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("dailySongs")).toArray()); }, err);
}

void NeteaseApiClient::personalFm(JsonArrayFn ok, ErrFn err)
{
    get(QStringLiteral("/personal_fm"), QUrlQuery(),
        [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("data")).toArray()); }, err);
}

void NeteaseApiClient::comments(const QString &id, int offset, int limit, OkFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), id);
    q.addQueryItem(QStringLiteral("offset"), QString::number(offset));
    q.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    get(QStringLiteral("/comment/music"), q, ok, err);
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

void NeteaseApiClient::like(const QString &id, bool like, OkFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), id);
    q.addQueryItem(QStringLiteral("like"), like ? QStringLiteral("true") : QStringLiteral("false"));
    get(QStringLiteral("/like"), q, ok, err);
}

void NeteaseApiClient::likeList(const QString &uid, JsonArrayFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("uid"), uid);
    get(QStringLiteral("/likelist"), q, [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("ids")).toArray()); }, err);
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
    return MusicSource::makeOnlineSong(
        sourceId(), sourceScheme(),
        obj.value(QStringLiteral("id")).toVariant().toString(),
        obj.value(QStringLiteral("name")).toString(),
        artists.join(QLatin1Char('/')),
        al.value(QStringLiteral("name")).toString(),
        durationMs,
        coverUrl,
        al.value(QStringLiteral("id")).toVariant().toString(),
        artistRemoteId);
}

} // namespace core
