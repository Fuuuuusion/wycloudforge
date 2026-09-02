#include "MainWindow.h"

#include "core/LyricsLoader.h"
#include "core/SearchAggregator.h"
#include "core/SettingsService.h"
#include "ui/AccountDialog.h"
#include "ui/AccountPanel.h"
#include "ui/AuroraBackground.h"
#include "ui/CoverProvider.h"
#include "ui/DownloadPage.h"
#include "ui/FavoritesPage.h"
#include "ui/LibraryPage.h"
#include "ui/LyricEditorDialog.h"
#include "ui/PlayerBar.h"
#include "ui/PlaylistEditDialog.h"
#include "ui/PlayingPage.h"
#include "ui/RecommendPage.h"
#include "ui/SearchPage.h"
#include "ui/SelfPlaylistsPage.h"
#include "ui/SideBar.h"
#include "ui/SidebarFooter.h"
#include "ui/SettingsDialog.h"
#include "ui/SongListPage.h"
#include "ui/SongListView.h"
#include "ui/TitleBar.h"

#include <QApplication>
#include <QAbstractScrollArea>
#include <QCursor>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QShortcut>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QSaveFile>
#include <QTextEdit>
#include <QTimer>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

#include <windows.h>

#include <algorithm>
#include <functional>

namespace {
constexpr int kResizeBorder = 5;
constexpr int kOnlineCoverDetailsBatch = 24;
constexpr int kOnlineCoverDownloadsBatch = 3;

void disableHorizontalScrollbars(QWidget *root)
{
    const QList<QAbstractScrollArea *> areas = root->findChildren<QAbstractScrollArea *>();
    for (QAbstractScrollArea *a : areas) {
        if (a->property("allowHorizontalScroll").toBool())
            continue;
        a->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
}

struct NcHitTestResult
{
    bool handled = false;
    LRESULT code = HTCLIENT;
};

NcHitTestResult computeHitTest(QWidget *window, const QPoint &cursorGlobal, const ui::TitleBar *titleBar)
{
    const QPoint pos = window->mapFromGlobal(cursorGlobal);
    const int w = window->width();
    const int h = window->height();
    const bool maxed = window->isMaximized();

    if (!maxed) {
        const bool left = pos.x() < kResizeBorder;
        const bool right = pos.x() >= w - kResizeBorder;
        const bool top = pos.y() < kResizeBorder;
        const bool bottom = pos.y() >= h - kResizeBorder;
        if (left && top) return { true, HTTOPLEFT };
        if (right && top) return { true, HTTOPRIGHT };
        if (left && bottom) return { true, HTBOTTOMLEFT };
        if (right && bottom) return { true, HTBOTTOMRIGHT };
        if (left) return { true, HTLEFT };
        if (right) return { true, HTRIGHT };
        if (top) return { true, HTTOP };
        if (bottom) return { true, HTBOTTOM };
    }

    const QRect titleGeometry = titleBar
        ? QRect(titleBar->mapTo(window, QPoint(0, 0)), titleBar->size()) : QRect();
    if (titleBar && titleGeometry.contains(pos)) {
        for (int i = 0; i <= ui::TitleBar::CloseBtn; ++i) {
            const QRect r = titleBar->windowButtonRect(i);
            if (r.translated(titleGeometry.topLeft()).contains(pos)) {
                switch (i) {
                case ui::TitleBar::CloseBtn:
                case ui::TitleBar::MaximizeBtn:
                case ui::TitleBar::MinimizeBtn:
                    return { true, HTCLIENT };
                default:
                    break;
                }
            }
        }
        const QRect sR = titleBar->searchRect();
        if (sR.isValid() && sR.translated(titleGeometry.topLeft()).contains(pos))
            return { true, HTCLIENT };
        return { true, HTCAPTION };
    }
    return { false, HTCLIENT };
}

QJsonObject cloudPlaylistToJson(const core::OnlinePlaylist &playlist)
{
    return {
        { QStringLiteral("source"), int(playlist.source) },
        { QStringLiteral("remoteId"), playlist.remoteId },
        { QStringLiteral("name"), playlist.name },
        { QStringLiteral("coverUrl"), playlist.coverUrl },
        { QStringLiteral("description"), playlist.description },
    };
}

core::OnlinePlaylist cloudPlaylistFromJson(const QJsonObject &object)
{
    core::OnlinePlaylist playlist;
    playlist.source = static_cast<core::SourceId>(
        object.value(QStringLiteral("source")).toInt());
    playlist.remoteId = object.value(QStringLiteral("remoteId")).toString().trimmed();
    playlist.name = object.value(QStringLiteral("name")).toString().trimmed();
    playlist.coverUrl = object.value(QStringLiteral("coverUrl")).toString().trimmed();
    playlist.description = object.value(QStringLiteral("description")).toString().trimmed();
    return playlist;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("仿网易云播放器"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    resize(1280, 800);
    setMinimumSize(940, 600);

    // 封面、缓存和元数据请求可能在启动后集中完成。合并短时间内的库变更，
    // 避免每张封面都同步重建侧栏和多个歌曲列表。
    m_libraryRefreshTimer.setSingleShot(true);
    m_libraryRefreshTimer.setInterval(250);
    connect(&m_libraryRefreshTimer, &QTimer::timeout, this, [this] {
        refreshSidebar();
        refreshLibraryViews();
        refreshAllPages();
        refreshDownloadPage();
        if (m_currentSongId > 0) {
            const core::Song current = m_library.songById(m_currentSongId);
            if (current.id > 0)
                m_playerBar->setSong(current, m_playlists.isFavorite(current.id));
        }
        if (!m_restoredLastSong) {
            m_restoredLastSong = true;
            const QString lastPath = core::SettingsService::lastSongPath();
            if (!lastPath.isEmpty()) {
                const auto all = m_library.allSongs();
                for (int i = 0; i < all.size(); ++i) {
                    if (QDir::cleanPath(all[i].filePath).compare(QDir::cleanPath(lastPath), Qt::CaseInsensitive) == 0) {
                        m_player.setPlaylist(all, i);
                        m_player.seek(core::SettingsService::lastSongPositionMs());
                        break;
                    }
                }
            }
        }
    });
    m_coverRefreshTimer.setSingleShot(true);
    m_coverRefreshTimer.setInterval(450);
    connect(&m_coverRefreshTimer, &QTimer::timeout, this, [this] {
        m_libraryPage->refreshCovers(&m_library);
        m_songListPage->refreshCovers(&m_library);
        refreshSongListStates();
        if (m_currentSongId > 0) {
            const core::Song current = m_library.songById(m_currentSongId);
            if (current.id > 0) {
                m_playerBar->setSong(current, m_playlists.isFavorite(current.id));
                m_playing->setSong(current, QPixmap());
            }
        }
    });

    const QByteArray geometry = core::SettingsService::windowGeometry();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    m_titleBar = new ui::TitleBar(this);
    m_titleBar->setMaximizedState(isMaximized());
    m_sideBar = new ui::SideBar(this);
    m_playerBar = new ui::PlayerBar(this);
    m_accountPanel = new ui::AccountPanel(this);

    m_recommend = new ui::RecommendPage(this);
    m_favorites = new ui::FavoritesPage(this);
    m_libraryPage = new ui::LibraryPage(this);
    m_selfPlaylists = new ui::SelfPlaylistsPage(this);
    m_songListPage = new ui::SongListPage(this);
    m_playing = new ui::PlayingPage(this);
    m_search = new ui::SearchPage(this);
    m_downloadPage = new ui::DownloadPage(this);

    m_sourceRegistry.registerSource(&m_apiClient);
    m_sourceRegistry.registerSource(&m_qqClient);
    m_player.setSourceProvider(&m_apiClient);
    m_player.setSourceRegistry(&m_sourceRegistry);
    m_player.setLibrary(&m_library);
    m_downloads.setSourceProvider(&m_apiClient);
    m_downloads.setSourceRegistry(&m_sourceRegistry);
    m_downloads.setLibrary(&m_library);
    m_search->setSourceProvider(&m_apiClient, &m_library);
    m_search->setSourceRegistry(&m_sourceRegistry);
    m_search->setOnlineSourceEnabled(core::SourceId::QqMusic, false);
    refreshSourceAccessStates();
    m_playing->setSourceProvider(&m_apiClient);
    m_playing->setSourceRegistry(&m_sourceRegistry);
    m_playing->setLibrary(&m_library);
    m_recommend->setSourceProvider(&m_apiClient, &m_library);
    m_recommend->setSourceRegistry(&m_sourceRegistry);
    m_apiClient.setCookie(core::SettingsService::onlineCookie());
    m_qqClient.setBaseUrl(core::SettingsService::qqApiBase());
    m_qqClient.setCookie(core::SettingsService::qqCookie());

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName("contentStack");
    m_stack->addWidget(m_recommend);
    m_stack->addWidget(m_favorites);
    m_stack->addWidget(m_libraryPage);
    m_stack->addWidget(m_selfPlaylists);
    m_stack->addWidget(m_songListPage);
    m_stack->addWidget(m_playing);
    m_stack->addWidget(m_search);
    m_stack->addWidget(m_downloadPage);

    connectSongListActions();

    auto *central = new ui::AuroraBackground(this);
    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *sidebarShell = new QWidget(central);
    sidebarShell->setObjectName(QStringLiteral("sidebarShell"));
    sidebarShell->setFixedWidth(240);
    sidebarShell->setStyleSheet(QStringLiteral(
        "QWidget#sidebarShell{background:#0E0E14;border:none;}"));
    auto *sideLayout = new QVBoxLayout(sidebarShell);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(0);
    sideLayout->addWidget(m_accountPanel);
    sideLayout->addWidget(m_sideBar, 1);

    auto *settingsFooter = new ui::SidebarFooter(sidebarShell);
    m_sidebarFooter = settingsFooter;
    m_sidebarRefreshButton = settingsFooter->refreshButton();
    sideLayout->addWidget(settingsFooter);
    connect(settingsFooter, &ui::SidebarFooter::refreshClicked,
            this, &MainWindow::refreshFromSidebar);
    connect(settingsFooter, &ui::SidebarFooter::downloadClicked,
            this, [this] { showPage(7); });

    auto *rightColumn = new QWidget(central);
    rightColumn->setObjectName(QStringLiteral("rightColumn"));
    auto *rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(m_titleBar);
    rightLayout->addWidget(m_stack, 1);
    rightLayout->addWidget(m_playerBar);

    rootLayout->addWidget(sidebarShell);
    rootLayout->addWidget(rightColumn, 1);
    setCentralWidget(central);
    rightColumn->setStyleSheet(QStringLiteral(
        "QWidget#rightColumn{background:#0E0E14;border:none;}"));

    // ---------- 标题栏 ----------
    connect(m_titleBar, &ui::TitleBar::minimizeClicked, this, &QWidget::showMinimized);
    connect(m_titleBar, &ui::TitleBar::maximizeClicked, this, [this] {
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(m_titleBar, &ui::TitleBar::closeClicked, this, &QWidget::close);
    const auto executeSearch = [this](const QString &requested) {
        const QString text = m_search->resolvedSearchText(requested);
        m_searchQuery = text;
        m_qqSearchExecutionPending = false;
        if (text.isEmpty()) {
            m_search->showSearchAssistant();
            showPage(6);
            ensureQqSearchSource();
            return;
        }
        m_titleBar->setSearchText(text);
        m_search->performSearch(text);
        showPage(6);
        if (!m_qqApiReady)
            m_qqSearchExecutionPending = true;
        ensureQqSearchSource();
    };
    connect(m_titleBar, &ui::TitleBar::searchRequested, this, executeSearch);
    connect(m_titleBar, &ui::TitleBar::searchTextEdited, this, [this](const QString &text) {
        m_searchQuery = text.trimmed();
        m_qqSearchExecutionPending = false;
        m_search->previewSearchText(text);
        showPage(6);
        if (text.trimmed().size() < 2)
            return;
        ensureQqSearchSource();
    });
    connect(m_titleBar, &ui::TitleBar::searchFocused, this, [this] {
        if (m_titleBar->searchText().trimmed().isEmpty()) {
            m_search->showSearchAssistant();
            showPage(6);
        }
        ensureQqSearchSource();
    });
    connect(m_titleBar, &ui::TitleBar::searchDismissed, this, [this] {
        navigateBack();
    });
    connect(m_titleBar, &ui::TitleBar::searchNavigationRequested,
            m_search, &ui::SearchPage::moveSearchAssistantSelection);
    connect(m_search, &ui::SearchPage::searchTextChosen, this, executeSearch);
    connect(m_search, &ui::SearchPage::defaultSearchTextReady,
            m_titleBar, &ui::TitleBar::setSearchPlaceholder);

    // ---------- 侧边栏 ----------
    connect(m_sideBar, &ui::SideBar::pageRequested, this, [this](int pageId) {
        m_navigationHistory.clear();
        showPage(pageId);
    });
    connect(m_sideBar, &ui::SideBar::playlistSelected, this, &MainWindow::openPlaylist);
    connect(m_sideBar, &ui::SideBar::cloudPlaylistSelected, this,
            [this](int sourceId, const QString &remoteId, const QString &name) {
        openOnlinePlaylist(static_cast<core::SourceId>(sourceId), remoteId, name, true);
    });
    connect(m_sideBar, &ui::SideBar::createPlaylistRequested, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("创建歌单"),
                                                   QStringLiteral("歌单名称:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            const int id = m_playlists.createPlaylist(name);
            if (id > 0) {
                m_playlistContext = -1;
                refreshSidebar();
                refreshAllPages();
                showPage(3);
            }
        }
    });

    // ---------- 账号区 / 设置 ----------
    connect(m_accountPanel, &ui::AccountPanel::accountClicked, this, &MainWindow::openAccount);
    connect(settingsFooter, &ui::SidebarFooter::settingsClicked,
            this, &MainWindow::openSettings);

    // ---------- 播放栏 ----------
    connect(m_playerBar, &ui::PlayerBar::playPauseClicked, this, [this] {
        if (m_player.currentIndex() < 0) {
            const auto all = m_library.allSongs();
            if (!all.isEmpty())
                playSongs(all, 0);
            return;
        }
        m_player.playPause();
    });
    connect(m_playerBar, &ui::PlayerBar::prevClicked, &m_player, &core::PlayerService::prev);
    connect(m_playerBar, &ui::PlayerBar::nextClicked, &m_player, &core::PlayerService::next);
    connect(m_playerBar, &ui::PlayerBar::modeClicked, this, [this] {
        const auto mode = core::PlayerService::PlayMode((int(m_player.mode()) + 1) % 3);
        m_player.setMode(mode);
        core::SettingsService::setPlayMode(int(mode));
    });
    connect(m_playerBar, &ui::PlayerBar::seekRequested, &m_player, &core::PlayerService::seek);
    connect(m_playerBar, &ui::PlayerBar::volumeChanged, this, [this](int v) {
        m_player.setVolume(v);
        core::SettingsService::setVolume(v);
    });
    connect(m_playerBar, &ui::PlayerBar::muteToggled, this, [this](bool muted) {
        m_player.setMuted(muted);
        core::SettingsService::setMuted(muted);
    });
    connect(m_playerBar, &ui::PlayerBar::heartToggled, this, [this](bool fav) {
        if (m_currentSongId > 0)
            m_playlists.setFavorite(m_currentSongId, fav);
    });
    connect(m_playerBar, &ui::PlayerBar::downloadRequested, this, [this] {
        handleSongDownload(m_player.currentSong());
    });
    connect(m_playerBar, &ui::PlayerBar::deleteDownloadRequested, this, [this] {
        handleSongDelete(materializeSongForAction(m_player.currentSong()));
    });
    connect(m_playerBar, &ui::PlayerBar::lyricsClicked, this, [this] { showPage(5); });
    connect(m_playerBar, &ui::PlayerBar::playlistClicked, this, &MainWindow::openPlaybackQueue);

    connect(m_downloadPage, &ui::DownloadPage::backRequested, this, &MainWindow::navigateBack);
    connect(m_search, &ui::SearchPage::backRequested, this, &MainWindow::navigateBack);
    connect(m_songListPage, &ui::SongListPage::backRequested, this, &MainWindow::navigateBack);
    connect(m_playing, &ui::PlayingPage::backRequested, this, &MainWindow::navigateBack);
    connect(m_downloadPage, &ui::DownloadPage::cancelRequested, &m_downloads, &core::DownloadService::cancel);
    connect(m_downloadPage, &ui::DownloadPage::retryRequested, &m_downloads, &core::DownloadService::retry);
    connect(m_downloadPage, &ui::DownloadPage::deleteRequested, this, [this](qint64 songId) {
        const core::Song song = m_library.songById(songId);
        if (song.id > 0)
            handleSongDelete(song);
        refreshDownloadPage();
    });
    connect(&m_downloads, &core::DownloadService::tasksChanged, this, [this] {
        refreshDownloadPage();
        refreshDownloadVisualStates();
    });

    // ---------- 播放器信号 → UI ----------
    connect(&m_player, &core::PlayerService::songChanged, this, &MainWindow::onCurrentSongChanged);
    connect(&m_player, &core::PlayerService::playingChanged, m_playerBar, &ui::PlayerBar::setPlaying);
    connect(&m_player, &core::PlayerService::playingChanged, this, [this](bool playing) {
        for (ui::SongListView *view : findChildren<ui::SongListView *>())
            view->setPlaybackActive(playing);
    });
    connect(&m_player, &core::PlayerService::positionChanged, m_playerBar, &ui::PlayerBar::setPosition);
    connect(&m_player, &core::PlayerService::positionChanged, m_playing, &ui::PlayingPage::setPosition);
    connect(&m_player, &core::PlayerService::durationChanged, m_playerBar, &ui::PlayerBar::setDuration);
    connect(&m_player, &core::PlayerService::modeChanged, m_playerBar, &ui::PlayerBar::setMode);
    connect(&m_player, &core::PlayerService::volumeChanged, m_playerBar, &ui::PlayerBar::setVolume);
    connect(&m_player, &core::PlayerService::mutedChanged, m_playerBar, &ui::PlayerBar::setMuted);
    connect(&m_player, &core::PlayerService::errorOccurred, this, [this](const QString &message) {
        const core::Song s = m_player.currentSong();
        m_playerBar->setSong(s, m_playlists.isFavorite(s.id));
        m_playerBar->setPlaybackError(message);
    });

    // ---------- 推荐页 ----------
    connect(m_recommend, &ui::RecommendPage::playRequested, this, &MainWindow::playSongs);
    connect(m_recommend, &ui::RecommendPage::openPlaylistRequested, this,
            [this](int sourceId, const QString &remoteId, const QString &name) {
        openOnlinePlaylist(static_cast<core::SourceId>(sourceId), remoteId, name);
    });
    connect(m_recommend, &ui::RecommendPage::loginRequested, this, &MainWindow::openAccount);
    connect(m_recommend, &ui::RecommendPage::sourceActivationRequested, this, [this](int sourceId) {
        if (sourceId != int(core::SourceId::QqMusic))
            return;
        m_qqApiService.ensureRunning([this] {
            m_qqApiReady = true;
            m_recommend->setSourceAvailable(core::SourceId::QqMusic, true);
            m_search->setOnlineSourceEnabled(core::SourceId::QqMusic, true);
            m_qqClient.setBaseUrl(m_qqApiService.apiBase());
            restoreQqSession();
            m_recommend->setActiveSource(core::SourceId::QqMusic);
        }, [this](const QString &) {
            m_qqApiReady = false;
            m_recommend->setSourceAvailable(core::SourceId::QqMusic, false);
            m_search->setOnlineSourceEnabled(core::SourceId::QqMusic, false);
            refreshSourceAccessStates();
        });
    });
    connect(m_recommend, &ui::RecommendPage::refreshStateChanged, this,
            [this](bool busy, const QString &message) {
        if (!m_sidebarRefreshInProgress)
            return;
        if (busy)
            return;
        finishSidebarRefresh(message);
    });
    connect(m_recommend, &ui::RecommendPage::heartRequested, this, [this](int row) {
        const core::Song song = m_recommend->currentSongs().value(row);
        if (song.id > 0)
            m_playlists.setFavorite(song.id, !m_playlists.isFavorite(song.id));
    });
    connect(m_recommend, &ui::RecommendPage::addToPlaylistRequested,
            this, [this](int row, int playlistId) {
        const core::Song song = m_recommend->currentSongs().value(row);
        if (song.id > 0)
            addSongToPlaylist(song, playlistId);
    });

    // ---------- 收藏页 ----------
    connect(m_favorites, &ui::FavoritesPage::playRequested, this, &MainWindow::playSongs);
    connect(m_favorites, &ui::FavoritesPage::heartRequested, this, [this](int row) {
        handleBatchFavorite(m_favorites->memberSongsAt(row), false);
    });
    connect(m_favorites, &ui::FavoritesPage::addToPlaylistRequested, this, [this](int row, int plId) {
        handleBatchAddToPlaylist(m_favorites->memberSongsAt(row), plId);
    });
    connect(m_favorites, &ui::FavoritesPage::removeFromPlaylistRequested, this, [this](int row) {
        handleBatchFavorite(m_favorites->memberSongsAt(row), false);
    });

    // ---------- 本地歌单页 ----------
    connect(m_libraryPage, &ui::LibraryPage::playRequested, this, &MainWindow::playSongs);
    connect(m_libraryPage, &ui::LibraryPage::artistClicked, this, &MainWindow::openLocalArtist);
    connect(m_libraryPage, &ui::LibraryPage::albumClicked, this, &MainWindow::openLocalAlbum);
    connect(m_libraryPage, &ui::LibraryPage::heartRequested, this, [this](int row) {
        const core::Song s = m_libraryPage->currentSongs().value(row);
        if (s.id > 0)
            m_playlists.setFavorite(s.id, !m_playlists.isFavorite(s.id));
    });
    connect(m_libraryPage, &ui::LibraryPage::addToPlaylistRequested, this, [this](int row, int plId) {
        const core::Song s = m_libraryPage->currentSongs().value(row);
        if (s.id > 0)
            addSongToPlaylist(s, plId);
    });
    connect(m_libraryPage, &ui::LibraryPage::deleteFromLibraryRequested, this, [this](int row) {
        const core::Song s = m_libraryPage->currentSongs().value(row);
        if (s.id > 0)
            handleSongDelete(s);
    });
    connect(m_libraryPage, &ui::LibraryPage::importRequested, this, &MainWindow::addMusicFolder);
    connect(m_libraryPage, &ui::LibraryPage::importFilesRequested, this, &MainWindow::addMusicFiles);

    // ---------- 自建歌单页 ----------
    connect(m_selfPlaylists, &ui::SelfPlaylistsPage::openPlaylistRequested, this, &MainWindow::openPlaylist);
    connect(m_selfPlaylists, &ui::SelfPlaylistsPage::openCloudPlaylistRequested, this,
            [this](int sourceId, const QString &remoteId, const QString &name) {
        openOnlinePlaylist(static_cast<core::SourceId>(sourceId), remoteId, name, true);
    });
    connect(m_selfPlaylists, &ui::SelfPlaylistsPage::createPlaylistRequested, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("创建歌单"),
                                                   QStringLiteral("歌单名称:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            const int id = m_playlists.createPlaylist(name);
            if (id > 0) {
                m_playlistContext = -1;
                refreshSidebar();
                refreshAllPages();
                showPage(3);
            }
        }
    });

    // ---------- 歌单/详情页 ----------
    connect(m_songListPage, &ui::SongListPage::playAllRequested, this, [this](const QList<core::Song> &songs) {
        playSongs(songs, 0);
    });
    connect(m_songListPage, &ui::SongListPage::playRequested, this, &MainWindow::playSongs);
    connect(m_songListPage, &ui::SongListPage::heartRequested, this, [this](int row) {
        const QList<core::Song> members = m_songListPage->memberSongsAt(row);
        bool anyFavorite = false;
        for (const core::Song &song : members)
            anyFavorite = anyFavorite || m_playlists.isFavorite(song.id);
        handleBatchFavorite(members, !anyFavorite);
    });
    connect(m_songListPage, &ui::SongListPage::addToPlaylistRequested, this, [this](int row, int plId) {
        handleBatchAddToPlaylist(m_songListPage->memberSongsAt(row), plId);
    });
    connect(m_songListPage, &ui::SongListPage::removeFromPlaylistRequested, this, [this](int row) {
        if (m_playlistContext <= 0)
            return;
        QList<qint64> songIds;
        for (const core::Song &song : m_songListPage->memberSongsAt(row)) {
            if (song.id > 0)
                songIds.append(song.id);
        }
        if (!songIds.isEmpty())
            m_playlists.removeSongsBatch(m_playlistContext, songIds);
    });
    connect(m_songListPage, &ui::SongListPage::deleteFromLibraryRequested, this, [this](int row) {
        const core::Song s = m_songListPage->currentSongs().value(row);
        if (s.id > 0) {
            handleSongDelete(s);
            if (m_playlistContext > 0)
                openPlaylist(m_playlistContext);
        }
    });
    connect(m_songListPage, &ui::SongListPage::editPlaylistRequested, this, &MainWindow::openPlaylistEditor);
    connect(m_songListPage, &ui::SongListPage::renamePlaylistRequested, this, [this](int id) {
        QString oldName;
        for (const auto &p : m_playlists.playlists())
            if (p.id == id) { oldName = p.name; break; }
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("重命名歌单"),
                                                   QStringLiteral("新名称:"), QLineEdit::Normal, oldName, &ok);
        if (ok && !name.trimmed().isEmpty())
            m_playlists.renamePlaylist(id, name);
        refreshSidebar();
    });
    connect(m_songListPage, &ui::SongListPage::deletePlaylistRequested, this, [this](int id) {
        if (id == m_playlists.favoritePlaylistId())
            return;
        if (QMessageBox::question(this, QStringLiteral("删除歌单"), QStringLiteral("确定删除该歌单?"))
            == QMessageBox::Yes) {
            m_playlists.deletePlaylist(id);
            refreshSidebar();
            showPage(3);
        }
    });

    // ---------- 正在播放页 ----------
    connect(m_playing, &ui::PlayingPage::seekRequested, &m_player, &core::PlayerService::seek);
    connect(m_playing, &ui::PlayingPage::editLyricsRequested, this, [this] {
        const core::Song song = m_playing->currentSong();
        if (song.id <= 0)
            return;
        if (ui::LyricEditorDialog::editForSong(this, song))
            m_playing->setLyrics(core::LyricsLoader::load(song));
    });

    // ---------- 搜索页 ----------
    connect(m_search, &ui::SearchPage::playRequested, this, &MainWindow::playSongs);
    connect(m_search, &ui::SearchPage::artistClicked, this, &MainWindow::openArtist);
    connect(m_search, &ui::SearchPage::albumClicked, this, &MainWindow::openAlbum);
    connect(m_search, &ui::SearchPage::onlineResultActivated, this,
            [this](int sourceValue, int typeValue, const QString &remoteId,
                   const QString &title) {
        const auto sourceId = static_cast<core::SourceId>(sourceValue);
        const auto type = static_cast<core::SearchItemType>(typeValue);
        if (type == core::SearchItemType::Playlist)
            openOnlinePlaylist(sourceId, remoteId, title);
        else if (type == core::SearchItemType::Artist)
            openOnlineArtist(sourceId, remoteId, title);
        else if (type == core::SearchItemType::Album)
            openOnlineAlbum(sourceId, remoteId, title);
    });
    connect(m_search, &ui::SearchPage::heartRequested, this, [this](int row) {
        const core::Song s = materializeSongForAction(m_search->currentSongs().value(row));
        if (s.id > 0)
            m_playlists.setFavorite(s.id, !m_playlists.isFavorite(s.id));
    });
    connect(m_search, &ui::SearchPage::addToPlaylistRequested, this, [this](int row, int plId) {
        const core::Song s = materializeSongForAction(m_search->currentSongs().value(row));
        if (s.id > 0)
            addSongToPlaylist(s, plId);
    });
    connect(m_search, &ui::SearchPage::deleteFromLibraryRequested, this, [this](int row) {
        const core::Song s = m_search->currentSongs().value(row);
        if (s.id > 0)
            handleSongDelete(s);
    });

    // ---------- 音乐库信号 ----------
    connect(&m_library, &core::LibraryService::libraryChanged, this, [this] {
        m_libraryRefreshTimer.start();
    });
    connect(&m_library, &core::LibraryService::songCoverChanged, this, [this](qint64) {
        m_coverRefreshTimer.start();
    });

    // ---------- 播放列表信号 ----------
    connect(&m_playlists, &core::PlaylistController::playlistsChanged, this, [this] {
        refreshSidebar();
        refreshAllPages();
    });
    connect(&m_playlists, &core::PlaylistController::playlistSongsChanged, this, [this](int playlistId) {
        refreshSidebar();
        refreshAllPages();
        if (m_playlistContext == playlistId && m_stack->currentIndex() == 4)
            openPlaylist(m_playlistContext);
    });
    connect(&m_playlists, &core::PlaylistController::operationFailed, this, [this](const QString &message) {
        QMessageBox::warning(this, QStringLiteral("歌单操作失败"), message);
    });

    // ---------- 初始化 ----------
    if (m_library.openDatabase()) {
        m_playlists.setDatabase(m_library.database());
        loadCloudPlaylistCache();
        refreshSidebar();
        refreshLibraryViews();
        refreshAllPages();
        m_library.startScan();
    } else {
        QMessageBox::critical(this, QStringLiteral("数据库打开失败"),
                              QStringLiteral("音乐库无法安全打开：%1").arg(m_library.lastError()));
    }
    m_player.setVolume(core::SettingsService::volume());
    m_player.setMuted(core::SettingsService::muted());
    m_player.setMode(core::PlayerService::PlayMode(core::SettingsService::playMode()));
    m_playing->setLyricFontSize(core::SettingsService::lyricFontSize());
    m_sideBar->setActivePage(ui::SideBar::RecommendPage);
    setupShortcuts();
    disableHorizontalScrollbars(this);

    // 在线服务可能需要数秒才能自启动；先让推荐页立即展示磁盘缓存。
    // 服务就绪后 restoreOnlineSession() 会再次刷新为最新在线内容。
    m_recommend->refresh();
    m_apiService.ensureRunning(
        [this] {
            m_apiReady = true;
            m_recommend->setSourceAvailable(core::SourceId::Netease, true);
            m_search->setOnlineSourceEnabled(core::SourceId::Netease, true);
            m_apiClient.setBaseUrl(m_apiService.apiBase());
            restoreOnlineSession();
            // 先让首屏和数据库恢复后的本地列表稳定显示，再分批补充历史在线歌曲封面。
            QTimer::singleShot(3000, this, [this] {
                if (m_apiReady)
                    hydrateOnlineCovers(m_library.allSongs());
            });
        },
        [this](const QString &) {
            m_apiReady = false;
            m_recommend->setSourceAvailable(core::SourceId::Netease, false);
            m_search->setOnlineSourceEnabled(core::SourceId::Netease, false);
            refreshSourceAccessStates();
            m_recommend->refresh();
        });

    // QQ 服务不阻塞首屏；只有存在保存账号时才在首屏稳定后按需启动并验证。
    if (!core::SettingsService::qqCookie().isEmpty()
        || !core::SettingsService::qqUserId().isEmpty()) {
        QTimer::singleShot(2200, this, [this] {
            m_qqApiService.ensureRunning([this] {
                m_qqApiReady = true;
                m_recommend->setSourceAvailable(core::SourceId::QqMusic, true);
                m_search->setOnlineSourceEnabled(core::SourceId::QqMusic, true);
                m_qqClient.setBaseUrl(m_qqApiService.apiBase());
                restoreQqSession();
            }, [this](const QString &) {
                // 临时不可用时保留本地账号展示，不影响网易云和本地曲库。
                m_qqApiReady = false;
                m_recommend->setSourceAvailable(core::SourceId::QqMusic, false);
                m_search->setOnlineSourceEnabled(core::SourceId::QqMusic, false);
                refreshSourceAccessStates();
            });
        });
    }
}

void MainWindow::showPage(int pageId)
{
    if (pageId < 0 || pageId >= m_stack->count())
        return;
    const int currentPage = m_stack->currentIndex();
    if (!m_navigatingBack && currentPage >= 0 && currentPage != pageId)
        pushCurrentRoute();
    if (pageId >= 0 && pageId != 4)
        ++m_onlineDetailGeneration;
    const bool clearPlaylistSelection = pageId >= 0 && pageId != 4 && pageId != 5
        && pageId != 7 && m_playlistContext > 0;
    if (clearPlaylistSelection) {
        m_playlistContext = -1;
        refreshSidebar();
    }
    if (pageId >= 0 && pageId != 4 && !m_cloudPlaylistContext.isEmpty()) {
        m_cloudPlaylistContext.clear();
        refreshSidebar();
    }
    if (pageId == 5 || pageId == 7 || (pageId == 6 && m_stack->currentIndex() != 6))
        m_lastPage = m_stack->currentIndex();
    m_stack->setCurrentIndex(pageId);
    if (pageId == 1)
        m_favorites->setSongs(m_playlists.songsOf(m_playlists.favoritePlaylistId()), m_currentSongId);
    if (pageId == 3)
        refreshAllPages();
    if (pageId == 7)
        refreshDownloadPage();
    m_sideBar->setActivePage(pageId);
}

void MainWindow::navigateBack()
{
    if (m_navigationHistory.isEmpty())
        return;
    const RouteEntry destination = m_navigationHistory.takeLast();
    m_navigatingBack = true;
    m_playlistContext = destination.playlistContext;
    m_cloudPlaylistContext = destination.cloudPlaylistContext;
    if (destination.pageId == 4 && destination.hasSongListState) {
        m_songListPage->restoreNavigationState(destination.songListState);
        refreshSidebar();
    }
    showPage(destination.pageId);
    m_navigatingBack = false;
}

MainWindow::RouteEntry MainWindow::captureCurrentRoute() const
{
    RouteEntry route;
    route.pageId = m_stack ? m_stack->currentIndex() : -1;
    route.playlistContext = m_playlistContext;
    route.cloudPlaylistContext = m_cloudPlaylistContext;
    if (route.pageId == 4 && m_songListPage) {
        route.songListState = m_songListPage->navigationState();
        route.hasSongListState = true;
    }
    return route;
}

void MainWindow::pushCurrentRoute()
{
    const RouteEntry route = captureCurrentRoute();
    if (route.pageId < 0)
        return;
    if (!m_navigationHistory.isEmpty()
        && m_navigationHistory.constLast().pageId == route.pageId
        && route.pageId != 4) {
        return;
    }
    m_navigationHistory.append(route);
    if (m_navigationHistory.size() > 32)
        m_navigationHistory.removeFirst();
}

void MainWindow::prepareSongListNavigation()
{
    if (!m_navigatingBack && m_stack && m_stack->currentIndex() == 4)
        pushCurrentRoute();
}

void MainWindow::openPlaybackQueue()
{
    ++m_onlineDetailGeneration;
    const QList<core::Song> queue = m_player.playlist();
    if (queue.isEmpty())
        return;

    qint64 totalSec = 0;
    for (const auto &song : queue)
        totalSec += song.durationMs / 1000;
    const QString meta = QStringLiteral("%1 首 · 共 %2:%3")
        .arg(queue.size())
        .arg(totalSec / 60)
        .arg(totalSec % 60, 2, 10, QLatin1Char('0'));
    prepareSongListNavigation();
    m_playlistContext = -1;
    m_cloudPlaylistContext.clear();
    refreshSidebar();
    m_songListPage->showContent(queue, QStringLiteral("播放列表"), meta,
                                m_currentSongId, true);
    m_songListPage->setPlaybackQueueContext();
    showPage(4);
}

void MainWindow::openPlaylist(int playlistId)
{
    ++m_onlineDetailGeneration;
    QString name = QStringLiteral("歌单");
    QString desc;
    QString cover;
    bool found = false;
    for (const auto &p : m_playlists.playlists())
        if (p.id == playlistId) {
            found = true;
            name = p.name;
            desc = p.description;
            cover = p.coverPath;
            break;
        }
    if (!found) {
        m_playlistContext = -1;
        refreshSidebar();
        showPage(3);
        return;
    }
    prepareSongListNavigation();
    m_playlistContext = playlistId;
    m_cloudPlaylistContext.clear();
    refreshSidebar();
    const auto songs = m_playlists.songsOf(playlistId);
    const QList<core::SearchResultGroup> groups =
        core::SearchAggregator::aggregateSongsPreservingOrder(songs);
    qint64 totalSec = 0;
    for (const core::SearchResultGroup &group : groups) {
        if (!group.variants.isEmpty())
            totalSec += group.variants.constFirst().item.song.durationMs / 1000;
    }
    const QString meta = desc.isEmpty()
        ? QStringLiteral("%1 首 · 共 %2:%3").arg(groups.size()).arg(totalSec / 60).arg(totalSec % 60, 2, 10, QLatin1Char('0'))
        : QStringLiteral("%1 首 · %2").arg(groups.size()).arg(desc);
    m_songListPage->showContent(songs, name, meta, m_currentSongId,
                                playlistId > 0, cover, true);
    m_songListPage->setPlaylistContext(playlistId);
    showPage(4);
}

void MainWindow::openArtist(const QString &artist)
{
    ++m_onlineDetailGeneration;
    prepareSongListNavigation();
    m_cloudPlaylistContext.clear();
    refreshSidebar();
    QList<core::Song> songs;
    for (const auto &s : m_library.allSongs())
        if (s.artist == artist)
            songs.append(s);
    m_songListPage->showContent(songs, artist, QStringLiteral("歌手 · %1 首").arg(songs.size()), m_currentSongId, false);
    m_songListPage->setReadOnlyContext();
    showPage(4);
}

void MainWindow::openLocalArtist(const QString &artist)
{
    ++m_onlineDetailGeneration;
    prepareSongListNavigation();
    m_cloudPlaylistContext.clear();
    refreshSidebar();
    QList<core::Song> songs;
    for (const auto &s : m_library.allSongs())
        if (s.artist == artist && s.isLocallyAvailable())
            songs.append(s);
    m_songListPage->showContent(songs, artist, QStringLiteral("歌手 · %1 首").arg(songs.size()), m_currentSongId, false);
    m_songListPage->setReadOnlyContext();
    showPage(4);
}

void MainWindow::openAlbum(const QString &album, const QString &artist)
{
    ++m_onlineDetailGeneration;
    prepareSongListNavigation();
    m_cloudPlaylistContext.clear();
    refreshSidebar();
    QList<core::Song> songs;
    for (const auto &s : m_library.allSongs())
        if (s.album == album && (artist.isEmpty() || s.artist == artist)
            && (!s.isOnline() || s.isCached()))
            songs.append(s);
    m_songListPage->showContent(songs, album, QStringLiteral("专辑 · %1 首").arg(songs.size()), m_currentSongId, false);
    m_songListPage->setReadOnlyContext();
    showPage(4);
}

void MainWindow::openLocalAlbum(const QString &album, const QString &artist)
{
    ++m_onlineDetailGeneration;
    prepareSongListNavigation();
    m_cloudPlaylistContext.clear();
    refreshSidebar();
    QList<core::Song> songs;
    for (const auto &s : m_library.allSongs())
        if (s.album == album && (artist.isEmpty() || s.artist == artist)
            && s.isLocallyAvailable())
            songs.append(s);
    m_songListPage->showContent(songs, album, QStringLiteral("专辑 · %1 首").arg(songs.size()), m_currentSongId, false);
    m_songListPage->setReadOnlyContext();
    showPage(4);
}

void MainWindow::openOnlinePlaylist(core::SourceId sourceId, const QString &remoteId,
                                    const QString &name, bool cloudContext)
{
    core::MusicSource *source = m_sourceRegistry.source(sourceId);
    if (!source || remoteId.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("在线歌单"),
                                 QStringLiteral("对应音乐来源当前不可用"));
        return;
    }
    prepareSongListNavigation();
    m_playlistContext = -1;
    m_cloudPlaylistContext = cloudContext
        ? QStringLiteral("%1:%2").arg(int(sourceId)).arg(remoteId) : QString();
    refreshSidebar();
    const quint64 generation = ++m_onlineDetailGeneration;
    const QPointer<MainWindow> guard(this);
    auto showTracks = [guard, source, remoteId, name, generation](const QString &coverPath) {
        if (!guard || generation != guard->m_onlineDetailGeneration)
            return;
        source->playlistTracks(remoteId, [guard, source, name, coverPath, generation](const QJsonArray &arr) {
            if (!guard || generation != guard->m_onlineDetailGeneration)
                return;
            QList<core::Song> songs;
            for (const QJsonValue &v : arr) {
                core::Song s = source->songFromJson(v.toObject());
                s.id = guard->m_library.upsertOnlineSong(s);
                if (s.id > 0) {
                    const core::Song stored = guard->m_library.songById(s.id);
                    s.coverPath = stored.coverPath;
                    s.cachePath = stored.cachePath;
                    s.downloadPath = stored.downloadPath;
                    s.lyricPath = stored.lyricPath;
                }
                songs.append(s);
            }
            guard->ensureOnlineCovers(songs);
            int local = 0, online = 0;
            for (const auto &s : songs)
                s.isOnline() ? ++online : ++local;
            const QString meta = QStringLiteral("本地 %1 首 · 在线 %2 首").arg(local).arg(online);
            guard->m_songListPage->showContent(songs, name, meta, guard->m_currentSongId,
                                               false, coverPath);
            guard->m_songListPage->setReadOnlyContext();
            guard->showPage(4);
        }, [guard, generation](const QString &msg) {
            if (guard && generation == guard->m_onlineDetailGeneration) {
                QMessageBox::information(guard.data(), QStringLiteral("在线歌单"),
                                         QStringLiteral("加载失败:%1").arg(msg));
            }
        });
    };

    source->playlistDetail(remoteId, [guard, sourceId, remoteId, source, showTracks, generation](const QJsonObject &playlist) {
        if (!guard || generation != guard->m_onlineDetailGeneration)
            return;
        QString coverUrl = playlist.value(QStringLiteral("coverImgUrl")).toString();
        if (coverUrl.isEmpty())
            coverUrl = playlist.value(QStringLiteral("picUrl")).toString();
        if (coverUrl.isEmpty())
            coverUrl = playlist.value(QStringLiteral("coverUrl")).toString();
        const QString coverPath = guard->m_library.playlistCoverCachePath(sourceId, remoteId);
        if (QFileInfo::exists(coverPath) && QFileInfo(coverPath).size() > 0) {
            showTracks(coverPath);
            return;
        }
        if (!coverUrl.isEmpty()) {
            source->downloadToFile(QUrl(coverUrl), coverPath, [showTracks, coverPath](bool ok) {
                showTracks(ok ? coverPath : QString());
            });
        } else {
            showTracks(QString());
        }
    }, [showTracks](const QString &) {
        showTracks(QString());
    });
}

void MainWindow::openOnlineArtist(core::SourceId sourceId, const QString &remoteId,
                                  const QString &name)
{
    core::MusicSource *source = m_sourceRegistry.source(sourceId);
    if (!source || remoteId.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("在线歌手"),
                                 QStringLiteral("对应音乐来源当前不可用"));
        return;
    }
    prepareSongListNavigation();
    m_playlistContext = -1;
    m_cloudPlaylistContext.clear();
    refreshSidebar();
    const quint64 generation = ++m_onlineDetailGeneration;
    const QPointer<MainWindow> guard(this);
    source->artistSongs(remoteId, [guard, source, sourceId, name, generation](const QJsonArray &array) {
        if (!guard || generation != guard->m_onlineDetailGeneration)
            return;
        QList<core::Song> songs;
        songs.reserve(array.size());
        for (const QJsonValue &value : array) {
            core::Song song = source->songFromJson(value.toObject());
            if (song.hasRemoteIdentity())
                songs.append(song);
        }
        guard->m_songListPage->showContent(
            songs, name,
            QStringLiteral("%1歌手 · %2 首").arg(sourceId == core::SourceId::Netease
                                                    ? QStringLiteral("网易云")
                                                    : QStringLiteral("QQ音乐"))
                .arg(songs.size()),
            guard->m_currentSongId, false);
        guard->m_songListPage->setReadOnlyContext();
        guard->showPage(4);
    }, [guard, generation](const QString &message) {
        if (guard && generation == guard->m_onlineDetailGeneration) {
            QMessageBox::information(guard.data(), QStringLiteral("在线歌手"),
                                     QStringLiteral("加载失败：%1").arg(message));
        }
    });
}

