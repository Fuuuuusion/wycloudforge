#include "FavoritesPage.h"

#include "ui/SongListView.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace ui {

FavoritesPage::FavoritesPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("收藏"), this);
    title->setProperty("class", "pageTitle");
    layout->addWidget(title);
    auto *hint = new QLabel(QStringLiteral("红心歌单 · 我喜欢的音乐"), this);
    hint->setProperty("class", "pageSub");
    layout->addWidget(hint);

    m_view = new SongListView(this);
    m_view->setRemovable(true);
    layout->addWidget(m_view, 1);

    connect(m_view, &SongListView::playRequested, this, [this](int row) {
        emit playRequested(m_view->songs(), row);
    });
    connect(m_view, &SongListView::heartRequested, this, [this](int row) {
        emit heartRequested(row);
    });
    connect(m_view, &SongListView::addToPlaylistRequested, this, [this](int row, int plId) {
        emit addToPlaylistRequested(row, plId);
    });
    connect(m_view, &SongListView::removeFromPlaylistRequested, this, [this](int row) {
        emit removeFromPlaylistRequested(row);
    });
}

void FavoritesPage::setSongs(const QList<core::Song> &songs, qint64 playingId)
{
    m_view->setSongs(songs, playingId);
}

void FavoritesPage::setPlayingId(qint64 playingId)
{
    m_view->setPlayingId(playingId);
}

QList<core::Song> FavoritesPage::currentSongs() const
{
    return m_view->songs();
}

} // namespace ui
