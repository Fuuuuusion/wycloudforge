#pragma once

#include "core/PlaylistController.h"

#include <QWidget>

class QGridLayout;

namespace ui {

class SelfPlaylistsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SelfPlaylistsPage(QWidget *parent = nullptr);

    void setPlaylists(const QList<core::PlaylistController::PlaylistInfo> &playlists);

signals:
    void openPlaylistRequested(int playlistId);
    void createPlaylistRequested();

private:
    void rebuild();

    QGridLayout *m_grid = nullptr;
    QList<core::PlaylistController::PlaylistInfo> m_playlists;
};

} // namespace ui