void MainWindow::openOnlineAlbum(core::SourceId sourceId, const QString &remoteId,
                                 const QString &name)
{
    core::MusicSource *source = m_sourceRegistry.source(sourceId);
    if (!source || remoteId.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("在线专辑"),
                                 QStringLiteral("对应音乐来源当前不可用"));
        return;
    }
    prepareSongListNavigation();
    m_playlistContext = -1;
    m_cloudPlaylistContext.clear();
    refreshSidebar();
    const quint64 generation = ++m_onlineDetailGeneration;
    const QPointer<MainWindow> guard(this);
    source->albumDetail(remoteId, [guard, source, sourceId, name, generation](const QJsonObject &object) {
        if (!guard || generation != guard->m_onlineDetailGeneration)
            return;
        const QJsonArray array = object.value(QStringLiteral("songs")).toArray();
        QList<core::Song> songs;
        songs.reserve(array.size());
        for (const QJsonValue &value : array) {
            core::Song song = source->songFromJson(value.toObject());
            if (song.hasRemoteIdentity())
                songs.append(song);
        }
        guard->m_songListPage->showContent(
            songs, name,
            QStringLiteral("%1专辑 · %2 首").arg(sourceId == core::SourceId::Netease
                                                    ? QStringLiteral("网易云")
                                                    : QStringLiteral("QQ音乐"))
                .arg(songs.size()),
            guard->m_currentSongId, false);
        guard->m_songListPage->setReadOnlyContext();
        guard->showPage(4);
    }, [guard, generation](const QString &message) {
        if (guard && generation == guard->m_onlineDetailGeneration) {
            QMessageBox::information(guard.data(), QStringLiteral("在线专辑"),
                                     QStringLiteral("加载失败：%1").arg(message));
        }
    });
}

