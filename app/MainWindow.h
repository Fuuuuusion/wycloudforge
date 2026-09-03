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
#include "ui/SongListPage.h"

#include <QJsonArray>
#include <QHash>
#include <QMainWindow>
#include <QTimer>
#include <QSet>
#include <QStackedWidget>

namespace ui {
class AccountPanel;
class AiReportPage;
class DownloadPage;
class FavoritesPage;
class LibraryPage;
class PlayerBar;
class PlayingPage;
class RecommendPage;
class SearchPage;
class SelfPlaylistsPage;
class SideBar;
class SidebarFooter;
class TitleBar;
}

class QStackedWidget;
class QPushButton;
class QResizeEvent;

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
    static constexpr int kAiReportPageIndex = 8;

    struct RouteEntry
    {
        int pageId = -1;
        ui::SongListPage::NavigationState songListState;
        bool hasSongListState = false;
        int playlistContext = -1;
        QString cloudPlaylistContext;
    };

    void showPage(int pageId);
    void navigateBack();
    RouteEntry captureCurrentRoute() const;
    void pushCurrentRoute();
    void prepareSongListNavigation();
    void openPlaybackQueue();
    void openPlaylist(int playlistId);
    void openArtist(const QString &artist);
    void openAlbum(const QString &album, const QString &artist);
    void openLocalArtist(const QString &artist);
    void openLocalAlbum(const QString &album, const QString &artist);
    void openOnlinePlaylist(core::SourceId sourceId, const QString &remoteId,
                            const QString &name, bool cloudContext = false);
    void openOnlineArtist(core::SourceId sourceId, const QString &remoteId,
                          const QString &name);
    void openOnlineAlbum(core::SourceId sourceId, const QString &remoteId,
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
    void loadCloudPlaylistCache();
    void saveCloudPlaylistCache() const;
    void refreshCloudPlaylists(core::SourceId sourceId, const QString &userId);
    void removeCloudPlaylists(core::SourceId sourceId);
    void replaceCloudPlaylists(core::SourceId sourceId,
                               const QList<core::OnlinePlaylist> &playlists);
    void queueCloudPlaylistCovers(const QList<core::OnlinePlaylist> &playlists);
    void startCloudPlaylistCoverDownloads();
    void cacheQqAvatar(const QString &remoteUrl);
    void onCurrentSongChanged(const core::Song &song, int index);
    void enrichLocalSong(const core::Song &song);
    void searchLocalMetadata(const core::Song &song);
    void resolveLocalMetadataMatch(const core::Song &song, const QJsonArray &items, bool exactHash);
    void refreshCurrentSongMetadata(qint64 songId);
    void setupShortcuts();
    void ensureQqSearchSource();
    void refreshSearchAfterQqReady();
    void addMusicFolder();
    void addMusicFiles();
    void connectSongListActions();
    void handleSongDownload(const core::Song &song);
    bool handleSongDelete(const core::Song &song, bool batch = false);
    void handleBatchFavorite(const QList<core::Song> &songs, bool favorite);
    void handleBatchAddToPlaylist(const QList<core::Song> &songs, int playlistId);
    void handleBatchCreatePlaylist(const QList<core::Song> &songs);
    void refreshSongListStates();
    void refreshDownloadVisualStates();
    void refreshDownloadPage();
    void refreshFromSidebar();
    void finishSidebarRefresh(const QString &message);
    void resetManagedCacheViews();
    void refreshSourceAccessStates();
    bool focusIsEditable() const;
    void requestNativeWindowCommand(quint32 command);
    void updateWindowCorners();

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
    ui::AiReportPage *m_aiReportPage = nullptr;
    QStackedWidget *m_stack = nullptr;
    QPushButton *m_sidebarRefreshButton = nullptr;
    ui::SidebarFooter *m_sidebarFooter = nullptr;

    qint64 m_currentSongId = -1;
    int m_playlistContext = -1;
    QString m_cloudPlaylistContext;
    int m_lastPage = 0;
    QList<RouteEntry> m_navigationHistory;
    bool m_navigatingBack = false;
    QString m_searchQuery;
    bool m_restoredLastSong = false;
    bool m_apiReady = false;
    bool m_qqApiReady = false;
    bool m_neteaseSessionVerifying = false;
    bool m_qqSessionVerifying = false;
    bool m_qqApiStarting = false;
    bool m_qqSearchExecutionPending = false;
    bool m_sidebarRefreshInProgress = false;
    QHash<int, core::SourceAccessState> m_sourceAccessStates;
    quint64 m_onlineDetailGeneration = 0;
    quint64 m_onlineCoverGeneration = 0;
    QHash<int, quint64> m_cloudPlaylistGenerations;
    QSet<QString> m_metadataAttempted;
    QHash<QString, qint64> m_activeDownloadSongIds;
    QSet<qint64> m_onlineCoverAttempted;
    QSet<qint64> m_onlineCoverDetailsAttempted;
    QList<core::Song> m_onlineCoverQueue;
    QList<core::OnlinePlaylist> m_cloudPlaylists;
    QList<core::OnlinePlaylist> m_cloudPlaylistCoverQueue;
    QSet<QString> m_cloudPlaylistCoverQueued;
    int m_onlineCoverDownloadsActive = 0;
    int m_cloudPlaylistCoverDownloadsActive = 0;
    bool m_onlineCoverDetailsInFlight = false;
    QTimer m_libraryRefreshTimer{ this };
    QTimer m_coverRefreshTimer{ this };
};
