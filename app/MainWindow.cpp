#include "MainWindow.h"

#include "core/LyricsLoader.h"
#include "core/SettingsService.h"
#include "ui/AccountDialog.h"
#include "ui/AccountPanel.h"
#include "ui/AuroraBackground.h"
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
#include "ui/SettingsDialog.h"
#include "ui/SongListPage.h"
#include "ui/TitleBar.h"

#include <QApplication>
#include <QAbstractScrollArea>
#include <QCryptographicHash>
#include <QCursor>
#include <QCloseEvent>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QStackedWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <windows.h>

#include <functional>

namespace {
constexpr int kResizeBorder = 5;

void disableHorizontalScrollbars(QWidget *root)
{
    const QList<QAbstractScrollArea *> areas = root->findChildren<QAbstractScrollArea *>();
    for (QAbstractScrollArea *a : areas)
        a->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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

    if (titleBar && pos.y() <= titleBar->height()) {
        for (int i = 0; i <= ui::TitleBar::CloseBtn; ++i) {
            const QRect r = titleBar->windowButtonRect(i);
            if (r.translated(titleBar->pos()).contains(pos)) {
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
        if (sR.isValid() && sR.translated(titleBar->pos()).contains(pos))
            return { true, HTCLIENT };
        return { true, HTCAPTION };
    }
    return { false, HTCLIENT };
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("仿网易云播放器"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    resize(1280, 800);
    setMinimumSize(940, 600);

    const QByteArray geometry = core::SettingsService::windowGeometry();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    m_titleBar = new ui::TitleBar(this);
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

    m_player.setSourceProvider(&m_apiClient);
    m_player.setLibrary(&m_library);
    m_search->setSourceProvider(&m_apiClient, &m_library);
    m_playing->setSourceProvider(&m_apiClient);
    m_recommend->setSourceProvider(&m_apiClient, &m_library);
    m_apiClient.setCookie(core::SettingsService::onlineCookie());

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName("contentStack");
    m_stack->addWidget(m_recommend);
    m_stack->addWidget(m_favorites);
    m_stack->addWidget(m_libraryPage);
    m_stack->addWidget(m_selfPlaylists);
    m_stack->addWidget(m_songListPage);
    m_stack->addWidget(m_playing);
    m_stack->addWidget(m_search);

    auto *body = new QWidget(this);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    auto *sideCol = new QVBoxLayout;
    sideCol->setContentsMargins(0, 0, 0, 0);
    sideCol->setSpacing(0);
    sideCol->addWidget(m_sideBar, 1);
    sideCol->addWidget(m_accountPanel);
    bodyLayout->addLayout(sideCol);
    bodyLayout->addWidget(m_stack, 1);

    auto *central = new ui::AuroraBackground(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_titleBar);
    layout->addWidget(body, 1);
    setCentralWidget(central);

    m_playerBar->setParent(central);
    m_playerBar->setBackdropSource(body);
    positionPlayerBar();
    m_playerBar->raise();
    body->setStyleSheet(QStringLiteral("background: transparent;"));
    QTimer::singleShot(0, this, [this] { positionPlayerBar(); });

    // ---------- 标题栏 ----------
    connect(m_titleBar, &ui::TitleBar::minimizeClicked, this, &QWidget::showMinimized);
    connect(m_titleBar, &ui::TitleBar::maximizeClicked, this, [this] {
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(m_titleBar, &ui::TitleBar::closeClicked, this, &QWidget::close);
    connect(m_titleBar, &ui::TitleBar::searchRequested, this, [this](const QString &text) {
        m_searchQuery = text;
        if (text.trimmed().isEmpty()) {
            showPage(m_lastPage);
            return;
        }
        m_search->performSearch(m_library.allSongs(), text);
        showPage(6);
    });

    // ---------- 侧边栏 ----------
    connect(m_sideBar, &ui::SideBar::pageRequested, this, &MainWindow::showPage);
    connect(m_sideBar, &ui::SideBar::playlistSelected, this, &MainWindow::openPlaylist);
    connect(m_sideBar, &ui::SideBar::createPlaylistRequested, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("创建歌单"),
                                                   QStringLiteral("歌单名称:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            const int id = m_playlists.createPlaylist(name);
            if (id > 0) {
                refreshSidebar();
                refreshAllPages();
                openPlaylist(id);
            }
        }
    });

    // ---------- 账号区 / 设置 ----------
    connect(m_accountPanel, &ui::AccountPanel::accountClicked, this, &MainWindow::openAccount);
    connect(m_accountPanel, &ui::AccountPanel::settingsClicked, this, &MainWindow::openSettings);

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
    connect(m_playerBar, &ui::PlayerBar::lyricsClicked, this, [this] { showPage(5); });
    connect(m_playerBar, &ui::PlayerBar::playlistClicked, this, &MainWindow::openPlaybackQueue);

    // ---------- 播放器信号 → UI ----------
    connect(&m_player, &core::PlayerService::songChanged, this, &MainWindow::onCurrentSongChanged);
    connect(&m_player, &core::PlayerService::playingChanged, m_playerBar, &ui::PlayerBar::setPlaying);
    connect(&m_player, &core::PlayerService::positionChanged, m_playerBar, &ui::PlayerBar::setPosition);
    connect(&m_player, &core::PlayerService::positionChanged, m_playing, &ui::PlayingPage::setPosition);
    connect(&m_player, &core::PlayerService::durationChanged, m_playerBar, &ui::PlayerBar::setDuration);
    connect(&m_player, &core::PlayerService::modeChanged, m_playerBar, &ui::PlayerBar::setMode);
    connect(&m_player, &core::PlayerService::volumeChanged, m_playerBar, &ui::PlayerBar::setVolume);
    connect(&m_player, &core::PlayerService::mutedChanged, m_playerBar, &ui::PlayerBar::setMuted);
    connect(&m_player, &core::PlayerService::errorOccurred, this, [this](const QString &) {
        const core::Song s = m_player.currentSong();
        m_playerBar->setSong(s, m_playlists.isFavorite(s.id));
    });

    // ---------- 推荐页 ----------
    connect(m_recommend, &ui::RecommendPage::playRequested, this, &MainWindow::playSongs);
    connect(m_recommend, &ui::RecommendPage::openPlaylistRequested, this, &MainWindow::openOnlinePlaylist);
    connect(m_recommend, &ui::RecommendPage::loginRequested, this, &MainWindow::openAccount);

    // ---------- 收藏页 ----------
    connect(m_favorites, &ui::FavoritesPage::playRequested, this, &MainWindow::playSongs);
    connect(m_favorites, &ui::FavoritesPage::heartRequested, this, [this](int row) {
        const auto songs = m_playlists.songsOf(m_playlists.favoritePlaylistId());
        const core::Song s = songs.value(row);
        if (s.id > 0)
            m_playlists.setFavorite(s.id, false);
    });
    connect(m_favorites, &ui::FavoritesPage::addToPlaylistRequested, this, [this](int row, int plId) {
        const auto songs = m_playlists.songsOf(m_playlists.favoritePlaylistId());
        const core::Song s = songs.value(row);
        if (s.id > 0)
            m_playlists.addSong(plId, s.id);
    });
    connect(m_favorites, &ui::FavoritesPage::removeFromPlaylistRequested, this, [this](int row) {
        const auto songs = m_playlists.songsOf(m_playlists.favoritePlaylistId());
        const core::Song s = songs.value(row);
        if (s.id > 0)
            m_playlists.setFavorite(s.id, false);
    });

    // ---------- 本地歌单页 ----------
    connect(m_libraryPage, &ui::LibraryPage::playRequested, this, &MainWindow::playSongs);
    connect(m_libraryPage, &ui::LibraryPage::artistClicked, this, &MainWindow::openArtist);
    connect(m_libraryPage, &ui::LibraryPage::albumClicked, this, &MainWindow::openAlbum);
    connect(m_libraryPage, &ui::LibraryPage::heartRequested, this, [this](int row) {
        const core::Song s = m_libraryPage->currentSongs().value(row);
        if (s.id > 0)
            m_playlists.setFavorite(s.id, !m_playlists.isFavorite(s.id));
    });
    connect(m_libraryPage, &ui::LibraryPage::addToPlaylistRequested, this, [this](int row, int plId) {
        const core::Song s = m_libraryPage->currentSongs().value(row);
        if (s.id > 0)
            m_playlists.addSong(plId, s.id);
    });
    connect(m_libraryPage, &ui::LibraryPage::deleteFromLibraryRequested, this, [this](int row) {
        const core::Song s = m_libraryPage->currentSongs().value(row);
        if (s.id > 0)
            m_library.removeSong(s.id);
    });
    connect(m_libraryPage, &ui::LibraryPage::importRequested, this, &MainWindow::addMusicFolder);
    connect(m_libraryPage, &ui::LibraryPage::importFilesRequested, this, &MainWindow::addMusicFiles);

    // ---------- 自建歌单页 ----------
    connect(m_selfPlaylists, &ui::SelfPlaylistsPage::openPlaylistRequested, this, &MainWindow::openPlaylist);
    connect(m_selfPlaylists, &ui::SelfPlaylistsPage::createPlaylistRequested, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("创建歌单"),
                                                   QStringLiteral("歌单名称:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            const int id = m_playlists.createPlaylist(name);
            if (id > 0) {
                refreshSidebar();
                refreshAllPages();
                openPlaylist(id);
            }
        }
    });

    // ---------- 歌单/详情页 ----------
    connect(m_songListPage, &ui::SongListPage::playAllRequested, this, [this](const QList<core::Song> &songs) {
        playSongs(songs, 0);
    });
    connect(m_songListPage, &ui::SongListPage::playRequested, this, &MainWindow::playSongs);
    connect(m_songListPage, &ui::SongListPage::heartRequested, this, [this](int row) {
        const core::Song s = m_songListPage->currentSongs().value(row);
        if (s.id > 0)
            m_playlists.setFavorite(s.id, !m_playlists.isFavorite(s.id));
    });
    connect(m_songListPage, &ui::SongListPage::addToPlaylistRequested, this, [this](int row, int plId) {
        const core::Song s = m_songListPage->currentSongs().value(row);
        if (s.id > 0)
            m_playlists.addSong(plId, s.id);
    });
    connect(m_songListPage, &ui::SongListPage::removeFromPlaylistRequested, this, [this](int row) {
        const core::Song s = m_songListPage->currentSongs().value(row);
        if (s.id > 0 && m_playlistContext > 0) {
            m_playlists.removeSong(m_playlistContext, s.id);
            openPlaylist(m_playlistContext);
        }
    });
    connect(m_songListPage, &ui::SongListPage::deleteFromLibraryRequested, this, [this](int row) {
        const core::Song s = m_songListPage->currentSongs().value(row);
        if (s.id > 0) {
            const bool wasCurrent = s.id == m_currentSongId;
            m_library.removeSong(s.id);
            if (wasCurrent)
                m_player.next();
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
    connect(m_search, &ui::SearchPage::heartRequested, this, [this](int row) {
        const core::Song s = m_search->currentSongs().value(row);
        if (s.id > 0)
            m_playlists.setFavorite(s.id, !m_playlists.isFavorite(s.id));
    });
    connect(m_search, &ui::SearchPage::addToPlaylistRequested, this, [this](int row, int plId) {
        const core::Song s = m_search->currentSongs().value(row);
        if (s.id > 0)
            m_playlists.addSong(plId, s.id);
    });
    connect(m_search, &ui::SearchPage::deleteFromLibraryRequested, this, [this](int row) {
        const core::Song s = m_search->currentSongs().value(row);
        if (s.id > 0)
            m_library.removeSong(s.id);
    });

    // ---------- 音乐库信号 ----------
    connect(&m_library, &core::LibraryService::libraryChanged, this, [this] {
        refreshSidebar();
        refreshLibraryViews();
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
        enrichLocalMetadata(m_library.allSongs());
    });

    // ---------- 播放列表信号 ----------
    connect(&m_playlists, &core::PlaylistController::playlistsChanged, this, [this] {
        refreshSidebar();
        refreshAllPages();
    });
    connect(&m_playlists, &core::PlaylistController::playlistSongsChanged, this, [this](int) {
        refreshSidebar();
        if (m_playlistContext > 0)
            openPlaylist(m_playlistContext);
    });

    // ---------- 初始化 ----------
    if (m_library.openDatabase()) {
        m_playlists.setDatabase(m_library.database());
        refreshSidebar();
        refreshLibraryViews();
        refreshAllPages();
        m_library.startScan();
    }
    m_player.setVolume(core::SettingsService::volume());
    m_player.setMuted(core::SettingsService::muted());
    m_player.setMode(core::PlayerService::PlayMode(core::SettingsService::playMode()));
    m_playing->setLyricFontSize(core::SettingsService::lyricFontSize());
    m_sideBar->setActivePage(ui::SideBar::RecommendPage);
    setupShortcuts();
    disableHorizontalScrollbars(this);

    m_apiService.ensureRunning(
        [this] {
            m_apiReady = true;
            m_apiClient.setBaseUrl(m_apiService.apiBase());
            m_recommend->refresh();
            enrichLocalMetadata(m_library.allSongs());
            hydrateOnlineCovers(m_library.allSongs());
        },
        [this](const QString &) {
            m_apiReady = false;
            m_recommend->refresh();
        });
}

void MainWindow::showPage(int pageId)
{
    if (pageId == 5)
        m_lastPage = m_stack->currentIndex();
    if (pageId >= 0 && pageId < m_stack->count())
        m_stack->setCurrentIndex(pageId);
    if (pageId == 1)
        m_favorites->setSongs(m_playlists.songsOf(m_playlists.favoritePlaylistId()), m_currentSongId);
    if (pageId == 3)
        refreshAllPages();
    m_sideBar->setActivePage(pageId);
}

void MainWindow::openPlaybackQueue()
{
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
    m_playlistContext = -1;
    m_songListPage->showContent(queue, QStringLiteral("播放列表"), meta,
                                m_currentSongId, false);
    m_songListPage->setPlaylistContext(-1);
    showPage(4);
}

void MainWindow::openPlaylist(int playlistId)
{
    m_playlistContext = playlistId;
    refreshSidebar();
    const auto songs = m_playlists.songsOf(playlistId);
    QString name = QStringLiteral("歌单");
    QString desc;
    QString cover;
    for (const auto &p : m_playlists.playlists())
        if (p.id == playlistId) {
            name = p.name;
            desc = p.description;
            cover = p.coverPath;
            break;
        }
    qint64 totalSec = 0;
    for (const auto &s : songs)
        totalSec += s.durationMs / 1000;
    const QString meta = desc.isEmpty()
        ? QStringLiteral("%1 首 · 共 %2:%3").arg(songs.size()).arg(totalSec / 60).arg(totalSec % 60, 2, 10, QLatin1Char('0'))
        : QStringLiteral("%1 首 · %2").arg(songs.size()).arg(desc);
    m_songListPage->showContent(songs, name, meta, m_currentSongId, playlistId > 0, cover);
    m_songListPage->setPlaylistContext(playlistId);
    showPage(4);
}

void MainWindow::openArtist(const QString &artist)
{
    QList<core::Song> songs;
    for (const auto &s : m_library.allSongs())
        if (s.artist == artist)
            songs.append(s);
    m_songListPage->showContent(songs, artist, QStringLiteral("歌手 · %1 首").arg(songs.size()), m_currentSongId, false);
    m_songListPage->setPlaylistContext(-1);
    showPage(4);
}

void MainWindow::openAlbum(const QString &album, const QString &artist)
{
    QList<core::Song> songs;
    for (const auto &s : m_library.allSongs())
        if (s.album == album && (artist.isEmpty() || s.artist == artist)
            && (!s.isOnline() || s.isCached()))
            songs.append(s);
    m_songListPage->showContent(songs, album, QStringLiteral("专辑 · %1 首").arg(songs.size()), m_currentSongId, false);
    m_songListPage->setPlaylistContext(-1);
    showPage(4);
}

void MainWindow::openOnlinePlaylist(qint64 id, const QString &name)
{
    auto showTracks = [this, id, name](const QString &coverPath) {
        m_apiClient.playlistTracks(id, [this, name, coverPath](const QJsonArray &arr) {
            QList<core::Song> songs;
            for (const QJsonValue &v : arr) {
                core::Song s = m_apiClient.songFromJson(v.toObject());
                s.id = m_library.upsertOnlineSong(s);
                if (s.id > 0) {
                    const core::Song stored = m_library.songById(s.id);
                    s.coverPath = stored.coverPath;
                    s.cachePath = stored.cachePath;
                    s.lyricPath = stored.lyricPath;
                }
                songs.append(s);
            }
            ensureOnlineCovers(songs);
            int local = 0, online = 0;
            for (const auto &s : songs)
                s.isOnline() ? ++online : ++local;
            const QString meta = QStringLiteral("本地 %1 首 · 在线 %2 首").arg(local).arg(online);
            m_songListPage->showContent(songs, name, meta, m_currentSongId, false, coverPath);
            m_songListPage->setPlaylistContext(-1);
            showPage(4);
        }, [this](const QString &msg) {
            QMessageBox::information(this, QStringLiteral("在线歌单"), QStringLiteral("加载失败:%1").arg(msg));
        });
    };

    m_apiClient.playlistDetail(id, [this, id, showTracks](const QJsonObject &playlist) {
        QString coverUrl = playlist.value(QStringLiteral("coverImgUrl")).toString();
        if (coverUrl.isEmpty())
            coverUrl = playlist.value(QStringLiteral("picUrl")).toString();
        const QString coverPath = m_library.playlistCoverCachePath(id);
        if (QFileInfo::exists(coverPath) && QFileInfo(coverPath).size() > 0) {
            showTracks(coverPath);
            return;
        }
        if (!coverUrl.isEmpty()) {
            m_apiClient.downloadToFile(QUrl(coverUrl), coverPath, [showTracks, coverPath](bool ok) {
                showTracks(ok ? coverPath : QString());
            });
        } else {
            showTracks(QString());
        }
    }, [showTracks](const QString &) {
        showTracks(QString());
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
        m_onlineCoverAttempted.insert(s.id);
        const QString path = m_library.songCoverCachePath(s);
        if (path.isEmpty())
            continue;
        if (QFileInfo::exists(path)) {
            m_library.setSongCoverPath(s.id, path);
            m_songListPage->refreshCovers(&m_library);
            continue;
        }
        const QUrl url(s.coverUrl);
        const qint64 id = s.id;
        m_apiClient.downloadToFile(url, path, [this, id, path](bool ok) {
            if (ok) {
                m_library.setSongCoverPath(id, path);
                m_songListPage->refreshCovers(&m_library);
            }
        });
    }
}

void MainWindow::hydrateOnlineCovers(const QList<core::Song> &songs)
{
    if (!m_apiReady)
        return;
    QList<core::Song> withCoverUrls;
    QList<qint64> ids;
    for (const core::Song &song : songs) {
        if (!song.isOnline() || song.id <= 0 || song.onlineId <= 0)
            continue;
        const core::Song stored = m_library.songById(song.id);
        if (!stored.coverPath.isEmpty() && QFileInfo::exists(stored.coverPath))
            continue;
        if (!song.coverUrl.isEmpty()) {
            withCoverUrls.append(song);
        } else if (!m_onlineCoverAttempted.contains(song.id)) {
            m_onlineCoverAttempted.insert(song.id);
            ids.append(song.onlineId);
        }
    }
    ensureOnlineCovers(withCoverUrls);
    if (ids.isEmpty())
        return;
    m_apiClient.songDetails(ids, [this](const QJsonArray &arr) {
        QList<core::Song> enriched;
        for (const QJsonValue &value : arr) {
            core::Song detail = m_apiClient.songFromJson(value.toObject());
            const core::Song stored = m_library.songByOnlineId(detail.source, detail.onlineId);
            if (stored.id <= 0 || detail.coverUrl.isEmpty())
                continue;
            detail.id = stored.id;
            detail.coverPath = stored.coverPath;
            detail.cachePath = stored.cachePath;
            detail.lyricPath = stored.lyricPath;
            m_library.upsertOnlineSong(detail);
            m_onlineCoverAttempted.remove(stored.id);
            enriched.append(detail);
        }
        ensureOnlineCovers(enriched);
    });
}

void MainWindow::openAccount()
{
    ui::AccountDialog dlg(&m_apiClient, this);
    connect(&dlg, &ui::AccountDialog::accountStateChanged, this, [this] {
        m_accountPanel->refresh();
        m_recommend->refresh();
    });
    dlg.exec();
}

void MainWindow::openSettings()
{
    ui::SettingsDialog dlg(&m_apiService, &m_apiClient, &m_library, this);
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
    m_player.setPlaylist(songs, index);
    m_player.play();
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
    return items;
}

void MainWindow::refreshSidebar()
{
    QList<ui::SideBar::PlaylistItem> items = selfPlaylistInfos();
    m_sideBar->setPlaylists(items, m_playlistContext);
    QList<QPair<int, QString>> menuItems;
    for (const auto &p : m_playlists.playlists())
        menuItems.append({ p.id, p.name });
    m_songListPage->setPlaylistMenuItems(menuItems);
}

void MainWindow::refreshLibraryViews()
{
    const auto all = m_library.allSongs();
    m_libraryPage->setSongs(all, m_currentSongId);
    if (!m_searchQuery.isEmpty())
        m_search->refreshLocalResults(all);
    m_search->refreshOnlineCovers();
    hydrateOnlineCovers(all);
}

void MainWindow::refreshAllPages()
{
    m_favorites->setSongs(m_playlists.songsOf(m_playlists.favoritePlaylistId()), m_currentSongId);
    m_selfPlaylists->setPlaylists(m_playlists.playlists());
    m_songListPage->refreshCovers(&m_library);
    m_songListPage->setPlayingId(m_currentSongId);
}

void MainWindow::onCurrentSongChanged(const core::Song &song, int index)
{
    Q_UNUSED(index);
    m_currentSongId = song.id;
    m_playerBar->setSong(song, m_playlists.isFavorite(song.id));
    refreshLibraryViews();
    m_songListPage->setPlayingId(m_currentSongId);
    m_playing->setSong(song, QPixmap());
    m_playing->loadLyricsFor(song);
    m_playing->setLyricFontSize(core::SettingsService::lyricFontSize());
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

void MainWindow::enrichLocalMetadata(const QList<core::Song> &songs)
{
    if (!m_apiReady)
        return;
    for (const core::Song &song : songs) {
        if (song.isOnline() || song.id <= 0 || song.filePath.isEmpty())
            continue;
        const QString suffix = QFileInfo(song.filePath).suffix().toLower();
        if (suffix == QLatin1String("mgg") || suffix == QLatin1String("mflac"))
            continue;
        const bool hasCover = !song.coverPath.isEmpty() && QFileInfo::exists(song.coverPath);
        if (hasCover && hasUsableSidecarLyrics(song.filePath) && !song.album.isEmpty())
            continue;
        enrichLocalSong(song);
    }
}

void MainWindow::enrichLocalSong(const core::Song &song)
{
    if (m_metadataAttempted.contains(song.filePath))
        return;
    const QString titleKey = normalizedMatchText(song.title);
    if (titleKey.isEmpty())
        return;

    m_metadataAttempted.insert(song.filePath);

    QFile file(song.filePath);
    QCryptographicHash hash(QCryptographicHash::Md5);
    const bool hashed = file.open(QIODevice::ReadOnly) && hash.addData(&file);
    const QString md5 = hashed ? QString::fromLatin1(hash.result().toHex()) : QString();
    if (!md5.isEmpty()) {
        m_apiClient.matchSong(song.title, song.artist, song.album, song.durationMs, md5,
                              [this, song](const QJsonArray &items) {
                                  if (!items.isEmpty())
                                      resolveLocalMetadataMatch(song, items, true);
                                  else
                                      searchLocalMetadata(song);
                              },
                              [this, song](const QString &) { searchLocalMetadata(song); });
    } else {
        searchLocalMetadata(song);
    }
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
    add(QKeySequence(QStringLiteral("Ctrl+O")), [this] { addMusicFolder(); });
    add(QKeySequence(Qt::Key_F5), [this] { m_library.startScan(); });
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
    if (event->type() == QEvent::WindowStateChange)
        m_titleBar->setMaximizedState(isMaximized());
    QMainWindow::changeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    positionPlayerBar();
}

void MainWindow::positionPlayerBar()
{
    if (!m_playerBar)
        return;
    QWidget *central = centralWidget();
    if (!central)
        return;
    if (!m_stack || m_stack->width() <= 0)
        return;
    const QPoint contentTopLeft = m_stack->mapTo(central, QPoint(0, 0));
    const int contentWidth = m_stack->width();
    const int playerWidth = qMax(1, qMin(860, contentWidth - 24));
    if (m_playerBar->width() != playerWidth)
        m_playerBar->setFixedWidth(playerWidth);
    const int maxX = contentTopLeft.x() + qMax(0, contentWidth - m_playerBar->width());
    const int centeredX = contentTopLeft.x() + (contentWidth - m_playerBar->width()) / 2;
    const int x = qBound(contentTopLeft.x(), centeredX, maxX);
    const int y = central->height() - m_playerBar->height() - 14;
    m_playerBar->move(x, qMax(contentTopLeft.y(), y));
    m_playerBar->raise();
}