void MainWindow::ensureOnlineCovers(const QList<core::Song> &songs)
{
    for (const core::Song &s : songs) {
        if (!s.isOnline() || s.coverUrl.isEmpty() || s.id <= 0 || m_onlineCoverAttempted.contains(s.id))
            continue;
        const core::Song current = m_library.songById(s.id);
        if (!current.coverPath.isEmpty() && QFileInfo::exists(current.coverPath))
            continue;
        const QString path = m_library.songCoverCachePath(s);
        if (path.isEmpty())
            continue;
        m_onlineCoverAttempted.insert(s.id);
        if (QFileInfo::exists(path)) {
            m_library.setSongCoverPath(s.id, path);
            continue;
        }
        m_onlineCoverQueue.append(s);
    }
    startOnlineCoverDownloads();
}

void MainWindow::startOnlineCoverDownloads()
{
    while (m_onlineCoverDownloadsActive < kOnlineCoverDownloadsBatch
           && !m_onlineCoverQueue.isEmpty()) {
        const core::Song song = m_onlineCoverQueue.takeFirst();
        const QString path = m_library.songCoverCachePath(song);
        if (path.isEmpty())
            continue;
        if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
            m_library.setSongCoverPath(song.id, path);
            continue;
        }
        core::MusicSource *source = m_sourceRegistry.sourceFor(song);
        if (!source) {
            m_onlineCoverAttempted.remove(song.id);
            continue;
        }
        ++m_onlineCoverDownloadsActive;
        const quint64 generation = m_onlineCoverGeneration;
        source->downloadToFile(QUrl(song.coverUrl), path,
                               [this, id = song.id, path, generation](bool ok) {
            if (ok && generation == m_onlineCoverGeneration)
                m_library.setSongCoverPath(id, path);
            else if (ok)
                QFile::remove(path);
            m_onlineCoverDownloadsActive = qMax(0, m_onlineCoverDownloadsActive - 1);
            startOnlineCoverDownloads();
        });
    }
}

