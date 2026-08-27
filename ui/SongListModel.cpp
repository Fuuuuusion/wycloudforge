#include "SongListModel.h"

#include <QColor>

namespace ui {

SongListModel::SongListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int SongListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_songs.size();
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
    emit dataChanged(index(0), index(m_songs.size() - 1));
}

} // namespace ui

