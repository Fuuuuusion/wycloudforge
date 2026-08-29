#pragma once

#include "core/Song.h"

#include <QAbstractTableModel>
#include <QSet>

namespace ui {

class SongListModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Roles {
        IndexRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        AlbumRole,
        DurationRole,
        IsPlayingRole,
        SourceRole,
        CachedRole,
        MissingRole,
        DownloadedRole,
        FavoriteRole,
        SelectedRole,
        BatchModeRole,
        SongRole
    };

    explicit SongListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void setSongs(const QList<core::Song> &songs, qint64 playingId = -1);
    void refreshSongs(const QList<core::Song> &songs);
    QList<core::Song> songs() const { return m_songs; }
    qint64 playingId() const { return m_playingId; }
    core::Song songAt(int row) const;
    void setPlayingId(qint64 playingId);
    void setFavoriteIds(const QSet<qint64> &ids);
    void setBatchMode(bool enabled);
    void setSelectedRows(const QSet<int> &rows);

private:
    QList<core::Song> m_songs;
    qint64 m_playingId = -1;
    QSet<qint64> m_favoriteIds;
    QSet<int> m_selectedRows;
    bool m_batchMode = false;
};

} // namespace ui