void MainWindow::hydrateOnlineCovers(const QList<core::Song> &songs)
{
    if (!m_apiReady || m_onlineCoverDetailsInFlight)
        return;
    QList<core::Song> withCoverUrls;
    QList<qint64> ids;
    QList<qint64> localIds;
    for (const core::Song &song : songs) {
        if (!song.isOnline() || song.id <= 0 || song.onlineId <= 0)
            continue;
        const core::Song stored = m_library.songById(song.id);
        if (!stored.coverPath.isEmpty() && QFileInfo::exists(stored.coverPath))
            continue;
        if (!song.coverUrl.isEmpty()) {
            withCoverUrls.append(song);
        } else if (!m_onlineCoverDetailsAttempted.contains(song.id)
                   && ids.size() < kOnlineCoverDetailsBatch) {
            m_onlineCoverDetailsAttempted.insert(song.id);
            ids.append(song.onlineId);
            localIds.append(song.id);
        }
    }
    ensureOnlineCovers(withCoverUrls);
    if (ids.isEmpty())
        return;
    m_onlineCoverDetailsInFlight = true;
    const quint64 coverGeneration = m_onlineCoverGeneration;
    m_apiClient.songDetails(ids, [this, coverGeneration](const QJsonArray &arr) {
        m_onlineCoverDetailsInFlight = false;
        if (coverGeneration != m_onlineCoverGeneration)
            return;
        QList<core::Song> enriched;
        for (const QJsonValue &value : arr) {
            core::Song detail = m_apiClient.songFromJson(value.toObject());
            const core::Song stored = m_library.songByOnlineId(detail.source, detail.onlineId);
            if (stored.id <= 0 || detail.coverUrl.isEmpty())
                continue;
            detail.id = stored.id;
            detail.coverPath = stored.coverPath;
            detail.cachePath = stored.cachePath;
            detail.downloadPath = stored.downloadPath;
            detail.lyricPath = stored.lyricPath;
            m_library.upsertOnlineSong(detail);
            enriched.append(detail);
        }
        ensureOnlineCovers(enriched);
        QTimer::singleShot(800, this, [this, coverGeneration] {
            if (coverGeneration == m_onlineCoverGeneration)
                hydrateOnlineCovers(m_library.allSongs());
        });
    }, [this, localIds, coverGeneration](const QString &) {
        m_onlineCoverDetailsInFlight = false;
        if (coverGeneration == m_onlineCoverGeneration) {
            for (qint64 id : localIds)
                m_onlineCoverDetailsAttempted.remove(id);
        }
    });
    connect(m_songListPage, &ui::SongListPage::savePlaybackQueueRequested, this, [this] {
        const QList<core::Song> queue = m_player.playlist();
        if (queue.isEmpty())
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("保存播放列表"), QStringLiteral("新歌单名称:"),
            QLineEdit::Normal, QStringLiteral("播放列表"), &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        const int playlistId = m_playlists.createPlaylist(name);
        if (playlistId <= 0)
            return;
        QList<qint64> songIds;
        for (const core::Song &queueSong : queue) {
            const core::Song song = materializeSongForAction(queueSong);
            if (song.id > 0)
                songIds.append(song.id);
        }
        const core::PlaylistController::BatchResult result =
            m_playlists.addSongsBatch(playlistId, songIds);
        if (!result.success) {
            m_playlists.deletePlaylist(playlistId);
            return;
        }
        refreshSidebar();
        refreshAllPages();
        openPlaylist(playlistId);
    });
    connect(m_songListPage, &ui::SongListPage::clearPlaybackQueueRequested, this, [this] {
        if (m_player.playlist().isEmpty())
            return;
        if (QMessageBox::question(this, QStringLiteral("清空播放列表"),
                                  QStringLiteral("确定清空当前播放列表？"))
            != QMessageBox::Yes) {
            return;
        }
        m_player.clearPlaylist();
        m_currentSongId = -1;
        const int destination = (m_lastPage >= 0 && m_lastPage < m_stack->count()
                                 && m_lastPage != 4) ? m_lastPage : 0;
        showPage(destination);
    });
    connect(m_songListPage, &ui::SongListPage::removeFromPlaybackQueueRequested,
            this, [this](int row) {
        if (!m_player.removeAt(row))
            return;
        if (m_player.playlist().isEmpty()) {
            m_currentSongId = -1;
            const int destination = (m_lastPage >= 0 && m_lastPage < m_stack->count()
                                     && m_lastPage != 4) ? m_lastPage : 0;
            showPage(destination);
        } else {
            openPlaybackQueue();
        }
    });
}

