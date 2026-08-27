#include "SettingsService.h"

#include <QSettings>

namespace core {

QStringList SettingsService::s_foldersOverride;
QString SettingsService::s_lastSongPathOverride;
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

} // namespace core
