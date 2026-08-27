#pragma once

#include <QMetaType>
#include <QString>

namespace core {

struct Song
{
    qint64 id = -1;
    QString filePath;
    QString title;
    QString artist;
    QString album;
    qint64 durationMs = 0;
    QString coverPath;   // 缓存封面文件路径;为空表示无封面
    QString lyricPath;   // 外挂 .lrc 路径;为空表示未发现
    bool missing = false;
    qint64 playCount = 0;
    qint64 lastPlayedMs = 0;

    // 多源:0 本地文件,1 网易云,2 QQ音乐(预留)
    int source = 0;
    qint64 onlineId = 0;
    QString coverUrl;
    QString cachePath;
    qint64 albumId = 0;

    bool isOnline() const { return source > 0; }
    bool isCached() const { return isOnline() && !cachePath.isEmpty(); }
    bool operator==(const Song &other) const { return id == other.id && filePath == other.filePath; }
};

} // namespace core

Q_DECLARE_METATYPE(core::Song)