void MainWindow::openAccount()
{
    ui::AccountDialog dlg(&m_apiClient, &m_qqClient, &m_qqApiService, this);
    connect(&dlg, &ui::AccountDialog::accountStateChanged, this, [this] {
        cacheQqAvatar(QString());
        m_accountPanel->refresh();
        m_recommend->refresh();
        restoreOnlineSession();
        restoreQqSession();
        refreshSourceAccessStates();
    });
    dlg.exec();
}

void MainWindow::restoreQqSession()
{
    const QString credential = core::SettingsService::qqCookie();
    if (credential.isEmpty()) {
        m_qqSessionVerifying = false;
        m_qqClient.setCookie(QString());
        removeCloudPlaylists(core::SourceId::QqMusic);
        m_accountPanel->refresh();
        refreshSourceAccessStates();
        return;
    }
    m_qqClient.setCookie(credential);
    m_qqSessionVerifying = true;
    refreshSourceAccessStates();
    m_qqClient.validateCredential(credential, QStringLiteral("saved"),
        [this](const QJsonObject &profile) {
            m_qqSessionVerifying = false;
            const QString userId = profile.value(QStringLiteral("userId")).toVariant().toString();
            if (userId.isEmpty()) {
                refreshSourceAccessStates();
                return;
            }
            core::SettingsService::setQqUserId(userId);
            core::SettingsService::setQqNickname(profile.value(QStringLiteral("nickname")).toString());
            const QString avatarUrl = profile.value(QStringLiteral("avatarUrl")).toString();
            core::SettingsService::setQqAvatarRemoteUrl(avatarUrl);
            cacheQqAvatar(avatarUrl);
            refreshCloudPlaylists(core::SourceId::QqMusic, userId);
            m_qqClient.vipStatus([this](const QJsonObject &status) {
                if (status.value(QStringLiteral("recognized")).toBool())
                    core::SettingsService::setQqVipStatus(
                        status.value(QStringLiteral("active")).toBool() ? 1 : 0);
                else
                    core::SettingsService::setQqVipStatus(-1);
                m_accountPanel->refresh();
            }, [this](const QString &) {
                core::SettingsService::setQqVipStatus(-1);
                m_accountPanel->refresh();
            });
            m_accountPanel->refresh();
            refreshSourceAccessStates();
        }, [this](const QString &) {
            // 网络或上游错误不视为凭据过期；保留本地账号，等待下次验证。
            m_qqSessionVerifying = false;
            m_accountPanel->refresh();
            refreshSourceAccessStates();
        });
}

void MainWindow::cacheQqAvatar(const QString &remoteUrl)
{
    const QString url = remoteUrl.isEmpty() ? core::SettingsService::qqAvatarRemoteUrl() : remoteUrl;
    if (url.isEmpty())
        return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/accounts");
    QDir().mkpath(dir);
    const QString avatarKey = QString::fromLatin1(
        QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex());
    const QString path = dir + QStringLiteral("/qq-avatar-%1.png").arg(avatarKey);
    if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
        core::SettingsService::setQqAvatarUrl(path);
        m_accountPanel->refresh();
        return;
    }
    m_qqClient.downloadToFile(QUrl(url), path, [this, path](bool ok) {
        if (!ok)
            return;
        core::SettingsService::setQqAvatarUrl(path);
        m_accountPanel->refresh();
    });
}

void MainWindow::openSettings()
{
    ui::SettingsDialog dlg(&m_apiService, &m_apiClient, &m_library, this);
    QSet<qint64> protectedSongIds;
    for (const core::Song &song : m_player.playlist()) {
        if (song.id > 0)
            protectedSongIds.insert(song.id);
    }
    dlg.setProtectedCacheSongIds(protectedSongIds);
    connect(&dlg, &ui::SettingsDialog::rescanRequested, this, [&dlg, this] {
        core::SettingsService::setMusicFolders(dlg.folders());
        m_library.startScan();
    });
    connect(&dlg, &ui::SettingsDialog::databaseReloadRequested, this, [this] {
        m_library.reloadDatabase();
        m_playlists.reload();
        if (m_playlistContext > 0)
            openPlaylist(m_playlistContext);
        else
            refreshAllPages();
    });
    connect(&dlg, &ui::SettingsDialog::cacheCleared,
            this, &MainWindow::resetManagedCacheViews);
    if (dlg.exec() == QDialog::Accepted) {
        m_apiClient.setBaseUrl(core::SettingsService::onlineApiBase());
        const QStringList folders = dlg.folders();
        if (folders != core::SettingsService::musicFolders())
            m_library.setFolders(folders);
        core::SettingsService::setLyricFontSize(dlg.lyricFontSize());
        m_playing->setLyricFontSize(dlg.lyricFontSize());
    }
}

void MainWindow::openPlaylistEditor(int playlistId)
{
    QString name, desc, cover;
    for (const auto &p : m_playlists.playlists())
        if (p.id == playlistId) { name = p.name; desc = p.description; cover = p.coverPath; break; }
    ui::PlaylistEditDialog dlg(&m_playlists, playlistId, name, desc, cover, this);
    if (dlg.exec() == QDialog::Accepted) {
        refreshSidebar();
        refreshAllPages();
        openPlaylist(playlistId);
    }
}

void MainWindow::playSongs(const QList<core::Song> &songs, int index)
{
    if (songs.isEmpty())
        return;
    // PlayerService 只在歌曲真正成为当前曲目时写入在线记录；队列里尚未
    // 播放的搜索结果仍保持临时身份，避免一次点击把整页浏览结果写入曲库。
    m_player.setPlaylist(songs, index);
    m_player.play();
}

core::Song MainWindow::materializeSongForAction(const core::Song &song)
{
    if (!song.isOnline() || song.id > 0 || !song.hasRemoteIdentity())
        return song;
    core::Song result = song;
    result.id = m_library.upsertOnlineSong(result);
    if (result.id <= 0)
        return result;
    const core::Song stored = m_library.songById(result.id);
    return stored.id > 0 ? stored : result;
}

void MainWindow::addSongToPlaylist(const core::Song &song, int playlistId)
{
    if (song.id <= 0 || playlistId <= 0)
        return;
    if (!m_playlists.addSong(playlistId, song.id))
        return;
    QString playlistName;
    for (const auto &playlist : m_playlists.playlists()) {
        if (playlist.id == playlistId) {
            playlistName = playlist.name;
            break;
        }
    }
    const QString message = playlistName.isEmpty()
        ? QStringLiteral("已添加到歌单")
        : QStringLiteral("已添加到「%1」").arg(playlistName);
    QToolTip::showText(QCursor::pos(), message, this, QRect(), 1800);
}

QList<ui::SideBar::PlaylistItem> MainWindow::selfPlaylistInfos() const
{
    QList<ui::SideBar::PlaylistItem> items;
    for (const auto &p : m_playlists.playlists()) {
        if (p.id == m_playlists.favoritePlaylistId())
            continue;
        ui::SideBar::PlaylistItem item;
        item.id = p.id;
        item.name = p.name;
        item.coverPath = p.coverPath;
        item.description = p.description;
        items.append(item);
    }
    for (const core::OnlinePlaylist &playlist : m_cloudPlaylists) {
        ui::SideBar::PlaylistItem item;
        item.name = playlist.name;
        item.coverPath = playlist.coverPath;
        item.description = playlist.description;
        item.cloud = true;
        item.source = playlist.source;
        item.remoteId = playlist.remoteId;
        items.append(item);
    }
    return items;
}

void MainWindow::loadCloudPlaylistCache()
{
    QFile file(core::SettingsService::cloudPlaylistCachePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return;

    const bool neteaseAllowed = core::SettingsService::onlineUid() > 0
        && !core::SettingsService::onlineCookie().isEmpty();
    const bool qqAllowed = !core::SettingsService::qqUserId().isEmpty()
        && !core::SettingsService::qqCookie().isEmpty();
    QSet<QString> identities;
    for (const QJsonValue &value : document.object()
             .value(QStringLiteral("playlists")).toArray()) {
        core::OnlinePlaylist playlist = cloudPlaylistFromJson(value.toObject());
        if (!playlist.isValid() || identities.contains(playlist.stableIdentity()))
            continue;
        if ((playlist.source == core::SourceId::Netease && !neteaseAllowed)
            || (playlist.source == core::SourceId::QqMusic && !qqAllowed)) {
            continue;
        }
        playlist.coverPath = m_library.playlistCoverCachePath(
            playlist.source, playlist.remoteId);
        if (!QFileInfo::exists(playlist.coverPath)
            || QFileInfo(playlist.coverPath).size() <= 0) {
            playlist.coverPath.clear();
        }
        identities.insert(playlist.stableIdentity());
        m_cloudPlaylists.append(playlist);
    }
}

void MainWindow::saveCloudPlaylistCache() const
{
    const QString path = core::SettingsService::cloudPlaylistCachePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return;
    QJsonArray array;
    for (const core::OnlinePlaylist &playlist : m_cloudPlaylists)
        array.append(cloudPlaylistToJson(playlist));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(QJsonObject{
        { QStringLiteral("version"), 1 },
        { QStringLiteral("playlists"), array },
    }).toJson(QJsonDocument::Compact));
    file.commit();
}

void MainWindow::refreshCloudPlaylists(core::SourceId sourceId, const QString &userId)
{
    core::MusicSource *source = m_sourceRegistry.source(sourceId);
    const QString normalizedUserId = userId.trimmed();
    if (!source || normalizedUserId.isEmpty())
        return;
    const int sourceKey = int(sourceId);
    const quint64 generation = m_cloudPlaylistGenerations.value(sourceKey) + 1;
    m_cloudPlaylistGenerations.insert(sourceKey, generation);
    const QPointer<MainWindow> guard(this);
    source->userPlaylistItems(normalizedUserId,
        [guard, sourceId, sourceKey, generation](const QList<core::OnlinePlaylist> &items) {
            if (!guard || guard->m_cloudPlaylistGenerations.value(sourceKey) != generation)
                return;
            guard->replaceCloudPlaylists(sourceId, items);
        }, [guard, sourceKey, generation](const QString &) {
            // 网络失败时保留该来源的磁盘缓存；只忽略本次刷新结果。
            if (!guard || guard->m_cloudPlaylistGenerations.value(sourceKey) != generation)
                return;
        });
}

