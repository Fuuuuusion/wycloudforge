#include "MainWindow.h"

#include "core/LyricsLoader.h"
#include "core/SettingsService.h"
#include "ui/DiscoverPage.h"
#include "ui/AuroraBackground.h"
#include "ui/LibraryPage.h"
#include "ui/LyricEditorDialog.h"
#include "ui/OnlinePage.h"
#include "ui/LoginDialog.h"
#include "ui/CommentsDialog.h"
#include "ui/PlayerBar.h"
#include "ui/PlayingPage.h"
#include "ui/SearchPage.h"
#include "ui/SettingsDialog.h"
#include "ui/SideBar.h"
#include "ui/SvgIcon.h"
#include "ui/SongListPage.h"
#include "ui/TitleBar.h"

#include <QApplication>
#include <QCursor>
#include <QCloseEvent>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

#include <windows.h>

#include <functional>

namespace {
constexpr int kResizeBorder = 5;

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
                case ui::TitleBar::CloseBtn: return { true, HTCLOSE };
                case ui::TitleBar::MaximizeBtn: return { true, HTMAXBUTTON };
                case ui::TitleBar::MinimizeBtn: return { true, HTMINBUTTON };
                case ui::TitleBar::SettingsBtn: return { true, HTCLIENT };
                default: break;
                }
            }
        }
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

    m_discover = new ui::DiscoverPage(this);
    m_libraryPage = new ui::LibraryPage(this);
    m_songListPage = new ui::SongListPage(this);
    m_playing = new ui::PlayingPage(this);
    m_search = new ui::SearchPage(this);
    m_online = new ui::OnlinePage(this);

    m_player.setSourceProvider(&m_apiClient);
    m_player.setLibrary(&m_library);
    m_search->setSourceProvider(&m_apiClient, &m_library);
    m_online->setSourceProvider(&m_apiClient, &m_library);
    m_playing->setSourceProvider(&m_apiClient);
    m_apiClient.setCookie(core::SettingsService::onlineCookie());

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName("contentStack");
    m_stack->addWidget(m_discover);
    m_stack->addWidget(m_libraryPage);
    m_stack->addWidget(m_songListPage);
    m_stack->addWidget(m_playing);
    m_stack->addWidget(m_search);
    m_stack->addWidget(m_online);

    auto *body = new QWidget(this);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(m_sideBar);
    bodyLayout->addWidget(m_stack, 1);

    auto *central = new ui::AuroraBackground(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 14);
    layout->setSpacing(0);
    layout->addWidget(m_titleBar);
    layout->addWidget(body, 1);
    layout->addWidget(m_playerBar, 0, Qt::AlignHCenter);
    setCentralWidget(central);

    body->setStyleSheet(QStringLiteral("background: transparent;"));

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
        showPage(4);
    });
    connect(m_titleBar, &ui::TitleBar::settingsClicked, this, [this] {
        ui::SettingsDialog dlg(&m_apiService, &m_apiClient, &m_library, this);
        connect(&dlg, &ui::SettingsDialog::rescanRequested, this, [&dlg, this] {
            core::SettingsService::setMusicFolders(dlg.folders());
            m_library.startScan();
        });
        if (dlg.exec() == QDialog::Accepted) {
            m_apiClient.setBaseUrl(core::SettingsService::onlineApiBase());
            const QStringList folders = dlg.folders();
            if (folders != core::SettingsService::musicFolders())
                m_library.setFolders(folders);
            core::SettingsService::setLyricFontSize(dlg.lyricFontSize());
            m_playing->setLyricFontSize(dlg.lyricFontSize());
        }
    });

    // ---------- 侧栏 ----------
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
                openPlaylist(id);
            }
        }
    });

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
    connect(m_playerBar, &ui::PlayerBar::lyricsClicked, this, [this] { showPage(3); });
    connect(m_playerBar, &ui::PlayerBar::playlistClicked, this, [this] { openPlaylist(1); });

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
        m_playerBar->setSong(m_player.currentSong(), m_playlists.isFavorite(m_player.currentSong().id));
    });

    // ---------- 发现页 ----------
    connect(m_discover, &ui::DiscoverPage::playRequested, this, &MainWindow::playSongs);
    connect(m_discover, &ui::DiscoverPage::playlistClicked, this, &MainWindow::openPlaylist);
    connect(m_discover, &ui::DiscoverPage::importRequested, this, &MainWindow::addMusicFolder);

    // ---------- 音乐库页 ----------
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
    connect(m_libraryPage, &ui::LibraryPage::removeFromPlaylistRequested, this, [this](int row) {
        const core::Song s = m_libraryPage->currentSongs().value(row);
        if (s.id > 0 && m_playlistContext > 0)
            m_playlists.removeSong(m_playlistContext, s.id);
    });
    connect(m_libraryPage, &ui::LibraryPage::deleteFromLibraryRequested, this, [this](int row) {
        const core::Song s = m_libraryPage->currentSongs().value(row);
        if (s.id > 0)
            m_library.removeSong(s.id);
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
            showPage(0);
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
    connect(m_playing, &ui::PlayingPage::commentsRequested, this, [this] {
        const core::Song song = m_playing->currentSong();
        if (song.onlineId > 0) {
            ui::CommentsDialog dlg(&m_apiClient, song.onlineId, song.title, this);
            dlg.exec();
        }
    });

    // ---------- 在线音乐页 ----------
    connect(m_online, &ui::OnlinePage::playRequested, this, &MainWindow::playSongs);
    connect(m_online, &ui::OnlinePage::openPlaylistRequested, this, &MainWindow::openOnlinePlaylist);
    connect(m_online, &ui::OnlinePage::openAlbumRequested, this, &MainWindow::openOnlineAlbum);
    connect(m_online, &ui::OnlinePage::openArtistRequested, this, &MainWindow::openOnlineArtist);
    connect(m_online, &ui::OnlinePage::loginRequested, this, &MainWindow::doLogin);
    connect(m_online, &ui::OnlinePage::logoutRequested, this, &MainWindow::doLogout);

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
    });

    // ---------- 播放列表信号 ----------
    connect(&m_playlists, &core::PlaylistController::playlistsChanged, this, &MainWindow::refreshSidebar);
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
        m_library.startScan();
    }
    m_player.setVolume(core::SettingsService::volume());
    m_player.setMuted(core::SettingsService::muted());
    m_player.setMode(core::PlayerService::PlayMode(core::SettingsService::playMode()));
    m_playing->setLyricFontSize(core::SettingsService::lyricFontSize());

    QList<QPair<int, QString>> playlistItems;
    for (const auto &p : m_playlists.playlists())
        playlistItems.append({ p.id, p.name });
    m_songListPage->setPlaylistMenuItems(playlistItems);
    m_sideBar->setActivePage(ui::SideBar::DiscoverPage);
    m_online->setLoginInfo(core::SettingsService::onlineNickname());
    setupShortcuts();

    m_apiService.ensureRunning(
        [this] {
            m_apiClient.setBaseUrl(m_apiService.apiBase());
            m_online->refresh();
        },
        [](const QString &) {});

}

