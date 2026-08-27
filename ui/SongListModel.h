#pragma once

#include "core/Song.h"

#include <QAbstractListModel>

namespace ui {

class SongListModel : public QAbstractListModel
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
        SongRole
    };

    explicit SongListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void setSongs(const QList<core::Song> &songs, qint64 playingId = -1);
    QList<core::Song> songs() const { return m_songs; }
    core::Song songAt(int row) const;
    void setPlayingId(qint64 playingId);

private:
    QList<core::Song> m_songs;
    qint64 m_playingId = -1;
};

} // namespace ui