void MainWindow::removeCloudPlaylists(core::SourceId sourceId)
{
    const int sourceKey = int(sourceId);
    m_cloudPlaylistGenerations.insert(
        sourceKey, m_cloudPlaylistGenerations.value(sourceKey) + 1);
    bool changed = false;
    for (int i = m_cloudPlaylists.size() - 1; i >= 0; --i) {
        if (m_cloudPlaylists.at(i).source == sourceId) {
            m_cloudPlaylists.removeAt(i);
            changed = true;
        }
    }
    for (int i = m_cloudPlaylistCoverQueue.size() - 1; i >= 0; --i) {
        if (m_cloudPlaylistCoverQueue.at(i).source == sourceId) {
            m_cloudPlaylistCoverQueued.remove(
                m_cloudPlaylistCoverQueue.at(i).stableIdentity());
            m_cloudPlaylistCoverQueue.removeAt(i);
        }
    }
    const QString prefix = QStringLiteral("%1:").arg(sourceKey);
    if (m_cloudPlaylistContext.startsWith(prefix)) {
        m_cloudPlaylistContext.clear();
        changed = true;
    }
    if (!changed)
        return;
    saveCloudPlaylistCache();
    refreshSidebar();
    refreshAllPages();
}

void MainWindow::replaceCloudPlaylists(core::SourceId sourceId,
                                       const QList<core::OnlinePlaylist> &playlists)
{
    QList<core::OnlinePlaylist> normalized;
    QSet<QString> identities;
    for (core::OnlinePlaylist playlist : playlists) {
        playlist.source = sourceId;
        playlist.remoteId = playlist.remoteId.trimmed();
        playlist.name = playlist.name.trimmed();
        if (!playlist.isValid() || identities.contains(playlist.stableIdentity()))
            continue;
        if (playlist.name.isEmpty())
            playlist.name = QStringLiteral("未命名歌单");
        const QString coverPath = m_library.playlistCoverCachePath(
            playlist.source, playlist.remoteId);
        if (QFileInfo::exists(coverPath) && QFileInfo(coverPath).size() > 0)
            playlist.coverPath = coverPath;
        identities.insert(playlist.stableIdentity());
        normalized.append(playlist);
    }

    for (int i = m_cloudPlaylists.size() - 1; i >= 0; --i) {
        if (m_cloudPlaylists.at(i).source == sourceId)
            m_cloudPlaylists.removeAt(i);
    }
    m_cloudPlaylists.append(normalized);
    std::stable_sort(m_cloudPlaylists.begin(), m_cloudPlaylists.end(),
                     [](const core::OnlinePlaylist &left,
                        const core::OnlinePlaylist &right) {
        return int(left.source) < int(right.source);
    });
    saveCloudPlaylistCache();
    refreshSidebar();
    refreshAllPages();
    queueCloudPlaylistCovers(normalized);
}

void MainWindow::queueCloudPlaylistCovers(const QList<core::OnlinePlaylist> &playlists)
{
    for (const core::OnlinePlaylist &playlist : playlists) {
        if (playlist.coverUrl.isEmpty() || !playlist.coverPath.isEmpty()
            || m_cloudPlaylistCoverQueued.contains(playlist.stableIdentity())) {
            continue;
        }
        m_cloudPlaylistCoverQueued.insert(playlist.stableIdentity());
        m_cloudPlaylistCoverQueue.append(playlist);
    }
    startCloudPlaylistCoverDownloads();
}

void MainWindow::startCloudPlaylistCoverDownloads()
{
    constexpr int kMaxCloudCoverDownloads = 2;
    while (m_cloudPlaylistCoverDownloadsActive < kMaxCloudCoverDownloads
           && !m_cloudPlaylistCoverQueue.isEmpty()) {
        const core::OnlinePlaylist playlist = m_cloudPlaylistCoverQueue.takeFirst();
        core::MusicSource *source = m_sourceRegistry.source(playlist.source);
        const QString path = m_library.playlistCoverCachePath(
            playlist.source, playlist.remoteId);
        if (!source || path.isEmpty()) {
            m_cloudPlaylistCoverQueued.remove(playlist.stableIdentity());
            continue;
        }
        ++m_cloudPlaylistCoverDownloadsActive;
        const int sourceKey = int(playlist.source);
        const quint64 generation = m_cloudPlaylistGenerations.value(sourceKey);
        source->downloadToFile(QUrl(playlist.coverUrl), path,
            [this, playlist, path, sourceKey, generation](bool ok) {
                --m_cloudPlaylistCoverDownloadsActive;
                m_cloudPlaylistCoverQueued.remove(playlist.stableIdentity());
                if (ok && m_cloudPlaylistGenerations.value(sourceKey) == generation) {
                    for (core::OnlinePlaylist &stored : m_cloudPlaylists) {
                        if (stored.stableIdentity() == playlist.stableIdentity()) {
                            stored.coverPath = path;
                            break;
                        }
                    }
                    saveCloudPlaylistCache();
                    refreshSidebar();
                    refreshAllPages();
                }
                startCloudPlaylistCoverDownloads();
            });
    }
}

void MainWindow::refreshSidebar()
{
    QList<ui::SideBar::PlaylistItem> items = selfPlaylistInfos();
    m_sideBar->setPlaylists(items, m_playlistContext, m_cloudPlaylistContext);
    QList<QPair<int, QString>> menuItems;
    for (const auto &p : m_playlists.playlists())
        menuItems.append({ p.id, p.name });
    m_recommend->setPlaylistMenuItems(menuItems);
    m_favorites->setPlaylistMenuItems(menuItems);
    m_libraryPage->setPlaylistMenuItems(menuItems);
    m_search->setPlaylistMenuItems(menuItems);
    m_songListPage->setPlaylistMenuItems(menuItems);
}

void MainWindow::refreshLibraryViews()
{
    const auto all = m_library.allSongs();
    m_libraryPage->setSongs(all, m_currentSongId);
    m_search->setLocalSongs(all);
    if (!m_searchQuery.isEmpty())
        m_search->refreshLocalResults();
    m_search->refreshOnlineCovers();
}

void MainWindow::refreshAllPages()
{
    m_favorites->setSongs(m_playlists.songsOf(m_playlists.favoritePlaylistId()), m_currentSongId);
    m_selfPlaylists->setPlaylists(m_playlists.playlists());
    m_selfPlaylists->setCloudPlaylists(m_cloudPlaylists);
    m_songListPage->refreshCovers(&m_library);
    m_songListPage->setPlayingId(m_currentSongId);
    refreshSongListStates();
}

void MainWindow::refreshFromSidebar()
{
    if (m_sidebarRefreshInProgress || !m_sidebarRefreshButton)
        return;
    m_sidebarRefreshInProgress = true;
    m_sidebarRefreshButton->setEnabled(false);
    m_sidebarRefreshButton->setToolTip(QStringLiteral("正在刷新…"));

    m_library.reloadDatabase();
    m_playlists.reload();
    refreshSidebar();
    refreshLibraryViews();
    refreshAllPages();

    const int currentPage = m_stack ? m_stack->currentIndex() : 0;
    if (currentPage == 4 && m_playlistContext > 0) {
        openPlaylist(m_playlistContext);
    } else if (currentPage == 5 && m_currentSongId > 0) {
        const core::Song song = m_library.songById(m_currentSongId);
        if (song.id > 0) {
            m_playerBar->setSong(song, m_playlists.isFavorite(song.id));
            m_playing->setSong(song, QPixmap());
            m_playing->loadLyricsFor(song);
        }
    } else if (currentPage == 6) {
        const QString query = m_searchQuery.trimmed();
        if (query.isEmpty())
            m_search->showSearchAssistant();
        else
            m_search->performSearch(query);
    } else if (currentPage == 7) {
        refreshDownloadPage();
    }

    const auto refreshRecommendations = [this] {
        if (!m_sidebarRefreshInProgress)
            return;
        m_recommend->refresh(true);
    };
    if (m_recommend->activeSourceId() == core::SourceId::QqMusic) {
        m_qqApiService.ensureRunning([this, refreshRecommendations] {
            m_qqApiReady = true;
            m_recommend->setSourceAvailable(core::SourceId::QqMusic, true);
            m_search->setOnlineSourceEnabled(core::SourceId::QqMusic, true);
            m_qqClient.setBaseUrl(m_qqApiService.apiBase());
            refreshSourceAccessStates();
            refreshRecommendations();
        }, [this](const QString &message) {
            m_qqApiReady = false;
            m_recommend->setSourceAvailable(core::SourceId::QqMusic, false);
            m_search->setOnlineSourceEnabled(core::SourceId::QqMusic, false);
            refreshSourceAccessStates();
            finishSidebarRefresh(QStringLiteral("QQ 音乐刷新失败：%1").arg(message));
        });
    } else {
        m_apiService.ensureRunning([this, refreshRecommendations] {
            m_apiReady = true;
            m_recommend->setSourceAvailable(core::SourceId::Netease, true);
            m_search->setOnlineSourceEnabled(core::SourceId::Netease, true);
            m_apiClient.setBaseUrl(m_apiService.apiBase());
            refreshSourceAccessStates();
            refreshRecommendations();
        }, [this](const QString &message) {
            m_apiReady = false;
            m_recommend->setSourceAvailable(core::SourceId::Netease, false);
            m_search->setOnlineSourceEnabled(core::SourceId::Netease, false);
            refreshSourceAccessStates();
            finishSidebarRefresh(QStringLiteral("网易云刷新失败：%1").arg(message));
        });
    }
}

void MainWindow::finishSidebarRefresh(const QString &message)
{
    if (!m_sidebarRefreshInProgress)
        return;
    m_sidebarRefreshInProgress = false;
    if (!m_sidebarRefreshButton)
        return;
    m_sidebarRefreshButton->setEnabled(true);
    m_sidebarRefreshButton->setToolTip(QStringLiteral("刷新当前页面和推荐内容"));
    const QString feedback = message.isEmpty() ? QStringLiteral("刷新完成") : message;
    QToolTip::showText(m_sidebarRefreshButton->mapToGlobal(
                           QPoint(m_sidebarRefreshButton->width() / 2, 0)),
                       feedback, m_sidebarRefreshButton);
}

void MainWindow::resetManagedCacheViews()
{
    ++m_onlineCoverGeneration;
    m_onlineCoverQueue.clear();
    m_onlineCoverAttempted.clear();
    m_onlineCoverDetailsAttempted.clear();
    ui::CoverProvider::clearCache();
    m_recommend->resetAfterCacheClear();
    m_search->resetAfterCacheClear();
    m_playlists.reload();
    refreshSidebar();
    refreshLibraryViews();
    refreshAllPages();
    if (m_currentSongId > 0) {
        const core::Song current = m_library.songById(m_currentSongId);
        if (current.id > 0) {
            m_playerBar->setSong(current, m_playlists.isFavorite(current.id));
            m_playing->setSong(current, QPixmap());
        }
    }
}

void MainWindow::restoreOnlineSession()
{
    const qint64 savedUid = core::SettingsService::onlineUid();
    const QString cookie = core::SettingsService::onlineCookie();
    if (savedUid <= 0 || cookie.isEmpty()) {
        m_neteaseSessionVerifying = false;
        // uid 与 cookie 必须成对有效，不能只凭本地 uid 显示“已登录”。
        if (savedUid > 0 || !cookie.isEmpty()) {
            core::SettingsService::setOnlineCookie(QString());
            core::SettingsService::setOnlineUid(0);
            core::SettingsService::setOnlineNickname(QString());
            core::SettingsService::setOnlineAvatarUrl(QString());
            m_apiClient.setCookie(QString());
        }
        removeCloudPlaylists(core::SourceId::Netease);
        m_accountPanel->refresh();
        m_recommend->refresh();
        refreshSourceAccessStates();
        return;
    }

    m_neteaseSessionVerifying = true;
    refreshSourceAccessStates();
    m_apiClient.loginStatus([this](const QJsonObject &obj) {
        m_neteaseSessionVerifying = false;
        const QJsonObject data = obj.value(QStringLiteral("data")).toObject();
        QJsonObject profile = data.value(QStringLiteral("profile")).toObject();
        if (profile.isEmpty())
            profile = obj.value(QStringLiteral("profile")).toObject();
        const QJsonObject account = data.value(QStringLiteral("account")).toObject();
        qint64 uid = profile.value(QStringLiteral("userId")).toVariant().toLongLong();
        if (uid <= 0)
            uid = account.value(QStringLiteral("id")).toVariant().toLongLong();

        if (uid > 0) {
            core::SettingsService::setOnlineUid(uid);
            const QString nickname = profile.value(QStringLiteral("nickname")).toString();
            if (!nickname.isEmpty())
                core::SettingsService::setOnlineNickname(nickname);
            refreshCloudPlaylists(core::SourceId::Netease, QString::number(uid));
        } else {
            // cookie 已失效时同步清理展示状态，避免“显示已登录但请求均未授权”。
            core::SettingsService::setOnlineCookie(QString());
            core::SettingsService::setOnlineUid(0);
            core::SettingsService::setOnlineNickname(QString());
            core::SettingsService::setOnlineAvatarUrl(QString());
            m_apiClient.setCookie(QString());
            removeCloudPlaylists(core::SourceId::Netease);
        }
        m_accountPanel->refresh();
        m_recommend->refresh();
        refreshSourceAccessStates();
    }, [this](const QString &) {
        // 服务暂时不可用时保留本地登录信息，并让推荐页回退到磁盘缓存。
        m_neteaseSessionVerifying = false;
        m_accountPanel->refresh();
        m_recommend->refresh();
        refreshSourceAccessStates();
    });
}

