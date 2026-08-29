#pragma once

#include "core/Song.h"

#include <QWidget>

class QGridLayout;
class QButtonGroup;
class QPushButton;
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
    void setPlaylistMenuItems(const QList<QPair<int, QString>> &items);

signals:
    void playRequested(const QList<core::Song> &songs, int index);
    void artistClicked(const QString &artist);
    void albumClicked(const QString &album, const QString &artist);
    void heartRequested(int row);
    void addToPlaylistRequested(int row, int playlistId);
    void removeFromPlaylistRequested(int row);
    void deleteFromLibraryRequested(int row);
    void importRequested();
    void importFilesRequested();

private:
    QPushButton *addTopButton(const QString &text, const QString &icon);
    void rebuildArtists();
    void rebuildAlbums();
    void applyFilter();

    SongListView *m_songList = nullptr;
    QGridLayout *m_artistGrid = nullptr;
    QGridLayout *m_albumGrid = nullptr;
    QStackedWidget *m_stack = nullptr;
    QList<core::Song> m_songs;
    QList<core::Song> m_filtered;
    qint64 m_playingId = -1;
    int m_filter = 0; // 0 全部 / 1 本地导入 / 2 已缓存 / 3 已下载
};

} // namespace ui
