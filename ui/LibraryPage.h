#pragma once

#include "core/Song.h"

#include <QWidget>

class QGridLayout;
class QStackedWidget;
class QVBoxLayout;

namespace ui {

class SongListView;

class LibraryPage : public QWidget
{
    Q_OBJECT
public:
    explicit LibraryPage(QWidget *parent = nullptr);

    void setSongs(const QList<core::Song> &songs, qint64 playingId);
    QList<core::Song> currentSongs() const;

signals:
    void playRequested(const QList<core::Song> &songs, int index);
    void artistClicked(const QString &artist);
    void albumClicked(const QString &album, const QString &artist);
    void heartRequested(int row);
    void addToPlaylistRequested(int row, int playlistId);
    void removeFromPlaylistRequested(int row);
    void deleteFromLibraryRequested(int row);

private:
    void rebuildArtists();
    void rebuildAlbums();

    SongListView *m_songList = nullptr;
    QGridLayout *m_artistGrid = nullptr;
    QGridLayout *m_albumGrid = nullptr;
    QStackedWidget *m_stack = nullptr;
    QList<core::Song> m_songs;
    qint64 m_playingId = -1;
};

} // namespace ui

