#include "SongListModel.h"

#include <QColor>

namespace ui {

SongListModel::SongListModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int SongListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_songs.size();
}

int SongListModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 7; // 序号 / 歌名 / 歌手 / 专辑 / 时长 / 收藏 / 下载
}

QVariant SongListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_songs.size())
        return {};
    const core::Song &song = m_songs[index.row()];
    switch (role) {
    case IndexRole: return index.row();
    case TitleRole: return song.title;
    case ArtistRole: return song.artist;
    case AlbumRole: return song.album;
    case DurationRole: return song.durationMs;
    case IsPlayingRole: return song.id == m_playingId;
    case SourceRole: return song.source;
    case CachedRole: return song.isCached();
    case MissingRole: return song.missing;
    case DownloadedRole: return song.isDownloaded();
    case FavoriteRole: return m_favoriteIds.contains(song.id);
    case SelectedRole: return m_selectedRows.contains(index.row());
    case BatchModeRole: return m_batchMode;
    case Qt::ToolTipRole: {
        if (song.isOnline()) {
            if (song.isDownloaded())
                return QStringLiteral("在线歌曲(已下载,可离线播放)");
            if (song.isCached())
                return QStringLiteral("在线歌曲(已缓存,可离线播放)");
            if (song.missing)
                return QStringLiteral("在线歌曲(已失效)");
            return QStringLiteral("在线歌曲(需网络)");
        }
        return song.missing ? QStringLiteral("本地歌曲(文件缺失)") : QStringLiteral("本地歌曲");
    }
    case SongRole: return QVariant::fromValue(song);
    default: return {};
    }
}

void SongListModel::setSongs(const QList<core::Song> &songs, qint64 playingId)
{
    beginResetModel();
    m_songs = songs;
    m_playingId = playingId;
    endResetModel();
}

void SongListModel::refreshSongs(const QList<core::Song> &songs)
{
    if (songs.size() != m_songs.size()) {
        setSongs(songs, m_playingId);
        return;
    }
    m_songs = songs;
    if (!m_songs.isEmpty())
        emit dataChanged(index(0, 0), index(m_songs.size() - 1, columnCount() - 1));
}

core::Song SongListModel::songAt(int row) const
{
    if (row < 0 || row >= m_songs.size())
        return {};
    return m_songs[row];
}

void SongListModel::setPlayingId(qint64 playingId)
{
    if (m_playingId == playingId)
        return;
    m_playingId = playingId;
    if (!m_songs.isEmpty())
        emit dataChanged(index(0, 0), index(m_songs.size() - 1, columnCount() - 1));
}

void SongListModel::setFavoriteIds(const QSet<qint64> &ids)
{
    if (m_favoriteIds == ids)
        return;
    m_favoriteIds = ids;
    if (!m_songs.isEmpty())
        emit dataChanged(index(0, 0), index(m_songs.size() - 1, columnCount() - 1));
}

void SongListModel::setBatchMode(bool enabled)
{
    if (m_batchMode == enabled)
        return;
    m_batchMode = enabled;
    if (!enabled)
        m_selectedRows.clear();
    if (!m_songs.isEmpty())
        emit dataChanged(index(0, 0), index(m_songs.size() - 1, 0));
}

void SongListModel::setSelectedRows(const QSet<int> &rows)
{
    const QSet<int> old = m_selectedRows;
    m_selectedRows = rows;
    QSet<int> changed = old;
    changed.unite(rows);
    for (int row : changed) {
        if (row >= 0 && row < m_songs.size())
            emit dataChanged(index(row, 0), index(row, 0));
    }
}

} // namespace ui
