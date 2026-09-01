#include "SongListPage.h"

#include "core/LibraryService.h"
#include "core/SearchAggregator.h"
#include "ui/CoverProvider.h"
#include "ui/SongListView.h"

#include <QHBoxLayout>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace ui {

SongListPage::SongListPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(18);

    auto *header = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(26);

    m_cover = new QLabel(header);
    m_cover->setFixedSize(160, 160);
    headerLayout->addWidget(m_cover);

    auto *info = new QWidget(header);
    auto *infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(8);
    m_title = new QLabel(info);
    m_title->setStyleSheet(QStringLiteral("font-size:24px;font-weight:700;color:#E8E8E8;"));
    m_meta = new QLabel(info);
    m_meta->setStyleSheet(QStringLiteral("font-size:13px;color:#6E6E7A;"));
    auto *actions = new QHBoxLayout;
    actions->setContentsMargins(0, 6, 0, 0);
    actions->setSpacing(12);
    auto *playAll = new QPushButton(QStringLiteral("播放全部"), info);
    playAll->setCursor(Qt::PointingHandCursor);
    playAll->setStyleSheet(QStringLiteral(
        "QPushButton{background:#EC4141;color:white;border:none;border-radius:18px;padding:8px 22px;font-size:13px;}"
        "QPushButton:hover{background:#F04A4A;}"));
    connect(playAll, &QPushButton::clicked, this, [this] {
        const QList<core::Song> songs = m_view->songs();
        if (!songs.isEmpty())
            emit playAllRequested(songs);
    });
    actions->addWidget(playAll);

    m_moreBtn = new QToolButton(info);
    m_moreBtn->setObjectName(QStringLiteral("songListMoreButton"));
    m_moreBtn->setText(QStringLiteral("···"));
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    m_moreBtn->setStyleSheet(QStringLiteral(
        "QToolButton{background:#1B1B24;border:none;border-radius:18px;"
        "padding:4px 14px;color:#C8C8D0;font-size:15px;}"
        "QToolButton:hover{background:#3A2024;color:#FF5A5A;}"));
    m_moreBtn->hide();
    actions->addWidget(m_moreBtn);
    actions->addStretch(1);
    infoLayout->addWidget(m_title);
    infoLayout->addWidget(m_meta);
    infoLayout->addLayout(actions);
    headerLayout->addWidget(info, 1);
    layout->addWidget(header);

    m_view = new SongListView;
    layout->addWidget(m_view, 1);

    connect(m_view, &SongListView::playRequested, this, [this](int row) {
        emit playRequested(m_view->songs(), row);
    });
    connect(m_view, &SongListView::heartRequested, this, &SongListPage::heartRequested);
    connect(m_view, &SongListView::addToPlaylistRequested, this, &SongListPage::addToPlaylistRequested);
    connect(m_view, &SongListView::removeFromPlaylistRequested, this, [this](int row) {
        if (m_playbackQueueContext)
            emit removeFromPlaybackQueueRequested(row);
        else
            emit removeFromPlaylistRequested(row);
    });
    connect(m_view, &SongListView::deleteFromLibraryRequested, this, &SongListPage::deleteFromLibraryRequested);
}