void MainWindow::refreshSourceAccessStates()
{
    QHash<int, core::SourceAccessState> states;
    states.insert(int(core::SourceId::Local), core::SourceAccessState::Authenticated);
    states.insert(int(core::SourceId::Netease),
                  !m_apiReady ? core::SourceAccessState::Unavailable
                  : m_neteaseSessionVerifying ? core::SourceAccessState::Verifying
                  : (core::SettingsService::onlineUid() > 0
                     && !core::SettingsService::onlineCookie().isEmpty())
                      ? core::SourceAccessState::Authenticated
                      : core::SourceAccessState::Guest);
    states.insert(int(core::SourceId::QqMusic),
                  !m_qqApiReady ? core::SourceAccessState::Unavailable
                  : m_qqSessionVerifying ? core::SourceAccessState::Verifying
                  : (!core::SettingsService::qqUserId().isEmpty()
                     && !core::SettingsService::qqCookie().isEmpty())
                      ? core::SourceAccessState::Authenticated
                      : core::SourceAccessState::Guest);
    if (states == m_sourceAccessStates)
        return;
    m_sourceAccessStates = states;
    if (m_search)
        m_search->setSourceAccessStates(states);
    for (ui::SongListView *view : findChildren<ui::SongListView *>())
        view->setSourceAccessStates(states);
}

void MainWindow::onCurrentSongChanged(const core::Song &song, int index)
{
    Q_UNUSED(index);
    m_currentSongId = song.id;
    m_playerBar->setSong(song, m_playlists.isFavorite(song.id));
    m_playerBar->setDownloadActive(m_activeDownloadSongIds.contains(song.selectionIdentity()));
    // 切歌只更新各列表的播放标记。旧实现会重建整个本地曲库页面，
    // 连带同步解压所有歌手/专辑封面，造成启动恢复歌曲时长时间卡顿。
    for (ui::SongListView *view : findChildren<ui::SongListView *>())
        view->setPlayingSong(song);
    m_playing->setSong(song, QPixmap());
    m_playing->loadLyricsFor(song);
    m_playing->setLyricFontSize(core::SettingsService::lyricFontSize());
    if (!song.isOnline())
        enrichLocalSong(song);
    if (song.id > 0) {
        m_library.markPlayed(song.id);
        m_playlists.recordPlay(song.id);
    }
}

namespace {

QString normalizedMatchText(const QString &text)
{
    QString result;
    for (const QChar ch : text) {
        if (ch.isLetterOrNumber())
            result.append(ch.toLower());
    }
    return result;
}

bool hasUsableSidecarLyrics(const QString &musicPath)
{
    const QString path = core::LyricsLoader::sidecarPathFor(musicPath);
    return QFileInfo::exists(path) && QFileInfo(path).size() > 0;
}

} // namespace

void MainWindow::enrichLocalSong(const core::Song &song)
{
    if (!m_apiReady || song.isOnline() || song.id <= 0 || song.filePath.isEmpty()
        || m_metadataAttempted.contains(song.filePath))
        return;
    const QString suffix = QFileInfo(song.filePath).suffix().toLower();
    if (suffix == QLatin1String("mgg") || suffix == QLatin1String("mflac"))
        return;
    const bool hasCover = !song.coverPath.isEmpty() && QFileInfo::exists(song.coverPath);
    if (hasCover && hasUsableSidecarLyrics(song.filePath) && !song.album.isEmpty())
        return;
    const QString titleKey = normalizedMatchText(song.title);
    if (titleKey.isEmpty())
        return;

    m_metadataAttempted.insert(song.filePath);
    // 元数据补全不能在 UI 线程读取整首音频并计算 MD5。标题+歌手搜索本身是
    // 异步请求，按队列节流即可保持启动期间界面可交互。
    searchLocalMetadata(song);
}

void MainWindow::searchLocalMetadata(const core::Song &song)
{
    QString keywords = song.title;
    if (!song.artist.isEmpty())
        keywords += QLatin1Char(' ') + song.artist;
    m_apiClient.searchSongs(keywords, 8,
                            [this, song](const QJsonArray &items) {
                                resolveLocalMetadataMatch(song, items, false);
                            });
}

void MainWindow::resolveLocalMetadataMatch(const core::Song &song, const QJsonArray &items, bool exactHash)
{
    core::Song best;
    int bestScore = -1;
    bool ambiguous = false;
    const QString localTitle = normalizedMatchText(song.title);
    const QString localArtist = normalizedMatchText(song.artist);
    for (const QJsonValue &value : items) {
        const core::Song candidate = m_apiClient.songFromJson(value.toObject());
        if (candidate.onlineId <= 0)
            continue;
        const QString candidateTitle = normalizedMatchText(candidate.title);
        const QString candidateArtist = normalizedMatchText(candidate.artist);
        if (candidateTitle != localTitle)
            continue;
        if (!localArtist.isEmpty() && !candidateArtist.contains(localArtist)
            && !localArtist.contains(candidateArtist))
            continue;

        int score = 100;
        if (!localArtist.isEmpty())
            score += 50;
        if (song.durationMs > 0 && candidate.durationMs > 0) {
            const qint64 delta = qAbs(song.durationMs - candidate.durationMs);
            if (delta <= 3000)
                score += 40;
            else if (delta > 10000)
                score -= 40;
        }
        if (score > bestScore) {
            best = candidate;
            bestScore = score;
            ambiguous = false;
        } else if (score == bestScore && candidate.onlineId != best.onlineId) {
            ambiguous = true;
        }
    }
    if (best.onlineId <= 0 || (!exactHash && ambiguous))
        return;

    // 搜索结果有时只有 picId 没有 picUrl,详情接口能补齐封面和完整字段。
    m_apiClient.songDetail(best.onlineId, [this, song, best](const QJsonObject &obj) {
        core::Song detail = best;
        const QJsonArray songs = obj.value(QStringLiteral("songs")).toArray();
        if (!songs.isEmpty())
            detail = m_apiClient.songFromJson(songs.first().toObject());
        if (detail.onlineId <= 0)
            return;
        m_library.fillMissingSongMetadata(song.id, detail.artist, detail.album);

        if (!hasUsableSidecarLyrics(song.filePath)) {
            m_apiClient.lyric(detail.onlineId, [this, song](const QString &lrc, const QString &, const QString &) {
                if (!lrc.trimmed().isEmpty() && !core::LrcParser::parseBytes(lrc.toUtf8()).isEmpty()
                    && core::LyricsLoader::saveSidecar(song, lrc))
                    refreshCurrentSongMetadata(song.id);
            });
        }

        const core::Song current = m_library.songById(song.id);
        if (current.coverPath.isEmpty() && !detail.coverUrl.isEmpty()) {
            const QString path = m_library.coverCacheDir()
                + QStringLiteral("/local_netease_%1.jpg").arg(detail.onlineId);
            if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
                m_library.setSongCoverPath(song.id, path);
                refreshCurrentSongMetadata(song.id);
            } else {
                m_apiClient.downloadToFile(QUrl(detail.coverUrl), path, [this, id = song.id, path](bool ok) {
                    if (ok) {
                        m_library.setSongCoverPath(id, path);
                        refreshCurrentSongMetadata(id);
                    }
                });
            }
        }
    });
}

void MainWindow::refreshCurrentSongMetadata(qint64 songId)
{
    if (songId != m_currentSongId)
        return;
    const core::Song updated = m_library.songById(songId);
    if (updated.id <= 0)
        return;
    m_playerBar->setSong(updated, m_playlists.isFavorite(songId));
    m_playing->setSong(updated, QPixmap());
    m_playing->loadLyricsFor(updated);
}

void MainWindow::setupShortcuts()
{
    auto add = [this](const QKeySequence &seq, const std::function<void()> &fn) {
        auto *sc = new QShortcut(seq, this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, [this, fn] {
            if (!focusIsEditable())
                fn();
        });
    };
    add(QKeySequence(Qt::Key_Space), [this] { emit m_playerBar->playPauseClicked(); });
    add(QKeySequence(QStringLiteral("Ctrl+Left")), [this] { m_player.prev(); });
    add(QKeySequence(QStringLiteral("Ctrl+Right")), [this] { m_player.next(); });
    add(QKeySequence(QStringLiteral("Ctrl+Up")), [this] {
        m_player.setVolume(qMin(100, m_player.volume() + 5));
        core::SettingsService::setVolume(m_player.volume());
    });
    add(QKeySequence(QStringLiteral("Ctrl+Down")), [this] {
        m_player.setVolume(qMax(0, m_player.volume() - 5));
        core::SettingsService::setVolume(m_player.volume());
    });
    add(QKeySequence(Qt::Key_L), [this] { showPage(5); });
    add(QKeySequence(Qt::Key_Escape), [this] { navigateBack(); });
    add(QKeySequence(QStringLiteral("Ctrl+O")), [this] { addMusicFolder(); });
    add(QKeySequence(Qt::Key_F5), [this] { m_library.startScan(); });
    auto *searchShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this);
    searchShortcut->setContext(Qt::ApplicationShortcut);
    connect(searchShortcut, &QShortcut::activated, m_titleBar, &ui::TitleBar::focusSearch);
}

void MainWindow::ensureQqSearchSource()
{
    if (m_qqApiReady || m_qqApiStarting)
        return;
    m_qqApiStarting = true;
    m_qqApiService.ensureRunning([this] {
        m_qqApiStarting = false;
        m_qqApiReady = true;
        m_recommend->setSourceAvailable(core::SourceId::QqMusic, true);
        m_search->setOnlineSourceEnabled(core::SourceId::QqMusic, true);
        m_qqClient.setBaseUrl(m_qqApiService.apiBase());
        refreshSourceAccessStates();
        refreshSearchAfterQqReady();
    }, [this](const QString &) {
        m_qqApiStarting = false;
        m_qqApiReady = false;
        m_recommend->setSourceAvailable(core::SourceId::QqMusic, false);
        m_qqSearchExecutionPending = false;
        m_search->setOnlineSourceEnabled(core::SourceId::QqMusic, false);
        refreshSourceAccessStates();
    });
}

void MainWindow::refreshSearchAfterQqReady()
{
    const QString text = m_titleBar->searchText().trimmed();
    if (text.isEmpty()) {
        m_qqSearchExecutionPending = false;
        m_search->showSearchAssistant();
        return;
    }
    if (m_qqSearchExecutionPending && text == m_searchQuery) {
        m_qqSearchExecutionPending = false;
        m_search->performSearch(text);
        return;
    }
    m_qqSearchExecutionPending = false;
    m_search->previewSearchText(text);
}

void MainWindow::addMusicFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择音乐文件夹"));
    if (dir.isEmpty())
        return;
    QStringList folders = core::SettingsService::musicFolders();
    if (!folders.contains(dir)) {
        folders.append(dir);
    }
    // 即使目录已经在监控列表中，也要让“导入文件夹”立即重新读取元数据。
    m_library.setFolders(folders);
}

void MainWindow::addMusicFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("导入歌曲"), QString(),
        QStringLiteral("音频 (*.mp3 *.flac *.wav *.m4a *.aac *.ogg *.mgg)"));
    if (files.isEmpty())
        return;
    QStringList folders = core::SettingsService::musicFolders();
    for (const QString &f : files) {
        const QString dir = QFileInfo(f).absolutePath();
        if (!folders.contains(dir))
            folders.append(dir);
    }
    // 之前同一目录已存在时不会触发扫描，导致手动导入看似无效。
    m_library.setFolders(folders);
}

