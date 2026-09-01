#pragma once

#include "core/PlaylistController.h"
#include "core/MusicSource.h"

#include <QWidget>

class QGridLayout;

namespace ui {

class SelfPlaylistsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SelfPlaylistsPage(QWidget *parent = nullptr);

    void setPlaylists(const QList<core::PlaylistController::PlaylistInfo> &playlists);
    void setCloudPlaylists(const QList<core::OnlinePlaylist> &playlists);

signals:
    void openPlaylistRequested(int playlistId);
    void openCloudPlaylistRequested(int sourceId, const QString &remoteId,
                                    const QString &name);
    void createPlaylistRequested();

private:
    void rebuild();

    QGridLayout *m_grid = nullptr;
    QList<core::PlaylistController::PlaylistInfo> m_playlists;
    QList<core::OnlinePlaylist> m_cloudPlaylists;
};

} // namespace ui
