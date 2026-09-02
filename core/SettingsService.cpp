#include "SettingsService.h"

#include "core/CredentialStore.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSettings>

namespace core {

QStringList SettingsService::s_foldersOverride;
QString SettingsService::s_lastSongPathOverride;
QString SettingsService::s_recommendCachePathOverride;
qint64 SettingsService::s_lastSongPositionOverride = 0;
bool SettingsService::s_hasOverrides = false;

QStringList SettingsService::musicFolders()
{
    if (s_hasOverrides)
        return s_foldersOverride;
    QSettings s;
    return s.value(QStringLiteral("library/folders")).toStringList();
}

void SettingsService::setMusicFolders(const QStringList &folders)
{
    if (s_hasOverrides)
        return;
    QSettings s;
    s.setValue(QStringLiteral("library/folders"), folders);
}

void SettingsService::setFoldersOverride(const QStringList &folders)
{
    s_hasOverrides = true;
    s_foldersOverride = folders;
}

int SettingsService::volume(int fallback)
{
    QSettings s;
    return s.value(QStringLiteral("player/volume"), fallback).toInt();
}

void SettingsService::setVolume(int volume)
{
    QSettings s;
    s.setValue(QStringLiteral("player/volume"), volume);
}

bool SettingsService::muted(bool fallback)
{
    QSettings s;
    return s.value(QStringLiteral("player/muted"), fallback).toBool();
}

void SettingsService::setMuted(bool muted)
{
    QSettings s;
    s.setValue(QStringLiteral("player/muted"), muted);
}

int SettingsService::playMode(int fallback)
{
    QSettings s;
    return s.value(QStringLiteral("player/mode"), fallback).toInt();
}

void SettingsService::setPlayMode(int mode)
{
    QSettings s;
    s.setValue(QStringLiteral("player/mode"), mode);
}

int SettingsService::lyricFontSize(int fallback)
{
    QSettings s;
    return s.value(QStringLiteral("player/lyricFontSize"), fallback).toInt();
}

void SettingsService::setLyricFontSize(int size)
{
    QSettings s;
    s.setValue(QStringLiteral("player/lyricFontSize"), size);
}

int SettingsService::themeMode(int fallback)
{
    return QSettings().value(QStringLiteral("ui/themeMode"), fallback).toInt();
}

void SettingsService::setThemeMode(int mode)
{
    QSettings().setValue(QStringLiteral("ui/themeMode"), qBound(0, mode, 2));
}

QString SettingsService::lastSongPath()
{
    if (s_hasOverrides)
        return s_lastSongPathOverride;
    QSettings s;
    return s.value(QStringLiteral("player/lastSongPath")).toString();
}

qint64 SettingsService::lastSongPositionMs()
{
    if (s_hasOverrides)
        return s_lastSongPositionOverride;
    QSettings s;
    return s.value(QStringLiteral("player/lastSongPositionMs"), 0).toLongLong();
}

void SettingsService::saveLastSong(const QString &path, qint64 positionMs)
{
    if (s_hasOverrides) {
        s_lastSongPathOverride = path;
        s_lastSongPositionOverride = positionMs;
        return;
    }
    QSettings s;
    s.setValue(QStringLiteral("player/lastSongPath"), path);
    s.setValue(QStringLiteral("player/lastSongPositionMs"), positionMs);
}

void SettingsService::setLastSongOverride(const QString &path, qint64 positionMs)
{
    s_hasOverrides = true;
    s_lastSongPathOverride = path;
    s_lastSongPositionOverride = positionMs;
}

QByteArray SettingsService::windowGeometry()
{
    QSettings s;
    return s.value(QStringLiteral("window/geometry")).toByteArray();
}

void SettingsService::saveWindowGeometry(const QByteArray &geometry)
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), geometry);
}

QString SettingsService::onlineApiBase(const QString &fallback)
{
    QSettings s;
    return s.value(QStringLiteral("online/apiBase"), fallback).toString();
}

void SettingsService::setOnlineApiBase(const QString &url)
{
    QSettings s;
    s.setValue(QStringLiteral("online/apiBase"), url);
}

bool SettingsService::onlineAutoStart(bool fallback)
{
    QSettings s;
    return s.value(QStringLiteral("online/autoStart"), fallback).toBool();
}

void SettingsService::setOnlineAutoStart(bool on)
{
    QSettings s;
    s.setValue(QStringLiteral("online/autoStart"), on);
}

