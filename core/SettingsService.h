#pragma once

#include <QByteArray>
#include <QStringList>

namespace core {

class SettingsService
{
public:
    static QStringList musicFolders();
    static void setMusicFolders(const QStringList &folders);
    static void setFoldersOverride(const QStringList &folders);

    static int volume(int fallback = 70);
    static void setVolume(int volume);

    static bool muted(bool fallback = false);
    static void setMuted(bool muted);

    static int playMode(int fallback = 0);
    static void setPlayMode(int mode);

    static int lyricFontSize(int fallback = 18);
    static void setLyricFontSize(int size);

    static QString lastSongPath();
    static qint64 lastSongPositionMs();
    static void saveLastSong(const QString &path, qint64 positionMs);
    static void setLastSongOverride(const QString &path, qint64 positionMs);

    static QByteArray windowGeometry();
    static void saveWindowGeometry(const QByteArray &geometry);

    // 在线服务
    static QString onlineApiBase(const QString &fallback = QStringLiteral("http://127.0.0.1:3000"));
    static void setOnlineApiBase(const QString &url);
    static bool onlineAutoStart(bool fallback = true);
    static void setOnlineAutoStart(bool on);
    static QString onlineApiDir();
    static void setOnlineApiDir(const QString &dir);
    static int onlineCacheMaxCount(int fallback = 200);
    static void setOnlineCacheMaxCount(int count);
    static int onlineCacheMaxMB(int fallback = 2048);
    static void setOnlineCacheMaxMB(int mb);
    static QString onlineDownloadDir();
    static void setOnlineDownloadDir(const QString &dir);
    static QString onlineCookie();
    static void setOnlineCookie(const QString &cookie);
    static qint64 onlineUid();
    static void setOnlineUid(qint64 uid);
    static QString onlineNickname();
    static void setOnlineNickname(const QString &name);
    static QString onlineAvatarUrl();
    static void setOnlineAvatarUrl(const QString &url);

    // QQ音乐
    static QString qqCookie();
    static void setQqCookie(const QString &cookie);
    static QString qqApiBase(const QString &fallback = QStringLiteral("http://127.0.0.1:3200"));
    static void setQqApiBase(const QString &url);
    static bool qqAutoStart(bool fallback = true);
    static void setQqAutoStart(bool on);
    static QString qqApiDir();
    static void setQqApiDir(const QString &dir);
    static QString qqUserId();
    static void setQqUserId(const QString &uid);
    static qint64 qqUid();
    static void setQqUid(qint64 uid);
    static QString qqNickname();
    static void setQqNickname(const QString &name);
    static QString qqAvatarUrl();
    static void setQqAvatarUrl(const QString &url);
    static QString qqAvatarRemoteUrl();
    static void setQqAvatarRemoteUrl(const QString &url);

    // 头像来源:0 网易云 / 1 QQ音乐 / 2 本地上传
    static int avatarSource(int fallback = 0);
    static void setAvatarSource(int source);
    static QString avatarUploadPath();
    static void setAvatarUploadPath(const QString &path);

    // 推荐缓存文件
    static QString recommendCachePath();
    static void setRecommendCachePathOverride(const QString &path);

private:
    static QStringList s_foldersOverride;
    static QString s_lastSongPathOverride;
    static QString s_recommendCachePathOverride;
    static qint64 s_lastSongPositionOverride;
    static bool s_hasOverrides;
};

} // namespace core
