#pragma once

#include "core/MusicSource.h"

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QUrlQuery>

class QNetworkAccessManager;
class QNetworkReply;

namespace core {

class NeteaseApiClient : public QObject, public MusicSource
{
    Q_OBJECT
public:
    explicit NeteaseApiClient(QObject *parent = nullptr);

    int sourceId() const override { return 1; }
    QString sourceName() const override { return QStringLiteral("网易云"); }
    QString sourceScheme() const override { return QStringLiteral("netease"); }

    void setBaseUrl(const QString &url) { m_base = url; }
    QString baseUrl() const { return m_base; }

    void setCookie(const QString &cookie) override { m_cookie = cookie; }
    QString cookie() const override { return m_cookie; }

    void get(const QString &path, const QUrlQuery &query, OkFn ok, ErrFn err = {});
    void checkReachable(BoolFn done);

    void searchSongs(const QString &keywords, int limit, JsonArrayFn ok, ErrFn err = {}) override;
    void searchSongsPage(const QString &keywords, int limit, int offset,
                         JsonArrayFn ok, ErrFn err = {}) override;
    void matchSong(const QString &title, const QString &artist, const QString &album,
                   qint64 durationMs, const QString &md5, JsonArrayFn ok, ErrFn err = {});
    void songUrls(const QList<qint64> &ids, JsonArrayFn ok, ErrFn err = {}) override;
    void songDetails(const QList<qint64> &ids, JsonArrayFn ok, ErrFn err = {});
    void lyric(qint64 id, String3Fn ok, ErrFn err = {}) override;
    void songDetail(qint64 id, OkFn ok, ErrFn err = {}) override;
    void albumDetail(qint64 id, OkFn ok, ErrFn err = {}) override;
    void artistDetail(qint64 id, OkFn ok, ErrFn err = {}) override;
    void artistSongs(qint64 id, JsonArrayFn ok, ErrFn err = {}) override;
    void playlistDetail(qint64 id, OkFn ok, ErrFn err = {}) override;
    void playlistTracks(qint64 id, JsonArrayFn ok, ErrFn err = {}) override;
    void topPlaylists(const QString &cat, int offset, JsonArrayFn ok, ErrFn err = {}) override;
    void topLists(JsonArrayFn ok, ErrFn err = {}) override;
    void recommendSongs(JsonArrayFn ok, ErrFn err = {}) override;
    void personalFm(JsonArrayFn ok, ErrFn err = {}) override;
    void comments(qint64 id, int offset, int limit, OkFn ok, ErrFn err = {}) override;
    void qrKey(StringFn ok, ErrFn err = {}) override;
    void qrCreate(const QString &key, StringFn ok, ErrFn err = {}) override;
    void qrCheck(const QString &key, OkFn ok, ErrFn err = {}) override;
    void loginStatus(OkFn ok, ErrFn err = {}) override;
    void logout(OkFn ok, ErrFn err = {}) override;
    void userPlaylists(qint64 uid, JsonArrayFn ok, ErrFn err = {}) override;
    void like(qint64 id, bool like, OkFn ok, ErrFn err = {}) override;
    void likeList(qint64 uid, JsonArrayFn ok, ErrFn err = {}) override;
    void downloadToFile(const QUrl &url, const QString &filePath, BoolFn done) override;
    DownloadId downloadToFileWithProgress(const QUrl &url, const QString &filePath,
                                          DownloadProgressFn progress,
                                          DownloadDoneFn done) override;
    void cancelDownload(DownloadId id) override;

    Song songFromJson(const QJsonObject &obj) const override;

private:
    QNetworkAccessManager *m_nam = nullptr;
    QHash<DownloadId, QPointer<QNetworkReply>> m_downloads;
    DownloadId m_nextDownloadId = 1;
    QString m_base = QStringLiteral("http://127.0.0.1:3000");
    QString m_cookie;
};

} // namespace core