QString SettingsService::onlineApiDir()
{
    QSettings s;
    return s.value(QStringLiteral("online/apiDir")).toString();
}

void SettingsService::setOnlineApiDir(const QString &dir)
{
    QSettings s;
    s.setValue(QStringLiteral("online/apiDir"), dir);
}

int SettingsService::onlineCacheMaxCount(int fallback)
{
    QSettings s;
    return s.value(QStringLiteral("online/cacheMaxCount"), fallback).toInt();
}

void SettingsService::setOnlineCacheMaxCount(int count)
{
    QSettings s;
    s.setValue(QStringLiteral("online/cacheMaxCount"), count);
}

int SettingsService::onlineCacheMaxMB(int fallback)
{
    QSettings s;
    return s.value(QStringLiteral("online/cacheMaxMB"), fallback).toInt();
}

QString SettingsService::onlineDownloadDir()
{
    const QString configured = QSettings().value(QStringLiteral("online/downloadDir")).toString();
    if (!configured.isEmpty())
        return configured;
    const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    const QString legacy = music + QStringLiteral("/NeteaseClone Downloads");
    // Existing installations keep their old folder. Fresh installs use the public
    // product name without making downloaded music appear to disappear.
    return QDir(legacy).exists() ? legacy : music + QStringLiteral("/FuSinplayer Downloads");
}

void SettingsService::setOnlineDownloadDir(const QString &dir)
{
    QSettings s;
    s.setValue(QStringLiteral("online/downloadDir"), dir);
}

void SettingsService::setOnlineCacheMaxMB(int mb)
{
    QSettings s;
    s.setValue(QStringLiteral("online/cacheMaxMB"), mb);
}

QString SettingsService::onlineCookie()
{
    CredentialStore::migrateLegacy(QStringLiteral("netease"), QStringLiteral("online/cookie"));
    return CredentialStore::read(QStringLiteral("netease"));
}

void SettingsService::setOnlineCookie(const QString &cookie)
{
    if (cookie.isEmpty()) {
        CredentialStore::remove(QStringLiteral("netease"));
        QSettings().remove(QStringLiteral("online/cookie"));
    } else if (CredentialStore::write(QStringLiteral("netease"), cookie)) {
        QSettings().remove(QStringLiteral("online/cookie"));
    }
}

qint64 SettingsService::onlineUid()
{
    QSettings s;
    return s.value(QStringLiteral("online/uid"), 0).toLongLong();
}

void SettingsService::setOnlineUid(qint64 uid)
{
    QSettings s;
    s.setValue(QStringLiteral("online/uid"), uid);
}

QString SettingsService::onlineNickname()
{
    QSettings s;
    return s.value(QStringLiteral("online/nickname")).toString();
}

void SettingsService::setOnlineNickname(const QString &name)
{
    QSettings s;
    s.setValue(QStringLiteral("online/nickname"), name);
}

QString SettingsService::onlineAvatarUrl()
{
    QSettings s;
    return s.value(QStringLiteral("online/avatarUrl")).toString();
}

void SettingsService::setOnlineAvatarUrl(const QString &url)
{
    QSettings s;
    s.setValue(QStringLiteral("online/avatarUrl"), url);
}

QString SettingsService::qqCookie()
{
    CredentialStore::migrateLegacy(QStringLiteral("qqmusic"), QStringLiteral("qq/cookie"));
    return CredentialStore::read(QStringLiteral("qqmusic"));
}

void SettingsService::setQqCookie(const QString &cookie)
{
    if (cookie.isEmpty()) {
        CredentialStore::remove(QStringLiteral("qqmusic"));
        QSettings().remove(QStringLiteral("qq/cookie"));
    } else if (CredentialStore::write(QStringLiteral("qqmusic"), cookie)) {
        QSettings().remove(QStringLiteral("qq/cookie"));
    }
}

QString SettingsService::qqApiBase(const QString &fallback)
{
    return QSettings().value(QStringLiteral("qq/apiBase"), fallback).toString();
}

void SettingsService::setQqApiBase(const QString &url)
{
    QSettings().setValue(QStringLiteral("qq/apiBase"), url);
}

bool SettingsService::qqAutoStart(bool fallback)
{
    return QSettings().value(QStringLiteral("qq/autoStart"), fallback).toBool();
}

void SettingsService::setQqAutoStart(bool on)
{
    QSettings().setValue(QStringLiteral("qq/autoStart"), on);
}

QString SettingsService::qqApiDir()
{
    return QSettings().value(QStringLiteral("qq/apiDir")).toString();
}

