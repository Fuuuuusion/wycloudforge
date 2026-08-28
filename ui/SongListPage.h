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
    explicit SongListPage(QWidget *parent = nullptr);

    void showContent(const QList<core::Song> &songs, const QString &title, const QString &meta,
                     qint64 playingId, bool removable = false,
                     const QString &headerCoverPath = QString());
    QList<core::Song> currentSongs() const;
    void setPlayingId(qint64 playingId);
    void refreshCovers(core::LibraryService *library);
    void setHeaderCoverPath(const QString &path);
    void setPlaylistContext(int playlistId);
    void setPlaylistMenuItems(const QList<QPair<int, QString>> &items);

signals:
    void playAllRequested(const QList<core::Song> &songs);
    void playRequested(const QList<core::Song> &songs, int index);
    void heartRequested(int row);
    void addToPlaylistRequested(int row, int playlistId);
    void removeFromPlaylistRequested(int row);
    void deleteFromLibraryRequested(int row);
    void editPlaylistRequested(int playlistId);
    void renamePlaylistRequested(int playlistId);
    void deletePlaylistRequested(int playlistId);

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
};

} // namespace ui
