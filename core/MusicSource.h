#pragma once

#include "core/Song.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkReply;

namespace core {

struct SourceCapabilities
{
    bool search = false;
    bool recommendations = false;
    bool playlists = false;
    bool account = false;
    bool lyrics = false;
    bool downloads = false;
    bool likes = false;
};

struct AccountProfile
{
    QString userId;
    QString nickname;
    QString avatarUrl;
    QString loginMethod;
};

struct LyricPayload
{
    QString original;
    QString translated;
    QString romanized;
};

struct MediaAddress
{
    QString remoteId;
    QUrl url;
    QString error;
};

// 在线音乐源统一接口:网易云 / QQ音乐 各自实现,播放器与页面只面向此接口
class MusicSource
{
public:
    using OkFn = std::function<void(const QJsonObject &)>;
    using ErrFn = std::function<void(const QString &)>;
    using JsonArrayFn = std::function<void(const QJsonArray &)>;
    using StringFn = std::function<void(const QString &)>;
    using String2Fn = std::function<void(const QString &, const QString &)>;
    using String3Fn = std::function<void(const QString &, const QString &, const QString &)>;
    using BoolFn = std::function<void(bool)>;
    using DownloadId = quint64;
    using DownloadProgressFn = std::function<void(qint64, qint64)>;
    struct DownloadResult
    {
        bool ok = false;
        QString error;
        qint64 sizeBytes = 0;
    };
    using DownloadDoneFn = std::function<void(const DownloadResult &)>;

    virtual ~MusicSource() = default;

    virtual SourceId sourceId() const = 0;
    virtual QString sourceName() const = 0;
    virtual QString sourceScheme() const = 0;
    virtual SourceCapabilities capabilities() const = 0;

    virtual void setCookie(const QString &cookie) = 0;
    virtual QString cookie() const = 0;

    virtual void searchSongs(const QString &keywords, int limit, JsonArrayFn ok, ErrFn err = {}) = 0;
    // 分页搜索; searchSongs 保留为从第一页开始的便捷接口
    virtual void searchSongsPage(const QString &keywords, int limit, int offset,
                                 JsonArrayFn ok, ErrFn err = {}) = 0;
    virtual void songUrls(const QStringList &ids, JsonArrayFn ok, ErrFn err = {}) = 0;
    // 原文 / 翻译 / 音译
    virtual void lyric(const QString &id, String3Fn ok, ErrFn err = {}) = 0;
    virtual void songDetail(const QString &id, OkFn ok, ErrFn err = {}) = 0;
    virtual void albumDetail(const QString &id, OkFn ok, ErrFn err = {}) = 0;
    virtual void artistDetail(const QString &id, OkFn ok, ErrFn err = {}) = 0;
    virtual void artistSongs(const QString &id, JsonArrayFn ok, ErrFn err = {}) = 0;
    virtual void playlistDetail(const QString &id, OkFn ok, ErrFn err = {}) = 0;
    virtual void playlistTracks(const QString &id, JsonArrayFn ok, ErrFn err = {}) = 0;
    virtual void topPlaylists(const QString &cat, int offset, JsonArrayFn ok, ErrFn err = {}) = 0;
    virtual void topLists(JsonArrayFn ok, ErrFn err = {}) = 0;
    virtual void recommendSongs(JsonArrayFn ok, ErrFn err = {}) = 0;
    virtual void personalFm(JsonArrayFn ok, ErrFn err = {}) = 0;
    virtual void comments(const QString &id, int offset, int limit, OkFn ok, ErrFn err = {}) = 0;
    virtual void qrKey(StringFn ok, ErrFn err = {}) = 0;
    virtual void qrCreate(const QString &key, StringFn ok, ErrFn err = {}) = 0;
    virtual void qrCheck(const QString &key, OkFn ok, ErrFn err = {}) = 0;
    virtual void loginStatus(OkFn ok, ErrFn err = {}) = 0;
    virtual void logout(OkFn ok, ErrFn err = {}) = 0;
    virtual void userPlaylists(const QString &uid, JsonArrayFn ok, ErrFn err = {}) = 0;
    virtual void like(const QString &id, bool like, OkFn ok, ErrFn err = {}) = 0;
    virtual void likeList(const QString &uid, JsonArrayFn ok, ErrFn err = {}) = 0;

    // 旧网易云调用点的兼容入口。新代码统一传字符串远端 ID。
    void songUrls(const QList<qint64> &ids, JsonArrayFn ok, ErrFn err = {});
    void lyric(qint64 id, String3Fn ok, ErrFn err = {});
    void songDetail(qint64 id, OkFn ok, ErrFn err = {});
    void albumDetail(qint64 id, OkFn ok, ErrFn err = {});
    void artistDetail(qint64 id, OkFn ok, ErrFn err = {});
    void artistSongs(qint64 id, JsonArrayFn ok, ErrFn err = {});
    void playlistDetail(qint64 id, OkFn ok, ErrFn err = {});
    void playlistTracks(qint64 id, JsonArrayFn ok, ErrFn err = {});
    void comments(qint64 id, int offset, int limit, OkFn ok, ErrFn err = {});
    void userPlaylists(qint64 uid, JsonArrayFn ok, ErrFn err = {});
    void like(qint64 id, bool liked, OkFn ok, ErrFn err = {});
    void likeList(qint64 uid, JsonArrayFn ok, ErrFn err = {});

    // 通用下载(播放缓存)
    virtual void downloadToFile(const QUrl &url, const QString &filePath, BoolFn done) = 0;
    // 可取消、可观察进度的下载(永久下载管理器)
    virtual DownloadId downloadToFileWithProgress(const QUrl &url, const QString &filePath,
                                                   DownloadProgressFn progress,
                                                   DownloadDoneFn done) = 0;
    virtual void cancelDownload(DownloadId id) = 0;

    virtual Song songFromJson(const QJsonObject &obj) const = 0;

    static Song makeOnlineSong(SourceId source, const QString &scheme, const QString &remoteId,
                               const QString &title,
                               const QString &artist, const QString &album, qint64 durationMs,
                               const QString &coverUrl, const QString &albumRemoteId = {},
                               const QString &artistRemoteId = {});
    static Song makeOnlineSong(int source, const QString &scheme, qint64 onlineId, const QString &title,
                               const QString &artist, const QString &album, qint64 durationMs,
                               const QString &coverUrl, qint64 albumId);
};

} // namespace core
