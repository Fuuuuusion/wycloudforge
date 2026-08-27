#pragma once

#include "core/Song.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <functional>

class QUrl;

namespace core {

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

    virtual ~MusicSource() = default;

    virtual int sourceId() const = 0;
    virtual QString sourceName() const = 0;
    virtual QString sourceScheme() const = 0;

    virtual void setCookie(const QString &cookie) = 0;
    virtual QString cookie() const = 0;

    virtual void searchSongs(const QString &keywords, int limit, JsonArrayFn ok, ErrFn err = {}) = 0;
    virtual void songUrls(const QList<qint64> &ids, JsonArrayFn ok, ErrFn err = {}) = 0;
    // 原文 / 翻译 / 音译
    virtual void lyric(qint64 id, String3Fn ok, ErrFn err = {}) = 0;
    virtual void songDetail(qint64 id, OkFn ok, ErrFn err = {}) = 0;
    virtual void albumDetail(qint64 id, OkFn ok, ErrFn err = {}) = 0;
    virtual void artistDetail(qint64 id, OkFn ok, ErrFn err = {}) = 0;
    virtual void artistSongs(qint64 id, JsonArrayFn ok, ErrFn err = {}) = 0;
    virtual void playlistDetail(qint64 id, OkFn ok, ErrFn err = {}) = 0;
    virtual void playlistTracks(qint64 id, JsonArrayFn ok, ErrFn err = {}) = 0;
    virtual void topPlaylists(const QString &cat, int offset, JsonArrayFn ok, ErrFn err = {}) = 0;
    virtual void topLists(JsonArrayFn ok, ErrFn err = {}) = 0;
    virtual void recommendSongs(JsonArrayFn ok, ErrFn err = {}) = 0;
    virtual void personalFm(JsonArrayFn ok, ErrFn err = {}) = 0;
    virtual void comments(qint64 id, int offset, int limit, OkFn ok, ErrFn err = {}) = 0;
    virtual void qrKey(StringFn ok, ErrFn err = {}) = 0;
    virtual void qrCreate(const QString &key, StringFn ok, ErrFn err = {}) = 0;
    virtual void qrCheck(const QString &key, OkFn ok, ErrFn err = {}) = 0;
    virtual void loginStatus(OkFn ok, ErrFn err = {}) = 0;
    virtual void logout(OkFn ok, ErrFn err = {}) = 0;
    virtual void userPlaylists(qint64 uid, JsonArrayFn ok, ErrFn err = {}) = 0;
    virtual void like(qint64 id, bool like, OkFn ok, ErrFn err = {}) = 0;
    virtual void likeList(qint64 uid, JsonArrayFn ok, ErrFn err = {}) = 0;

    // 通用下载(播放缓存)
    virtual void downloadToFile(const QUrl &url, const QString &filePath, BoolFn done) = 0;

    virtual Song songFromJson(const QJsonObject &obj) const = 0;

    static Song makeOnlineSong(int source, const QString &scheme, qint64 onlineId, const QString &title,
                               const QString &artist, const QString &album, qint64 durationMs,
                               const QString &coverUrl, qint64 albumId);
};

} // namespace core
