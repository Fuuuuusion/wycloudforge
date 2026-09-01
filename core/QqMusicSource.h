#pragma once

#include "core/MusicSource.h"

#include <QHash>
#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

namespace core {

class QqMusicSource : public QObject, public MusicSource
{
    Q_OBJECT
public:
    explicit QqMusicSource(QObject *parent = nullptr);

    SourceId sourceId() const override { return SourceId::QqMusic; }
    QString sourceName() const override { return QStringLiteral("QQ音乐"); }
    QString sourceScheme() const override { return QStringLiteral("qqmusic"); }
    SourceCapabilities capabilities() const override
    {
        SourceCapabilities value;
        value.search = true;
        value.recommendations = true;
        value.playlists = true;
        value.account = true;
        value.lyrics = true;
        value.downloads = true;
        value.searchSuggestions = true;
        value.hotSearch = true;
        value.searchArtists = true;
        value.searchAlbums = true;
        value.searchPlaylists = true;
        value.searchLyrics = true;
        return value;
    }

    void setBaseUrl(const QString &url) { m_base = url; }
    QString baseUrl() const { return m_base; }
    void setCookie(const QString &cookie) override { m_cookie = cookie; }
    QString cookie() const override { return m_cookie; }

    void post(const QString &path, const QJsonObject &payload, OkFn ok, ErrFn err = {},
              quint64 searchGeneration = 0);
    void startQrLogin(const QString &method, OkFn ok, ErrFn err = {});
    void pollQrLogin(const QString &attemptId, OkFn ok, ErrFn err = {});
    void cancelQrLogin(const QString &attemptId, OkFn ok = {}, ErrFn err = {});
    void validateCredential(const QString &credential, const QString &loginMethod,
                            OkFn ok, ErrFn err = {});
    void vipStatus(OkFn ok, ErrFn err = {});

    void searchSongs(const QString &keywords, int limit, JsonArrayFn ok, ErrFn err = {}) override;
    void searchSongsPage(const QString &keywords, int limit, int offset,
                         JsonArrayFn ok, ErrFn err = {}) override;
    void search(const SearchRequest &request, SearchResponseFn ok, ErrFn err = {}) override;
    void searchSuggestions(const QString &keywords, int limit,
                           SearchSuggestionsFn ok, ErrFn err = {}) override;
    void hotSearch(int limit, HotSearchFn ok, ErrFn err = {}) override;
    void cancelSearch(quint64 generation) override;
    void songUrls(const QStringList &ids, JsonArrayFn ok, ErrFn err = {}) override;
    void songUrls(const QList<Song> &songs, JsonArrayFn ok, ErrFn err = {}) override;
    void lyric(const QString &id, String3Fn ok, ErrFn err = {}) override;
    void songDetail(const QString &id, OkFn ok, ErrFn err = {}) override;
    void albumDetail(const QString &id, OkFn ok, ErrFn err = {}) override;
    void artistDetail(const QString &id, OkFn ok, ErrFn err = {}) override;
    void artistSongs(const QString &id, JsonArrayFn ok, ErrFn err = {}) override;
    void playlistDetail(const QString &id, OkFn ok, ErrFn err = {}) override;
    void playlistTracks(const QString &id, JsonArrayFn ok, ErrFn err = {}) override;
    void topPlaylists(const QString &cat, int offset, JsonArrayFn ok, ErrFn err = {}) override;
    void topLists(JsonArrayFn ok, ErrFn err = {}) override;
    void recommendSongs(JsonArrayFn ok, ErrFn err = {}) override;
    void personalFm(JsonArrayFn ok, ErrFn err = {}) override;
    void comments(const QString &id, int offset, int limit, OkFn ok, ErrFn err = {}) override;
    void qrKey(StringFn ok, ErrFn err = {}) override;
    void qrCreate(const QString &key, StringFn ok, ErrFn err = {}) override;
    void qrCheck(const QString &key, OkFn ok, ErrFn err = {}) override;
    void loginStatus(OkFn ok, ErrFn err = {}) override;
    void logout(OkFn ok, ErrFn err = {}) override;
    void userPlaylists(const QString &uid, JsonArrayFn ok, ErrFn err = {}) override;
    void like(const QString &id, bool like, OkFn ok, ErrFn err = {}) override;
    void likeList(const QString &uid, JsonArrayFn ok, ErrFn err = {}) override;

    void downloadToFile(const QUrl &url, const QString &filePath, BoolFn done) override;
    DownloadId downloadToFileWithProgress(const QUrl &url, const QString &filePath,
                                          DownloadProgressFn progress,
                                          DownloadDoneFn done) override;
    void cancelDownload(DownloadId id) override;

    Song songFromJson(const QJsonObject &obj) const override;

private:
    static void unsupported(const QString &feature, ErrFn err);

    QNetworkAccessManager *m_nam = nullptr;
    QHash<DownloadId, QPointer<QNetworkReply>> m_downloads;
    QHash<quint64, QList<QPointer<QNetworkReply>>> m_searchReplies;
    QHash<QString, QString> m_qrImages;
    DownloadId m_nextDownloadId = 1;
    QString m_base = QStringLiteral("http://127.0.0.1:3200");
    QString m_cookie;
};

} // namespace core
