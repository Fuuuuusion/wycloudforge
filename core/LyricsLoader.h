#pragma once

#include "core/LrcParser.h"
#include "core/Song.h"

#include <QHash>

namespace core {

class LyricsLoader
{
public:
    // 优先级:显式歌词路径 → 永久下载旁挂 → 播放缓存旁挂
    // → 本地导入旁挂 → 本地文件内嵌歌词;结果带缓存。
    static QList<LyricLine> load(const Song &song);
    static QString sidecarPathFor(const QString &musicPath);
    // 返回已经存在且非空的最高优先级外挂歌词。
    static QString existingSidecarPathFor(const Song &song);
    // 返回编辑歌词的保存目标;在线歌曲没有本地音频时返回空。
    static QString writableSidecarPathFor(const Song &song);
    static bool saveSidecar(const Song &song, const QString &lrcText);
    static void invalidate(const QString &musicPath);

private:
    static QHash<QString, QList<LyricLine>> s_cache;
};

} // namespace core
