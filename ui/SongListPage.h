#pragma once

#include "core/Song.h"

#include <QWidget>

namespace core {
class LibraryService;
}

class QLabel;
class QPushButton;
class QToolButton;

namespace ui {

class SongListView;

class SongListPage : public QWidget
{
    Q_OBJECT
public:
    struct NavigationState
    {
        QList<core::Song> songs;
        QString title;
        QString meta;
        qint64 playingId = -1;
        bool removable = false;
        QString headerCoverPath;
        bool mergeSources = false;
        int playlistContext = -1;
        bool playbackQueueContext = false;
        bool readOnlyContext = true;
    };

    explicit SongListPage(QWidget *parent = nullptr);

    void showContent(const QList<core::Song> &songs, const QString &title, const QString &meta,
                     qint64 playingId, bool removable = false,
                     const QString &headerCoverPath = QString(), bool mergeSources = false);
    QList<core::Song> currentSongs() const;
    QList<core::Song> memberSongsAt(int row) const;
    void setPlayingId(qint64 playingId);
    void refreshCovers(core::LibraryService *library);
    void setHeaderCoverPath(const QString &path);
    void setPlaylistContext(int playlistId);
    void setPlaybackQueueContext();
    void setReadOnlyContext();
    void setPlaylistMenuItems(const QList<QPair<int, QString>> &items);
    NavigationState navigationState() const;
    void restoreNavigationState(const NavigationState &state);

signals:
    void backRequested();
    void playAllRequested(const QList<core::Song> &songs);
    void playRequested(const QList<core::Song> &songs, int index);
    void heartRequested(int row);
    void addToPlaylistRequested(int row, int playlistId);
    void removeFromPlaylistRequested(int row);
    void deleteFromLibraryRequested(int row);
    void editPlaylistRequested(int playlistId);
    void renamePlaylistRequested(int playlistId);
    void deletePlaylistRequested(int playlistId);
    void savePlaybackQueueRequested();
    void clearPlaybackQueueRequested();
    void removeFromPlaybackQueueRequested(int row);

private:
    QLabel *m_cover = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_meta = nullptr;
    QToolButton *m_moreBtn = nullptr;
    SongListView *m_view = nullptr;
    QList<core::Song> m_songs;
    qint64 m_playingId = -1;
    QString m_headerCoverPath;
    int m_playlistContext = -1;
    bool m_playbackQueueContext = false;
    bool m_mergeSources = false;
    bool m_removable = false;
    bool m_readOnlyContext = true;
};

} // namespace ui