void MainWindow::showPage(int pageId)
{
    if (pageId == 3)
        m_lastPage = m_stack->currentIndex();
    if (pageId >= 0 && pageId < m_stack->count())
        m_stack->setCurrentIndex(pageId);
    m_sideBar->setActivePage(pageId);
}

void MainWindow::openPlaylist(int playlistId)
{
    m_playlistContext = playlistId;
    refreshSidebar();
    const auto songs = m_playlists.songsOf(playlistId);
    QString name = QStringLiteral("歌单");
    for (const auto &p : m_playlists.playlists())
        if (p.id == playlistId) { name = p.name; break; }
    qint64 totalSec = 0;
    for (const auto &s : songs)
        totalSec += s.durationMs / 1000;
    const QString meta = QStringLiteral("%1 首 · 共 %2:%3")
                             .arg(songs.size())
                             .arg(totalSec / 60)
                             .arg(totalSec % 60, 2, 10, QLatin1Char('0'));
    m_songListPage->showContent(songs, name, meta, m_currentSongId, playlistId > 0);
    m_songListPage->setPlaylistContext(playlistId);
    refreshSidebar();
    showPage(2);
}

void MainWindow::openArtist(const QString &artist)
{
    QList<core::Song> songs;
    for (const auto &s : m_library.allSongs())
        if (s.artist == artist)
            songs.append(s);
    m_songListPage->showContent(songs, artist, QStringLiteral("歌手 · %1 首").arg(songs.size()),
                                m_currentSongId, false);
    m_songListPage->setPlaylistContext(-1);
    showPage(2);
}

void MainWindow::openAlbum(const QString &album, const QString &artist)
{
    QList<core::Song> songs;
    for (const auto &s : m_library.allSongs())
        if (s.album == album && (artist.isEmpty() || s.artist == artist))
            songs.append(s);
    m_songListPage->showContent(songs, album, QStringLiteral("专辑 · %1 首").arg(songs.size()),
                                m_currentSongId, false);
    m_songListPage->setPlaylistContext(-1);
    showPage(2);
}

void MainWindow::openOnlinePlaylist(qint64 id, const QString &name)
{
    m_apiClient.playlistTracks(id, [this, name](const QJsonArray &arr) {
        QList<core::Song> songs;
        for (const QJsonValue &v : arr) {
            core::Song s = m_apiClient.songFromJson(v.toObject());
            s.id = m_library.upsertOnlineSong(s);
            songs.append(s);
        }
        ensureOnlineCovers(songs);
        int local = 0;
        int online = 0;
        for (const auto &s : songs)
            s.isOnline() ? ++online : ++local;
        const QString meta = QStringLiteral("本地 %1 首 · 在线 %2 首").arg(local).arg(online);
        m_songListPage->showContent(songs, name, meta, m_currentSongId, false);
        m_songListPage->setPlaylistContext(-1);
        showPage(2);
    }, [this](const QString &msg) {
        QMessageBox::information(this, QStringLiteral("在线歌单"), QStringLiteral("加载失败:%1").arg(msg));
    });
}

