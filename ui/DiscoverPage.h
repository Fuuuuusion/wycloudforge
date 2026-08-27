#pragma once

#include "core/PlaylistController.h"
#include "core/Song.h"

#include <QWidget>

class QGridLayout;
class QHBoxLayout;
class QVBoxLayout;

namespace ui {

class DiscoverPage : public QWidget
{
    Q_OBJECT
public:
    explicit DiscoverPage(QWidget *parent = nullptr);

    void setLibrary(const QList<core::Song> &songs, qint64 playingId);
    void setPlaylists(const QList<core::PlaylistController::PlaylistInfo> &playlists);
    void setRecent(const QList<core::Song> &recent);

signals:
    void playRequested(const QList<core::Song> &songs, int index);
    void playlistClicked(int playlistId);
    void importRequested();

private:
    void rebuild();

    QVBoxLayout *m_contentLayout = nullptr;
    QHBoxLayout *m_recentLayout = nullptr;
    QGridLayout *m_playlistGrid = nullptr;
    QHBoxLayout *m_artistLayout = nullptr;

    QList<core::Song> m_songs;
    QList<core::Song> m_recent;
    QList<core::PlaylistController::PlaylistInfo> m_playlists;
    qint64 m_playingId = -1;
};

} // namespace ui

