#pragma once

#include "core/LrcParser.h"
#include "core/Song.h"

#include <QHash>

namespace core {

class LyricsLoader
{
public:
    // 优先级:外挂 .lrc → 内嵌歌词;结果带缓存
    static QList<LyricLine> load(const Song &song);
    static QString sidecarPathFor(const QString &musicPath);
    static bool saveSidecar(const Song &song, const QString &lrcText);
    static void invalidate(const QString &musicPath);

private:
    static QHash<QString, QList<LyricLine>> s_cache;
};

} // namespace core