void MainWindow::connectSongListActions()
{
    const auto views = findChildren<ui::SongListView *>();
    for (ui::SongListView *view : views) {
        connect(view, &ui::SongListView::downloadRequested, this, [this, view](int row) {
            handleSongDownload(view->songs().value(row));
        });
        connect(view, &ui::SongListView::deleteDownloadRequested, this, [this, view](int row) {
            handleSongDelete(view->songs().value(row));
        });
        connect(view, &ui::SongListView::batchDownloadRequested, this,
                [this](const QList<core::Song> &songs) {
            QList<core::Song> pending;
            for (const core::Song &candidate : songs) {
                const core::Song song = materializeSongForAction(candidate);
                if (song.isOnline() && song.id > 0 && !song.isDownloaded())
                    pending.append(song);
            }
            if (pending.isEmpty()) {
                QToolTip::showText(QCursor::pos(), QStringLiteral("所选歌曲均不可下载或已经下载"),
                                   this, QRect(), 2200);
                return;
            }
            m_downloads.enqueue(pending);
            const int skipped = songs.size() - pending.size();
            const QString message = skipped > 0
                ? QStringLiteral("已加入 %1 首下载任务，跳过 %2 首").arg(pending.size()).arg(skipped)
                : QStringLiteral("已加入 %1 首下载任务").arg(pending.size());
            QToolTip::showText(QCursor::pos(), message, this, QRect(), 2600);
        });
        connect(view, &ui::SongListView::batchDeleteRequested, this,
                [this](const QList<core::Song> &songs) {
            int localRemoved = 0;
            int downloadsRemoved = 0;
            int cachesRemoved = 0;
            int onlineRecordsRemoved = 0;
            QStringList failures;
            for (const core::Song &song : songs) {
                const bool local = !song.isOnline();
                const bool downloaded = song.isDownloaded();
                const bool cached = song.isCached();
                if (!handleSongDelete(song, true)) {
                    failures.append(song.title.isEmpty() ? QStringLiteral("未知歌曲") : song.title);
                    continue;
                }
                if (local)
                    ++localRemoved;
                else if (downloaded)
                    ++downloadsRemoved;
                else if (cached)
                    ++cachesRemoved;
                else
                    ++onlineRecordsRemoved;
            }
            QStringList summary;
            if (localRemoved > 0)
                summary.append(QStringLiteral("从曲库移除 %1 首").arg(localRemoved));
            if (downloadsRemoved > 0)
                summary.append(QStringLiteral("删除永久下载 %1 首").arg(downloadsRemoved));
            if (cachesRemoved > 0)
                summary.append(QStringLiteral("删除缓存 %1 首").arg(cachesRemoved));
            if (onlineRecordsRemoved > 0)
                summary.append(QStringLiteral("移除在线记录 %1 首").arg(onlineRecordsRemoved));
            if (!failures.isEmpty()) {
                QString detail = failures.mid(0, 3).join(QStringLiteral("、"));
                if (failures.size() > 3)
                    detail += QStringLiteral("等");
                summary.append(QStringLiteral("失败 %1 首：%2").arg(failures.size()).arg(detail));
            }
            if (summary.isEmpty())
                summary.append(QStringLiteral("没有可处理的歌曲"));
            QToolTip::showText(QCursor::pos(), summary.join(QStringLiteral("；")),
                               this, QRect(), 4200);
        });
        connect(view, &ui::SongListView::batchFavoriteRequested, this,
                &MainWindow::handleBatchFavorite);
        connect(view, &ui::SongListView::batchAddToPlaylistRequested, this,
                &MainWindow::handleBatchAddToPlaylist);
        connect(view, &ui::SongListView::batchCreatePlaylistRequested, this,
                &MainWindow::handleBatchCreatePlaylist);
    }
}

void MainWindow::handleSongDownload(const core::Song &song)
{
    const core::Song stored = materializeSongForAction(song);
    if (!stored.isOnline() || stored.id <= 0 || stored.isDownloaded())
        return;
    if (m_downloads.enqueue(stored) > 0)
        QToolTip::showText(QCursor::pos(), QStringLiteral("已加入下载队列"),
                           this, QRect(), 1800);
}

bool MainWindow::handleSongDelete(const core::Song &song, bool batch)
{
    if (song.id <= 0)
        return false;
    bool deleteLocalFile = false;
    if (!batch && !song.isOnline()) {
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("删除本地导入歌曲"));
        box.setText(QStringLiteral("请选择对“%1”的处理方式：").arg(song.title));
        QPushButton *removeFromLibrary = box.addButton(QStringLiteral("仅从曲库移除"), QMessageBox::AcceptRole);
        QPushButton *removeFile = box.addButton(QStringLiteral("同时删除硬盘文件"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == nullptr || box.clickedButton() == box.button(QMessageBox::Cancel))
            return false;
        deleteLocalFile = box.clickedButton() == removeFile;
        if (box.clickedButton() != removeFromLibrary && !deleteLocalFile)
            return false;
    }

    const bool wasCurrent = song.id == m_currentSongId;
    bool ok = false;
    if (!song.isOnline()) {
        core::PlayerService::FileReleaseState release;
        if (deleteLocalFile && QFileInfo::exists(song.filePath)) {
            release = m_player.releaseFileForRemoval(song.filePath);
            if (!QFile::remove(song.filePath)) {
                if (release.detached)
                    m_player.synchronizeSong(song, release);
                if (!batch)
                    QMessageBox::warning(this, QStringLiteral("删除失败"),
                                         QStringLiteral("无法删除本地文件，文件可能正在使用、只读或权限不足。"));
                return false;
            }
        }
        m_player.removeSongById(song.id);
        m_library.removeSong(song.id);
        ok = m_library.songById(song.id).id <= 0;
    } else if (song.isDownloaded()) {
        const auto release = m_player.releaseFileForRemoval(song.downloadPath);
        const core::LibraryService::FileRemovalResult result =
            m_library.removeSongDownloadDetailed(song.id);
        ok = result.ok;
        const core::Song updated = m_library.songById(song.id);
        if (release.detached && updated.id > 0)
            m_player.synchronizeSong(updated, release);
        else if (updated.id > 0)
            m_player.synchronizeSong(updated);
        if (!ok && !batch)
            QMessageBox::warning(this, QStringLiteral("删除下载失败"), result.error);
    } else if (song.isCached()) {
        const auto release = m_player.releaseFileForRemoval(song.cachePath);
        const core::LibraryService::FileRemovalResult result =
            m_library.removeSongCacheDetailed(song.id);
        ok = result.ok;
        const core::Song updated = m_library.songById(song.id);
        if (release.detached && updated.id > 0)
            m_player.synchronizeSong(updated, release);
        else if (updated.id > 0)
            m_player.synchronizeSong(updated);
        if (!ok && !batch)
            QMessageBox::warning(this, QStringLiteral("删除缓存失败"), result.error);
    } else {
        m_player.removeSongById(song.id);
        m_library.removeSong(song.id);
        ok = m_library.songById(song.id).id <= 0;
    }
    if (ok && wasCurrent && m_player.playlist().isEmpty())
        m_currentSongId = -1;
    if (ok)
        refreshDownloadPage();
    return ok;
}

void MainWindow::handleBatchFavorite(const QList<core::Song> &songs, bool favorite)
{
    QList<qint64> songIds;
    for (const core::Song &candidate : songs) {
        const core::Song song = materializeSongForAction(candidate);
        if (song.id > 0)
            songIds.append(song.id);
    }
    if (songIds.isEmpty())
        return;
    const core::PlaylistController::BatchResult result =
        m_playlists.setFavoritesBatch(songIds, favorite);
    if (!result.success)
        return;
    const QString action = favorite ? QStringLiteral("收藏") : QStringLiteral("取消收藏");
    QToolTip::showText(QCursor::pos(),
                       QStringLiteral("已%1 %2 首，跳过 %3 首")
                           .arg(action).arg(result.changed).arg(result.unchanged),
                       this, QRect(), 2600);
}

void MainWindow::handleBatchAddToPlaylist(const QList<core::Song> &songs, int playlistId)
{
    QList<qint64> songIds;
    for (const core::Song &candidate : songs) {
        const core::Song song = materializeSongForAction(candidate);
        if (song.id > 0)
            songIds.append(song.id);
    }
    if (songIds.isEmpty())
        return;
    const core::PlaylistController::BatchResult result =
        m_playlists.addSongsBatch(playlistId, songIds);
    if (!result.success)
        return;
    QToolTip::showText(QCursor::pos(),
                       QStringLiteral("已添加 %1 首到歌单，跳过 %2 首")
                           .arg(result.changed).arg(result.unchanged),
                       this, QRect(), 2600);
}

void MainWindow::handleBatchCreatePlaylist(const QList<core::Song> &songs)
{
    if (songs.isEmpty())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("新建歌单"),
                                               QStringLiteral("歌单名称:"), QLineEdit::Normal,
                                               QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    const int playlistId = m_playlists.createPlaylist(name);
    if (playlistId <= 0)
        return;
    handleBatchAddToPlaylist(songs, playlistId);
}

void MainWindow::refreshSongListStates()
{
    QSet<qint64> favoriteIds;
    for (const core::Song &song : m_playlists.songsOf(m_playlists.favoritePlaylistId()))
        favoriteIds.insert(song.id);
    for (ui::SongListView *view : findChildren<ui::SongListView *>()) {
        view->refreshLibraryState(&m_library);
        view->setFavoriteIds(favoriteIds);
    }
}

void MainWindow::refreshDownloadVisualStates()
{
    QHash<QString, qint64> activeSongIds;
    QSet<QString> activeIdentities;
    for (const core::DownloadService::Task &task : m_downloads.tasks()) {
        if (task.state != core::DownloadService::Queued
            && task.state != core::DownloadService::Downloading)
            continue;
        const QString identity = task.song.selectionIdentity();
        activeIdentities.insert(identity);
        activeSongIds.insert(identity, task.song.id);
    }

    const auto views = findChildren<ui::SongListView *>();
    for (auto it = m_activeDownloadSongIds.cbegin(); it != m_activeDownloadSongIds.cend(); ++it) {
        if (activeSongIds.contains(it.key()))
            continue;
        const core::Song stored = m_library.songById(it.value());
        if (stored.id <= 0)
            continue;
        for (ui::SongListView *view : views)
            view->updateSong(stored);
    }
    for (ui::SongListView *view : views)
        view->setDownloadingIdentities(activeIdentities);

    const core::Song current = m_player.currentSong();
    if (current.id > 0) {
        const core::Song stored = m_library.songById(current.id);
        if (stored.id > 0)
            m_playerBar->setSong(stored, m_playlists.isFavorite(stored.id));
        m_playerBar->setDownloadActive(activeIdentities.contains(current.selectionIdentity()));
    }
    m_activeDownloadSongIds = activeSongIds;
}

void MainWindow::refreshDownloadPage()
{
    const QList<core::DownloadService::Task> tasks = m_downloads.tasks();
    m_downloadPage->setTasks(tasks);
    QList<core::Song> downloaded;
    for (const core::Song &song : m_library.allSongs()) {
        if (song.isDownloaded())
            downloaded.append(song);
    }
    std::sort(downloaded.begin(), downloaded.end(), [](const core::Song &a,
                                                       const core::Song &b) {
        return QString::localeAwareCompare(a.title, b.title) < 0;
    });
    m_downloadPage->setDownloadedSongs(downloaded);
    bool downloading = false;
    bool queued = false;
    for (const core::DownloadService::Task &task : tasks) {
        downloading = downloading || task.state == core::DownloadService::Downloading;
        queued = queued || task.state == core::DownloadService::Queued;
    }
    if (m_sidebarFooter)
        m_sidebarFooter->setDownloadStatus(downloading, queued, !downloaded.isEmpty());
}

bool MainWindow::focusIsEditable() const
{
    QWidget *w = QApplication::focusWidget();
    return qobject_cast<QLineEdit *>(w) || qobject_cast<QTextEdit *>(w)
        || qobject_cast<QPlainTextEdit *>(w);
}

bool MainWindow::nativeEvent(const QByteArray &, void *message, qintptr *result)
{
    const auto *msg = static_cast<MSG *>(message);
    if (msg->message == WM_NCHITTEST) {
        const QPoint cursorGlobal = QCursor::pos();
        const NcHitTestResult r = computeHitTest(this, cursorGlobal, m_titleBar);
        if (r.handled) {
            *result = r.code;
            return true;
        }
    }
    return QMainWindow::nativeEvent(QByteArray(), message, result);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    const core::Song song = m_player.currentSong();
    if (song.id > 0)
        core::SettingsService::saveLastSong(song.filePath, m_player.position());
    core::SettingsService::saveWindowGeometry(saveGeometry());
    core::SettingsService::setVolume(m_player.volume());
    core::SettingsService::setMuted(m_player.muted());
    core::SettingsService::setPlayMode(int(m_player.mode()));
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange && m_titleBar)
        m_titleBar->setMaximizedState(isMaximized());
    QMainWindow::changeEvent(event);
}
