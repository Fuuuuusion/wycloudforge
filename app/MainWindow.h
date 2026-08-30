#pragma once

#include "core/ApiService.h"
#include "core/DownloadService.h"
#include "core/LibraryService.h"
#include "core/NeteaseApiClient.h"
#include "core/MusicSourceRegistry.h"
#include "core/QqApiService.h"
#include "core/QqMusicSource.h"
#include "core/PlayerService.h"
#include "core/PlaylistController.h"
#include "core/SettingsService.h"

#include "ui/SideBar.h"

#include <QJsonArray>
#include <QMainWindow>
#include <QTimer>
#include <QSet>
#include <QStackedWidget>

namespace ui {
class AccountPanel;
class DownloadPage;
class FavoritesPage;
class LibraryPage;
class PlayerBar;
class PlayingPage;
class RecommendPage;
class SearchPage;
class SelfPlaylistsPage;
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
    void resizeEvent(QResizeEvent *event) override;

private:
    void positionPlayerBar();
    void showPage(int pageId);
    void openPlaybackQueue();
    void openPlaylist(int playlistId);
    void openArtist(const QString &artist);
    void openAlbum(const QString &album, const QString &artist);
    void openLocalArtist(const QString &artist);
    void openLocalAlbum(const QString &album, const QString &artist);
    void openOnlinePlaylist(core::SourceId sourceId, const QString &remoteId,
                            const QString &name);
    void hydrateOnlineCovers(const QList<core::Song> &songs);
    void ensureOnlineCovers(const QList<core::Song> &songs);
    void startOnlineCoverDownloads();
    void openAccount();
    void openSettings();
    void openPlaylistEditor(int playlistId);
    void playSongs(const QList<core::Song> &songs, int index);
    core::Song materializeSongForAction(const core::Song &song);
    void addSongToPlaylist(const core::Song &song, int playlistId);
    QList<ui::SideBar::PlaylistItem> selfPlaylistInfos() const;
    void refreshSidebar();
    void refreshLibraryViews();
    void refreshAllPages();
    void restoreOnlineSession();
    void restoreQqSession();
    void cacheQqAvatar(const QString &remoteUrl);
    void onCurrentSongChanged(const core::Song &song, int index);
    void enrichLocalSong(const core::Song &song);
    void searchLocalMetadata(const core::Song &song);
    void resolveLocalMetadataMatch(const core::Song &song, const QJsonArray &items, bool exactHash);
    void refreshCurrentSongMetadata(qint64 songId);
    void setupShortcuts();
    void addMusicFolder();
    void addMusicFiles();
    void connectSongListActions();
    void handleSongDownload(const core::Song &song);
    bool handleSongDelete(const core::Song &song, bool batch = false);
    void handleBatchFavorite(const QList<core::Song> &songs, bool favorite);
    void handleBatchAddToPlaylist(const QList<core::Song> &songs, int playlistId);
    void handleBatchCreatePlaylist(const QList<core::Song> &songs);
    void refreshSongListStates();
    bool focusIsEditable() const;

    core::LibraryService m_library{ this };
    core::ApiService m_apiService{ this };
    core::NeteaseApiClient m_apiClient{ this };
    core::QqApiService m_qqApiService{ this };
    core::QqMusicSource m_qqClient{ this };
    core::MusicSourceRegistry m_sourceRegistry;
    core::PlaylistController m_playlists{ this };
    core::PlayerService m_player{ this };
    core::DownloadService m_downloads{ this };

    ui::TitleBar *m_titleBar = nullptr;
    ui::SideBar *m_sideBar = nullptr;
    ui::PlayerBar *m_playerBar = nullptr;
    ui::AccountPanel *m_accountPanel = nullptr;
    ui::RecommendPage *m_recommend = nullptr;
    ui::FavoritesPage *m_favorites = nullptr;
    ui::LibraryPage *m_libraryPage = nullptr;
    ui::SelfPlaylistsPage *m_selfPlaylists = nullptr;
    ui::SongListPage *m_songListPage = nullptr;
    ui::PlayingPage *m_playing = nullptr;
    ui::SearchPage *m_search = nullptr;
    ui::DownloadPage *m_downloadPage = nullptr;
    QStackedWidget *m_stack = nullptr;

    qint64 m_currentSongId = -1;
    int m_playlistContext = -1;
    int m_lastPage = 0;
    QString m_searchQuery;
    bool m_restoredLastSong = false;
    bool m_apiReady = false;
    bool m_qqApiReady = false;
    QSet<QString> m_metadataAttempted;
    QSet<qint64> m_onlineCoverAttempted;
    QSet<qint64> m_onlineCoverDetailsAttempted;
    QList<core::Song> m_onlineCoverQueue;
    int m_onlineCoverDownloadsActive = 0;
    bool m_onlineCoverDetailsInFlight = false;
    QTimer m_libraryRefreshTimer{ this };
    QTimer m_coverRefreshTimer{ this };
};