void SettingsService::setQqApiDir(const QString &dir)
{
    QSettings().setValue(QStringLiteral("qq/apiDir"), dir);
}

QString SettingsService::qqUserId()
{
    QSettings settings;
    const QString text = settings.value(QStringLiteral("qq/userId")).toString();
    if (!text.isEmpty())
        return text;
    const qint64 legacy = settings.value(QStringLiteral("qq/uid"), 0).toLongLong();
    return legacy > 0 ? QString::number(legacy) : QString();
}

void SettingsService::setQqUserId(const QString &uid)
{
    QSettings settings;
    settings.setValue(QStringLiteral("qq/userId"), uid);
    settings.remove(QStringLiteral("qq/uid"));
}

qint64 SettingsService::qqUid()
{
    return qqUserId().toLongLong();
}

void SettingsService::setQqUid(qint64 uid)
{
    setQqUserId(uid > 0 ? QString::number(uid) : QString());
}

QString SettingsService::qqNickname()
{
    QSettings s;
    return s.value(QStringLiteral("qq/nickname")).toString();
}

void SettingsService::setQqNickname(const QString &name)
{
    QSettings s;
    s.setValue(QStringLiteral("qq/nickname"), name);
}

QString SettingsService::qqAvatarUrl()
{
    const QString value = QSettings().value(QStringLiteral("qq/avatarUrl")).toString();
    return value.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
            || value.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)
        ? QString() : value;
}

void SettingsService::setQqAvatarUrl(const QString &url)
{
    QSettings().setValue(QStringLiteral("qq/avatarUrl"), url);
}

QString SettingsService::qqAvatarRemoteUrl()
{
    QSettings settings;
    const QString value = settings.value(QStringLiteral("qq/avatarRemoteUrl")).toString();
    if (!value.isEmpty())
        return value;
    // 兼容短暂使用 qq/avatarUrl 保存远程 URL 的开发版本。
    const QString legacy = settings.value(QStringLiteral("qq/avatarUrl")).toString();
    return legacy.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
            || legacy.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)
        ? legacy : QString();
}

void SettingsService::setQqAvatarRemoteUrl(const QString &url)
{
    QSettings().setValue(QStringLiteral("qq/avatarRemoteUrl"), url);
}

int SettingsService::onlineVipStatus(int fallback)
{
    return QSettings().value(QStringLiteral("online/vipStatus"), fallback).toInt();
}

void SettingsService::setOnlineVipStatus(int status)
{
    QSettings().setValue(QStringLiteral("online/vipStatus"), qBound(-1, status, 1));
}

int SettingsService::qqVipStatus(int fallback)
{
    return QSettings().value(QStringLiteral("qq/vipStatus"), fallback).toInt();
}

void SettingsService::setQqVipStatus(int status)
{
    QSettings().setValue(QStringLiteral("qq/vipStatus"), qBound(-1, status, 1));
}

int SettingsService::avatarSource(int fallback)
{
    QSettings s;
    return s.value(QStringLiteral("account/avatarSource"), fallback).toInt();
}

void SettingsService::setAvatarSource(int source)
{
    QSettings s;
    s.setValue(QStringLiteral("account/avatarSource"), source);
}

QString SettingsService::avatarUploadPath()
{
    QSettings s;
    return s.value(QStringLiteral("account/avatarUpload")).toString();
}

void SettingsService::setAvatarUploadPath(const QString &path)
{
    QSettings s;
    s.setValue(QStringLiteral("account/avatarUpload"), path);
}

QString SettingsService::recommendCachePath()
{
    if (!s_recommendCachePathOverride.isEmpty())
        return s_recommendCachePathOverride;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/recommend.json");
}

int SettingsService::accountDisplaySource(int fallback)
{
    return QSettings().value(QStringLiteral("account/displaySource"), fallback).toInt();
}

void SettingsService::setAccountDisplaySource(int source)
{
    QSettings settings;
    if (source < 0)
        settings.remove(QStringLiteral("account/displaySource"));
    else
        settings.setValue(QStringLiteral("account/displaySource"), source);
}

QString SettingsService::cloudPlaylistCachePath()
{
    const QFileInfo recommendFile(recommendCachePath());
    return recommendFile.dir().filePath(QStringLiteral("cloud-playlists.json"));
}

void SettingsService::setRecommendCachePathOverride(const QString &path)
{
    s_recommendCachePathOverride = path;
}

} // namespace core
