#include "SongListPage.h"

#include "core/LibraryService.h"
#include "ui/CoverProvider.h"
#include "ui/SongListView.h"

#include <QHBoxLayout>
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
        if (!m_songs.isEmpty())
            emit playAllRequested(m_songs);
    });
    actions->addWidget(playAll);

    m_moreBtn = new QToolButton(info);
    m_moreBtn->setText(QStringLiteral("···"));
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    m_moreBtn->setStyleSheet(QStringLiteral(
        "QToolButton{background:rgba(255,255,255,0.08);border:none;border-radius:18px;"
        "padding:4px 14px;color:#C8C8D0;font-size:15px;}"
        "QToolButton:hover{background:rgba(236,65,65,0.16);color:#FF5A5A;}"));
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
        emit playRequested(m_songs, row);
    });
    connect(m_view, &SongListView::heartRequested, this, &SongListPage::heartRequested);
    connect(m_view, &SongListView::addToPlaylistRequested, this, &SongListPage::addToPlaylistRequested);
    connect(m_view, &SongListView::removeFromPlaylistRequested, this, &SongListPage::removeFromPlaylistRequested);
    connect(m_view, &SongListView::deleteFromLibraryRequested, this, &SongListPage::deleteFromLibraryRequested);
}

void SongListPage::showContent(const QList<core::Song> &songs, const QString &title, const QString &meta,
                               qint64 playingId, bool removable)
{
    m_songs = songs;
    m_playingId = playingId;
    m_title->setText(title);
    m_meta->setText(meta);
    if (songs.isEmpty())
        m_cover->setPixmap(CoverProvider::placeholder(title, 160, 10));
    else
        m_cover->setPixmap(CoverProvider::coverFor(songs.first(), 160, 10));
    m_view->setSongs(songs, playingId);
    m_view->setRemovable(removable);
}

QList<core::Song> SongListPage::currentSongs() const
{
    return m_songs;
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
    m_view->setSongs(m_songs, m_playingId);
    m_cover->setPixmap(m_songs.isEmpty()
                           ? CoverProvider::placeholder(m_title->text(), 160, 10)
                           : CoverProvider::coverFor(m_songs.first(), 160, 10));
}

void SongListPage::setPlaylistContext(int playlistId)
{
    m_playlistContext = playlistId;
    delete m_moreBtn->menu();
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
}

void SongListPage::setPlaylistMenuItems(const QList<QPair<int, QString>> &items)
{
    m_view->setPlaylistMenuItems(items);
}

} // namespace ui
