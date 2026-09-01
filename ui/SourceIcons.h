#pragma once

#include "core/Song.h"

#include <QIcon>
#include <QString>

namespace ui {

inline QString sourceDisplayName(core::SourceId source)
{
    switch (source) {
    case core::SourceId::Local: return QStringLiteral("本地音乐");
    case core::SourceId::Netease: return QStringLiteral("网易云音乐");
    case core::SourceId::QqMusic: return QStringLiteral("QQ 音乐");
    }
    return QStringLiteral("未知来源");
}

inline QString sourceIconPath(core::SourceId source, bool available = true)
{
    QString name;
    switch (source) {
    case core::SourceId::Local: name = QStringLiteral("local"); break;
    case core::SourceId::Netease: name = QStringLiteral("netease"); break;
    case core::SourceId::QqMusic: name = QStringLiteral("qq"); break;
    }
    return QStringLiteral(":/source-icons/%1%2.png")
        .arg(name, available ? QString() : QStringLiteral("-disabled"));
}

inline QIcon sourceIcon(core::SourceId source, bool available = true)
{
    return QIcon(sourceIconPath(source, available));
}

} // namespace ui