void MainWindow::openOnlineAlbum(qint64 id)
{
    m_apiClient.albumDetail(id, [this](const QJsonObject &obj) {
        const QString name = obj.value(QStringLiteral("album")).toObject().value(QStringLiteral("name")).toString();
        QList<core::Song> songs;
        const QJsonArray arr = obj.value(QStringLiteral("songs")).toArray();
        for (const QJsonValue &v : arr) {
            core::Song s = m_apiClient.songFromJson(v.toObject());
            s.id = m_library.upsertOnlineSong(s);
            songs.append(s);
        }
        ensureOnlineCovers(songs);
        m_songListPage->showContent(songs, name, QStringLiteral("专辑 · %1 首").arg(songs.size()),
                                    m_currentSongId, false);
        m_songListPage->setPlaylistContext(-1);
        showPage(2);
    }, [this](const QString &msg) {
        QMessageBox::information(this, QStringLiteral("专辑"), QStringLiteral("加载失败:%1").arg(msg));
    });
}

void MainWindow::openOnlineArtist(qint64 id)
{
    m_apiClient.artistSongs(id, [this](const QJsonArray &arr) {
        QList<core::Song> songs;
        for (const QJsonValue &v : arr) {
            core::Song s = m_apiClient.songFromJson(v.toObject());
            s.id = m_library.upsertOnlineSong(s);
            songs.append(s);
        }
        ensureOnlineCovers(songs);
        m_songListPage->showContent(songs, QStringLiteral("歌手歌曲"), QStringLiteral("热门 %1 首").arg(songs.size()),
                                    m_currentSongId, false);
        m_songListPage->setPlaylistContext(-1);
        showPage(2);
    }, [this](const QString &msg) {
        QMessageBox::information(this, QStringLiteral("歌手"), QStringLiteral("加载失败:%1").arg(msg));
    });
}

void MainWindow::ensureOnlineCovers(const QList<core::Song> &songs)
{
    for (const core::Song &s : songs) {
        if (!s.isOnline() || s.coverUrl.isEmpty() || s.id <= 0)
            continue;
        const core::Song current = m_library.songById(s.id);
        if (!current.coverPath.isEmpty())
            continue;
        const QString path = m_library.coverCacheDir()
            + QStringLiteral("/online_%1_%2.jpg").arg(s.source).arg(s.onlineId);
        if (QFileInfo::exists(path)) {
            m_library.setSongCoverPath(s.id, path);
            continue;
        }
        const QUrl url(s.coverUrl);
        const qint64 id = s.id;
        m_apiClient.downloadToFile(url, path, [this, id, path](bool ok) {
            if (ok)
                m_library.setSongCoverPath(id, path);
        });
    }
}

void MainWindow::doLogin()
{
    ui::LoginDialog dlg(&m_apiClient, this);
    if (dlg.exec() == QDialog::Accepted)
        m_online->setLoginInfo(dlg.nickname());
}

void MainWindow::doLogout()
{
    m_apiClient.logout([](const QJsonObject &) {}, [](const QString &) {});
    core::SettingsService::setOnlineCookie(QString());
    core::SettingsService::setOnlineUid(0);
    core::SettingsService::setOnlineNickname(QString());
    m_apiClient.setCookie(QString());
    m_online->setLoginInfo(QString());
}

void MainWindow::playSongs(const QList<core::Song> &songs, int index)
{
    if (songs.isEmpty())
        return;
    m_player.setPlaylist(songs, index);
    m_player.play();
}

void MainWindow::refreshSidebar()
{
    QList<ui::SideBar::PlaylistItem> items;
    int active = -1;
    for (const auto &p : m_playlists.playlists()) {
        ui::SideBar::PlaylistItem item;
        item.id = p.id;
        item.name = p.name;
        item.favorite = (p.id == m_playlists.favoritePlaylistId());
        items.append(item);
        if (m_playlistContext == p.id)
            active = p.id;
    }
    m_sideBar->setPlaylists(items, active);
    QList<QPair<int, QString>> menuItems;
    for (const auto &p : m_playlists.playlists())
        menuItems.append({ p.id, p.name });
    m_songListPage->setPlaylistMenuItems(menuItems);
}

void MainWindow::refreshLibraryViews()
{
    const auto all = m_library.allSongs();
    m_discover->setLibrary(all, m_currentSongId);
    m_discover->setPlaylists(m_playlists.playlists());
    m_discover->setRecent(m_playlists.recentSongs(8));
    m_libraryPage->setSongs(all, m_currentSongId);
    if (!m_searchQuery.isEmpty())
        m_search->performSearch(all, m_searchQuery);
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
    add(QKeySequence(Qt::Key_Space), [this] {
        emit m_playerBar->playPauseClicked();
    });
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
    add(QKeySequence(Qt::Key_L), [this] { showPage(3); });
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
        m_library.setFolders(folders);
    }
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
