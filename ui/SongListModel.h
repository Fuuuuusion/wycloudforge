#pragma once

#include "core/SearchAggregator.h"
#include "core/Song.h"

#include <QAbstractTableModel>
#include <QSet>

namespace ui {

struct SongSourceChoice
{
    core::SourceId source = core::SourceId::Local;
    core::Song song;
    bool available = false;
    QString unavailableReason;
};

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
        DownloadingRole,
        StableIdentityRole,
        ActiveSourceRole,
        SongRole
    };

    explicit SongListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void setSongs(const QList<core::Song> &songs, qint64 playingId = -1);
    void setSearchResultGroups(const QList<core::SearchResultGroup> &groups,
                               qint64 playingId = -1);
    void refreshSongs(const QList<core::Song> &songs);
    bool updateSong(const core::Song &song);
    QList<core::Song> songs() const { return m_songs; }
    qint64 playingId() const { return m_playingId; }
    core::Song songAt(int row) const;
    QString rowIdentityAt(int row) const;
    QList<SongSourceChoice> sourceChoicesAt(int row) const;
    core::SourceId activeSourceAt(int row) const;
    bool activateSource(int row, core::SourceId source, QString *error = nullptr);
    void setPlayingId(qint64 playingId);
    void setFavoriteIds(const QSet<qint64> &ids);
    void setBatchMode(bool enabled);
    void setSelectedIdentities(const QSet<QString> &identities);
    void setDownloadingIdentities(const QSet<QString> &identities);

private:
    struct RowContext {
        QString identity;
        QList<SongSourceChoice> choices;
        core::SourceId activeSource = core::SourceId::Local;
    };

    QList<core::Song> m_songs;
    QList<RowContext> m_rows;
    qint64 m_playingId = -1;
    QSet<qint64> m_favoriteIds;
    QSet<QString> m_selectedIdentities;
    QSet<QString> m_downloadingIdentities;
    bool m_batchMode = false;
};

} // namespace ui
