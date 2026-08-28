#pragma once

#include "core/Song.h"

#include <QWidget>

namespace ui {

class SongListView;

class FavoritesPage : public QWidget
{
    Q_OBJECT
public:
    explicit FavoritesPage(QWidget *parent = nullptr);

    void setSongs(const QList<core::Song> &songs, qint64 playingId);
    void setPlayingId(qint64 playingId);
    QList<core::Song> currentSongs() const;
    void setPlaylistMenuItems(const QList<QPair<int, QString>> &items);

signals:
    void playRequested(const QList<core::Song> &songs, int index);
    void heartRequested(int row);
    void addToPlaylistRequested(int row, int playlistId);
    void removeFromPlaylistRequested(int row);

private:
    SongListView *m_view = nullptr;
};

} // namespace ui