void SongListPage::showContent(const QList<core::Song> &songs, const QString &title, const QString &meta,
                               qint64 playingId, bool removable, const QString &headerCoverPath,
                               bool mergeSources)
{
    m_songs = songs;
    m_playingId = playingId;
    m_headerCoverPath = headerCoverPath;
    m_mergeSources = mergeSources;
    m_title->setText(title);
    m_meta->setText(meta);
    if (!m_headerCoverPath.isEmpty() && QFileInfo::exists(m_headerCoverPath)) {
        const QPixmap pm(m_headerCoverPath);
        m_cover->setPixmap(pm.scaled(160, 160, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    } else if (songs.isEmpty())
        m_cover->setPixmap(CoverProvider::placeholder(title, 160, 10));
    else
        m_cover->setPixmap(CoverProvider::coverFor(songs.first(), 160, 10));
    m_view->setMergedCollectionActions(mergeSources);
    if (mergeSources) {
        m_view->setSearchResultGroups(
            core::SearchAggregator::aggregateSongsPreservingOrder(songs), playingId);
    } else {
        m_view->setSongs(songs, playingId);
    }
    m_view->setRemovable(removable);
}

QList<core::Song> SongListPage::currentSongs() const
{
    return m_view->songs();
}

QList<core::Song> SongListPage::memberSongsAt(int row) const
{
    return m_view->memberSongsAt(row);
}

void SongListPage::setPlayingId(qint64 playingId)
{
    m_playingId = playingId;
    m_view->setPlayingId(playingId);
}

void SongListPage::refreshCovers(core::LibraryService *library)
{
    if (!library || m_songs.isEmpty())
        return;
    bool changed = false;
    for (core::Song &song : m_songs) {
        const core::Song stored = library->songById(song.id);
        if (stored.coverPath != song.coverPath) {
            song.coverPath = stored.coverPath;
            changed = true;
        }
        if (stored.lyricPath != song.lyricPath)
            song.lyricPath = stored.lyricPath;
    }
    if (!changed)
        return;
    if (m_mergeSources) {
        m_view->setSearchResultGroups(
            core::SearchAggregator::aggregateSongsPreservingOrder(m_songs), m_playingId);
    } else {
        m_view->setSongs(m_songs, m_playingId);
    }
    if (!m_headerCoverPath.isEmpty() && QFileInfo::exists(m_headerCoverPath)) {
        const QPixmap pm(m_headerCoverPath);
        m_cover->setPixmap(pm.scaled(160, 160, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    } else {
        m_cover->setPixmap(m_songs.isEmpty()
                               ? CoverProvider::placeholder(m_title->text(), 160, 10)
                               : CoverProvider::coverFor(m_songs.first(), 160, 10));
    }
}

void SongListPage::setHeaderCoverPath(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;
    const QPixmap pm(path);
    if (pm.isNull())
        return;
    m_headerCoverPath = path;
    m_cover->setPixmap(pm.scaled(160, 160, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
}

void SongListPage::setPlaylistContext(int playlistId)
{
    m_playlistContext = playlistId;
    m_playbackQueueContext = false;
    // QToolButton 仍持有旧菜单指针时直接 delete 可能触发 QtWidgets 访问冲突。
    // 先解除绑定，再延迟销毁旧菜单，避免刷新歌单详情时使用悬空指针。
    if (QMenu *oldMenu = m_moreBtn->menu()) {
        m_moreBtn->setMenu(nullptr);
        oldMenu->deleteLater();
    }
    auto *menu = new QMenu(m_moreBtn);
    if (playlistId > 0) {
        if (playlistId != 1) {
            menu->addAction(QStringLiteral("编辑歌单"), this, [this] {
                emit editPlaylistRequested(m_playlistContext);
            });
        }
        menu->addAction(QStringLiteral("重命名歌单"), this, [this] {
            emit renamePlaylistRequested(m_playlistContext);
        });
        auto *del = menu->addAction(QStringLiteral("删除歌单"), this, [this] {
            emit deletePlaylistRequested(m_playlistContext);
        });
        del->setEnabled(playlistId != 1);
    }
    m_moreBtn->setMenu(menu);
    m_moreBtn->setPopupMode(QToolButton::InstantPopup);
    m_moreBtn->setVisible(!menu->isEmpty());
}

void SongListPage::setPlaybackQueueContext()
{
    m_playlistContext = -1;
    m_playbackQueueContext = true;
    if (QMenu *oldMenu = m_moreBtn->menu()) {
        m_moreBtn->setMenu(nullptr);
        oldMenu->deleteLater();
    }
    auto *menu = new QMenu(m_moreBtn);
    menu->addAction(QStringLiteral("保存为新建歌单"), this,
                    &SongListPage::savePlaybackQueueRequested);
    menu->addAction(QStringLiteral("清空播放列表"), this,
                    &SongListPage::clearPlaybackQueueRequested);
    m_moreBtn->setMenu(menu);
    m_moreBtn->setPopupMode(QToolButton::InstantPopup);
    m_moreBtn->show();
    m_view->setRemovable(true);
}

void SongListPage::setReadOnlyContext()
{
    setPlaylistContext(-1);
    m_view->setRemovable(false);
}

void SongListPage::setPlaylistMenuItems(const QList<QPair<int, QString>> &items)
{
    m_view->setPlaylistMenuItems(items);
}

} // namespace ui
