#pragma once

#include "core/Song.h"

#include <QHash>
#include <QPixmap>
#include <QSize>

namespace ui {

class CoverProvider
{
public:
    // 封面:内嵌缓存图 → 纯色占位图;返回圆角方形
    static QPixmap coverFor(const core::Song &song, int size, qreal radius = 6.0);
    static QPixmap placeholder(const QString &seed, int size, qreal radius = 6.0);

    static void clearCache();

private:
    static QHash<QString, QPixmap> s_cache;
};

} // namespace ui
