#include "NeteaseApiClient.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

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

Song MusicSource::makeOnlineSong(int source, const QString &scheme, qint64 onlineId, const QString &title,
                                 const QString &artist, const QString &album, qint64 durationMs,
                                 const QString &coverUrl, qint64 albumId)
{
    Song song;
    song.source = source;
    song.onlineId = onlineId;
    song.title = title;
    song.artist = artist;
    song.album = album;
    song.durationMs = durationMs;
    song.coverUrl = coverUrl;
    song.albumId = albumId;
    song.filePath = QStringLiteral("%1://%2").arg(scheme).arg(onlineId);
    return song;
}

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
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("keywords"), keywords);
    q.addQueryItem(QStringLiteral("type"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("limit"), QString::number(limit));
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

void NeteaseApiClient::songUrls(const QList<qint64> &ids, JsonArrayFn ok, ErrFn err)
{
    QStringList s;
    for (qint64 id : ids)
        s << QString::number(id);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), s.join(QLatin1Char(',')));
    q.addQueryItem(QStringLiteral("level"), QStringLiteral("standard"));
    get(QStringLiteral("/song/url/v1"), q, [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("data")).toArray()); }, err);
}

void NeteaseApiClient::lyric(qint64 id, String3Fn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), QString::number(id));
    get(QStringLiteral("/lyric"), q, [ok](const QJsonObject &obj) {
        const QString lrc = obj.value(QStringLiteral("lrc")).toObject().value(QStringLiteral("lyric")).toString();
        const QString tlyrc = obj.value(QStringLiteral("tlyric")).toObject().value(QStringLiteral("lyric")).toString();
        const QString romalrc = obj.value(QStringLiteral("romalrc")).toObject().value(QStringLiteral("lyric")).toString();
        ok(lrc, tlyrc, romalrc);
    }, err);
}

void NeteaseApiClient::songDetail(qint64 id, OkFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("ids"), QString::number(id));
    get(QStringLiteral("/song/detail"), q, ok, err);
}

void NeteaseApiClient::albumDetail(qint64 id, OkFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), QString::number(id));
    get(QStringLiteral("/album"), q, ok, err);
}

void NeteaseApiClient::artistDetail(qint64 id, OkFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), QString::number(id));
    get(QStringLiteral("/artist/detail"), q, ok, err);
}

void NeteaseApiClient::artistSongs(qint64 id, JsonArrayFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), QString::number(id));
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("50"));
    get(QStringLiteral("/artist/songs"), q, [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("songs")).toArray()); }, err);
}

void NeteaseApiClient::playlistDetail(qint64 id, OkFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), QString::number(id));
    get(QStringLiteral("/playlist/detail"), q,
        [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("playlist")).toObject()); }, err);
}

void NeteaseApiClient::playlistTracks(qint64 id, JsonArrayFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), QString::number(id));
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("1000"));
    q.addQueryItem(QStringLiteral("offset"), QStringLiteral("0"));
    get(QStringLiteral("/playlist/track/all"), q, [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("songs")).toArray()); }, err);
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

void NeteaseApiClient::comments(qint64 id, int offset, int limit, OkFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), QString::number(id));
    q.addQueryItem(QStringLiteral("offset"), QString::number(offset));
    q.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    get(QStringLiteral("/comment/music"), q, ok, err);
}

void NeteaseApiClient::qrKey(StringFn ok, ErrFn err)
{
    get(QStringLiteral("/login/qr/key"), QUrlQuery(),
        [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("unikey")).toString()); }, err);
}

void NeteaseApiClient::qrCreate(const QString &key, StringFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("key"), key);
    q.addQueryItem(QStringLiteral("qrimg"), QStringLiteral("true"));
    get(QStringLiteral("/login/qr/create"), q,
        [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("qrimg")).toString()); }, err);
}

void NeteaseApiClient::qrCheck(const QString &key, OkFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("key"), key);
    get(QStringLiteral("/login/qr/check"), q, ok, err);
}

void NeteaseApiClient::loginStatus(OkFn ok, ErrFn err)
{
    get(QStringLiteral("/login/status"), QUrlQuery(), ok, err);
}

void NeteaseApiClient::logout(OkFn ok, ErrFn err)
{
    get(QStringLiteral("/logout"), QUrlQuery(), ok, err);
}

void NeteaseApiClient::userPlaylists(qint64 uid, JsonArrayFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("uid"), QString::number(uid));
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("50"));
    get(QStringLiteral("/user/playlist"), q, [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("playlist")).toArray()); }, err);
}

void NeteaseApiClient::like(qint64 id, bool like, OkFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("id"), QString::number(id));
    q.addQueryItem(QStringLiteral("like"), like ? QStringLiteral("true") : QStringLiteral("false"));
    get(QStringLiteral("/like"), q, ok, err);
}

void NeteaseApiClient::likeList(qint64 uid, JsonArrayFn ok, ErrFn err)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("uid"), QString::number(uid));
    get(QStringLiteral("/likelist"), q, [ok](const QJsonObject &obj) { ok(obj.value(QStringLiteral("ids")).toArray()); }, err);
}

void NeteaseApiClient::downloadToFile(const QUrl &url, const QString &filePath, BoolFn done)
{
    QNetworkRequest req(url);
    req.setTransferTimeout(60000);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, filePath, done] {
        reply->deleteLater();
        bool ok = false;
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray data = reply->readAll();
            QFile f(filePath);
            if (!data.isEmpty() && f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                ok = f.write(data) == data.size();
                f.close();
            }
        }
        if (done)
            done(ok);
    });
}

Song NeteaseApiClient::songFromJson(const QJsonObject &obj) const
{
    QStringList artists;
    QJsonArray ar = obj.value(QStringLiteral("ar")).toArray();
    if (ar.isEmpty())
        ar = obj.value(QStringLiteral("artists")).toArray();
    for (const QJsonValue &v : ar)
        artists << v.toObject().value(QStringLiteral("name")).toString();
    QJsonObject al = obj.value(QStringLiteral("al")).toObject();
    if (al.isEmpty())
        al = obj.value(QStringLiteral("album")).toObject();
    qint64 durationMs = obj.value(QStringLiteral("dt")).toVariant().toLongLong();
    if (durationMs <= 0)
        durationMs = obj.value(QStringLiteral("duration")).toVariant().toLongLong();
    return MusicSource::makeOnlineSong(
        sourceId(), sourceScheme(),
        obj.value(QStringLiteral("id")).toVariant().toLongLong(),
        obj.value(QStringLiteral("name")).toString(),
        artists.join(QLatin1Char('/')),
        al.value(QStringLiteral("name")).toString(),
        durationMs,
        al.value(QStringLiteral("picUrl")).toString(),
        al.value(QStringLiteral("id")).toVariant().toLongLong());
}

} // namespace core
