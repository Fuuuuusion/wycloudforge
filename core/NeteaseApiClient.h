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

    SourceId sourceId() const override { return SourceId::Netease; }
    QString sourceName() const override { return QStringLiteral("网易云"); }
    QString sourceScheme() const override { return QStringLiteral("netease"); }
    SourceCapabilities capabilities() const override
    {
        SourceCapabilities value;
        value.search = true;
        value.recommendations = true;
        value.playlists = true;
        value.account = true;
        value.lyrics = true;
        value.downloads = true;
        value.likes = true;
        value.searchSuggestions = true;
        value.hotSearch = true;
        value.searchArtists = true;
        value.searchAlbums = true;
        value.searchPlaylists = true;
        value.searchLyrics = true;
        value.defaultSearch = true;
        return value;
    }

    using MusicSource::albumDetail;
    using MusicSource::artistDetail;
    using MusicSource::artistSongs;
    using MusicSource::lyric;
    using MusicSource::playlistDetail;
    using MusicSource::playlistTracks;
    using MusicSource::songDetail;
    using MusicSource::songUrls;
    using MusicSource::userPlaylists;

    void setBaseUrl(const QString &url) { m_base = url; }
    QString baseUrl() const { return m_base; }

    void setCookie(const QString &cookie) override { m_cookie = cookie; }
    QString cookie() const override { return m_cookie; }

    void get(const QString &path, const QUrlQuery &query, OkFn ok, ErrFn err = {},
             quint64 searchGeneration = 0);
    void checkReachable(BoolFn done);

    void searchSongs(const QString &keywords, int limit, JsonArrayFn ok, ErrFn err = {}) override;
    void searchSongsPage(const QString &keywords, int limit, int offset,
                         JsonArrayFn ok, ErrFn err = {}) override;
    void search(const SearchRequest &request, SearchResponseFn ok, ErrFn err = {}) override;
    void searchSuggestions(const QString &keywords, int limit,
                           SearchSuggestionsFn ok, ErrFn err = {}) override;
    void hotSearch(int limit, HotSearchFn ok, ErrFn err = {}) override;
    void defaultSearchText(StringFn ok, ErrFn err = {}) override;
    void cancelSearch(quint64 generation) override;
    void matchSong(const QString &title, const QString &artist, const QString &album,
                   qint64 durationMs, const QString &md5, JsonArrayFn ok, ErrFn err = {});
    void songUrls(const QStringList &ids, JsonArrayFn ok, ErrFn err = {}) override;
    void songDetails(const QList<qint64> &ids, JsonArrayFn ok, ErrFn err = {});
    void lyric(const QString &id, String3Fn ok, ErrFn err = {}) override;
    void songDetail(const QString &id, OkFn ok, ErrFn err = {}) override;
    void albumDetail(const QString &id, OkFn ok, ErrFn err = {}) override;
    void artistDetail(const QString &id, OkFn ok, ErrFn err = {}) override;
    void artistSongs(const QString &id, JsonArrayFn ok, ErrFn err = {}) override;
    void playlistDetail(const QString &id, OkFn ok, ErrFn err = {}) override;
    void playlistTracks(const QString &id, JsonArrayFn ok, ErrFn err = {}) override;
    void topPlaylists(const QString &cat, int offset, JsonArrayFn ok, ErrFn err = {}) override;
    void recommendSongs(JsonArrayFn ok, ErrFn err = {}) override;
    void qrKey(StringFn ok, ErrFn err = {}) override;
    void qrCreate(const QString &key, StringFn ok, ErrFn err = {}) override;
    void qrCheck(const QString &key, OkFn ok, ErrFn err = {}) override;
    void loginStatus(OkFn ok, ErrFn err = {}) override;
    void vipStatus(qint64 uid, OkFn ok, ErrFn err = {});
    void logout(OkFn ok, ErrFn err = {}) override;
    void userPlaylists(const QString &uid, JsonArrayFn ok, ErrFn err = {}) override;
    void downloadToFile(const QUrl &url, const QString &filePath, BoolFn done) override;
    DownloadId downloadToFileWithProgress(const QUrl &url, const QString &filePath,
                                          DownloadProgressFn progress,
                                          DownloadDoneFn done) override;
    void cancelDownload(DownloadId id) override;

    Song songFromJson(const QJsonObject &obj) const override;

private:
    QNetworkAccessManager *m_nam = nullptr;
    QHash<DownloadId, QPointer<QNetworkReply>> m_downloads;
    QHash<quint64, QList<QPointer<QNetworkReply>>> m_searchReplies;
    DownloadId m_nextDownloadId = 1;
    QString m_base = QStringLiteral("http://127.0.0.1:3000");
    QString m_cookie;
};

} // namespace core
