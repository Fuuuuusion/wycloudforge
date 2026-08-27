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

private:
    static QStringList s_foldersOverride;
    static QString s_lastSongPathOverride;
    static qint64 s_lastSongPositionOverride;
    static bool s_hasOverrides;
};

} // namespace core
