#include "LibraryPage.h"

#include "core/SearchService.h"
#include "ui/CoverCard.h"
#include "ui/CoverProvider.h"
#include "ui/SongListView.h"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace ui {

LibraryPage::LibraryPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("音乐库"), this);
    title->setProperty("class", "pageTitle");
    layout->addWidget(title);

    auto *tabRow = new QWidget(this);
    auto *tabLayout = new QHBoxLayout(tabRow);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(30);
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    const QStringList names = { QStringLiteral("歌曲"), QStringLiteral("歌手"), QStringLiteral("专辑") };
    for (int i = 0; i < names.size(); ++i) {
        auto *btn = new QPushButton(names[i], tabRow);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton{border:none;background:transparent;color:#9A9AA5;font-size:14px;"
            "padding:7px 16px;border-radius:999px;}"
            "QPushButton:hover{background:rgba(255,255,255,0.08);color:#E8E8E8;}"
            "QPushButton:checked{background:rgba(236,65,65,0.16);color:#EC4141;font-weight:600;}"));
        group->addButton(btn, i);
        tabLayout->addWidget(btn);
    }
    tabLayout->addStretch(1);
    layout->addWidget(tabRow);

    m_stack = new QStackedWidget(this);

    m_songList = new SongListView;
    m_stack->addWidget(m_songList);

    auto *artistPage = new QWidget;
    auto *artistLayout = new QVBoxLayout(artistPage);
    artistLayout->setContentsMargins(0, 0, 0, 0);
    auto *artistScroll = new QScrollArea(artistPage);
    artistScroll->setWidgetResizable(true);
    artistScroll->setFrameShape(QFrame::NoFrame);
    auto *artistContent = new QWidget;
    m_artistGrid = new QGridLayout(artistContent);
    m_artistGrid->setContentsMargins(0, 0, 0, 0);
    m_artistGrid->setSpacing(16);
    artistScroll->setWidget(artistContent);
    artistLayout->addWidget(artistScroll);
    m_stack->addWidget(artistPage);

    auto *albumPage = new QWidget;
    auto *albumLayout = new QVBoxLayout(albumPage);
    albumLayout->setContentsMargins(0, 0, 0, 0);
    auto *albumScroll = new QScrollArea(albumPage);
    albumScroll->setWidgetResizable(true);
    albumScroll->setFrameShape(QFrame::NoFrame);
    auto *albumContent = new QWidget;
    m_albumGrid = new QGridLayout(albumContent);
    m_albumGrid->setContentsMargins(0, 0, 0, 0);
    m_albumGrid->setSpacing(16);
    albumScroll->setWidget(albumContent);
    albumLayout->addWidget(albumScroll);
    m_stack->addWidget(albumPage);

    layout->addWidget(m_stack, 1);

    connect(group, &QButtonGroup::idClicked, m_stack, &QStackedWidget::setCurrentIndex);
    connect(m_songList, &SongListView::playRequested, this, [this](int row) {
        emit playRequested(m_songs, row);
    });
    connect(m_songList, &SongListView::heartRequested, this, &LibraryPage::heartRequested);
    connect(m_songList, &SongListView::addToPlaylistRequested, this, &LibraryPage::addToPlaylistRequested);
    connect(m_songList, &SongListView::removeFromPlaylistRequested, this, &LibraryPage::removeFromPlaylistRequested);
    connect(m_songList, &SongListView::deleteFromLibraryRequested, this, &LibraryPage::deleteFromLibraryRequested);
}

void LibraryPage::setSongs(const QList<core::Song> &songs, qint64 playingId)
{
    m_songs = songs;
    m_playingId = playingId;
    m_songList->setSongs(songs, playingId);
    rebuildArtists();
    rebuildAlbums();
}

QList<core::Song> LibraryPage::currentSongs() const
{
    return m_songs;
}

void LibraryPage::rebuildArtists()
{
    while (QLayoutItem *item = m_artistGrid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto artists = core::SearchService::artists(m_songs);
    for (int i = 0; i < artists.size(); ++i) {
        const auto &a = artists[i];
        auto *card = new CoverCard;
        card->setFixedCardSize(120, 120);
        card->setCover(CoverProvider::placeholder(a.name, 120, 60));
        card->setRound(true);
        card->setText(a.name, QStringLiteral("%1 首").arg(a.count));
        connect(card, &CoverCard::clicked, this, [this, name = a.name] {
            emit artistClicked(name);
        });
        m_artistGrid->addWidget(card, i / 5, i % 5);
    }
    m_artistGrid->setColumnStretch(5, 1);
}

void LibraryPage::rebuildAlbums()
{
    while (QLayoutItem *item = m_albumGrid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto albums = core::SearchService::albums(m_songs);
    for (int i = 0; i < albums.size(); ++i) {
        const auto &a = albums[i];
        auto *card = new CoverCard;
        card->setCover(CoverProvider::placeholder(a.name, 150, 8));
        card->setText(a.name, a.artist);
        connect(card, &CoverCard::clicked, this, [this, name = a.name, artist = a.artist] {
            emit albumClicked(name, artist);
        });
        m_albumGrid->addWidget(card, i / 5, i % 5);
    }
    m_albumGrid->setColumnStretch(5, 1);
}

} // namespace ui
