#pragma once

#include <QMetaType>
#include <QFileInfo>
#include <QString>

namespace core {

enum class SourceId : int
{
    Local = 0,
    Netease = 1,
    QqMusic = 2
};

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

    // 多源:0 本地文件,1 网易云,2 QQ音乐。remoteId 是新的稳定远端身份，
    // onlineId/albumId 仅为旧网易云数据库兼容字段。
    int source = int(SourceId::Local);
    QString remoteId;
    // 来源用于定位实际音频文件的身份。QQ 的 songmid 与 file.media_mid
    // 并不总是相同；该字段不参与歌曲查重，只参与媒体地址解析。
    QString mediaRemoteId;
    QString albumRemoteId;
    QString artistRemoteId;
    qint64 onlineId = 0;
    QString coverUrl;
    QString cachePath;
    QString downloadPath; // 用户主动下载的永久文件路径
    qint64 albumId = 0;

    SourceId sourceId() const { return static_cast<SourceId>(source); }
    QString effectiveRemoteId() const
    {
        return !remoteId.isEmpty() ? remoteId
                                   : (onlineId > 0 ? QString::number(onlineId) : QString());
    }
    QString effectiveAlbumRemoteId() const
    {
        return !albumRemoteId.isEmpty() ? albumRemoteId
                                        : (albumId > 0 ? QString::number(albumId) : QString());
    }
    QString stableIdentity() const
    {
        if (isOnline())
            return QStringLiteral("%1:%2").arg(source).arg(effectiveRemoteId());
        return QStringLiteral("0:%1").arg(QFileInfo(filePath).absoluteFilePath());
    }
    QString selectionIdentity() const
    {
        if (isOnline() && hasRemoteIdentity())
            return QStringLiteral("remote:%1").arg(stableIdentity());
        if (!isOnline() && id > 0)
            return QStringLiteral("local:%1").arg(id);
        if (id > 0)
            return QStringLiteral("database:%1").arg(id);
        return QStringLiteral("fallback:%1").arg(stableIdentity());
    }

    bool isOnline() const { return source != int(SourceId::Local); }
    bool hasRemoteIdentity() const { return isOnline() && !effectiveRemoteId().isEmpty(); }
    bool isCached() const
    {
        return isOnline() && !cachePath.isEmpty()
            && QFileInfo(cachePath).isFile() && QFileInfo(cachePath).size() > 0;
    }
    bool isDownloaded() const
    {
        return isOnline() && !downloadPath.isEmpty()
            && QFileInfo(downloadPath).isFile() && QFileInfo(downloadPath).size() > 0;
    }
    bool isLocallyAvailable() const
    {
        return !isOnline() || isCached() || isDownloaded();
    }
    bool operator==(const Song &other) const
    {
        if (id > 0 && other.id > 0)
            return id == other.id;
        return stableIdentity() == other.stableIdentity();
    }
};

} // namespace core

Q_DECLARE_METATYPE(core::Song)
Q_DECLARE_METATYPE(core::SourceId)
