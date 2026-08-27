#pragma once

#include "core/LibraryService.h"
#include "core/PlayerService.h"
#include "core/PlaylistController.h"
#include "core/SettingsService.h"

#include <QMainWindow>
#include <QStackedWidget>

namespace ui {
class DiscoverPage;
class LibraryPage;
class PlayerBar;
class PlayingPage;
class SearchPage;
class SideBar;
class SongListPage;
class TitleBar;
}

class QStackedWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openPageForTesting(int pageId) { showPage(pageId); }

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void showPage(int pageId);
    void openPlaylist(int playlistId);
    void openArtist(const QString &artist);
    void openAlbum(const QString &album, const QString &artist);
    void playSongs(const QList<core::Song> &songs, int index);
    void refreshSidebar();
    void refreshLibraryViews();
    void onCurrentSongChanged(const core::Song &song, int index);
    void setupShortcuts();
    void addMusicFolder();
    bool focusIsEditable() const;

    core::LibraryService m_library{ this };
    core::PlaylistController m_playlists{ this };
    core::PlayerService m_player{ this };

    ui::TitleBar *m_titleBar = nullptr;
    ui::SideBar *m_sideBar = nullptr;
    ui::PlayerBar *m_playerBar = nullptr;
    ui::DiscoverPage *m_discover = nullptr;
    ui::LibraryPage *m_libraryPage = nullptr;
    ui::SongListPage *m_songListPage = nullptr;
    ui::PlayingPage *m_playing = nullptr;
    ui::SearchPage *m_search = nullptr;
    QStackedWidget *m_stack = nullptr;

    qint64 m_currentSongId = -1;
    int m_playlistContext = -1;
    int m_lastPage = 0;
    QString m_searchQuery;
    bool m_restoredLastSong = false;
};
