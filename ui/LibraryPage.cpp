#include "LibraryPage.h"

#include "core/SearchService.h"
#include "ui/CoverCard.h"
#include "ui/CoverProvider.h"
#include "ui/SongListView.h"

#include <QButtonGroup>
#include <QFileInfo>
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

    auto *titleRow = new QWidget(this);
    auto *titleLayout = new QHBoxLayout(titleRow);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(8);
    auto *title = new QLabel(QStringLiteral("本地歌单"), titleRow);
    title->setProperty("class", "pageTitle");
    titleLayout->addWidget(title, 1);
    auto *importBtn = addTopButton(QStringLiteral("导入文件夹"), QStringLiteral(":/icons/icon-folder.svg"));
    connect(importBtn, &QPushButton::clicked, this, &LibraryPage::importRequested);
    auto *importFileBtn = addTopButton(QStringLiteral("导入歌曲"), QStringLiteral(":/icons/icon-plus.svg"));
    connect(importFileBtn, &QPushButton::clicked, this, &LibraryPage::importFilesRequested);
    titleLayout->addWidget(importBtn);
    titleLayout->addWidget(importFileBtn);
    layout->addWidget(titleRow);

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
    group->button(0)->setChecked(true);
    tabLayout->addStretch(1);
    layout->addWidget(tabRow);

    auto *filterRow = new QWidget(this);
    auto *filterLayout = new QHBoxLayout(filterRow);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setSpacing(8);
    auto *filterGroup = new QButtonGroup(this);
    filterGroup->setExclusive(true);
    const QStringList filters = { QStringLiteral("全部"), QStringLiteral("本地导入"),
                                  QStringLiteral("已缓存"), QStringLiteral("已下载") };
    for (int i = 0; i < filters.size(); ++i) {
        auto *btn = new QPushButton(filters[i], filterRow);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton{border:none;background:rgba(255,255,255,0.06);color:#9A9AA5;"
            "font-size:12px;padding:4px 14px;border-radius:999px;}"
            "QPushButton:hover{background:rgba(255,255,255,0.1);color:#E8E8E8;}"
            "QPushButton:checked{background:rgba(236,65,65,0.18);color:#EC4141;font-weight:600;}"));
        filterGroup->addButton(btn, i);
        filterLayout->addWidget(btn);
    }
    filterGroup->button(0)->setChecked(true);
    filterLayout->addStretch(1);
    layout->addWidget(filterRow);
    connect(filterGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_filter = id;
        m_songList->setDownloadActionMode(id == 3 ? SongListView::DeleteDownloadAction
                                                  : SongListView::DownloadAction);
        applyFilter();
        rebuildArtists();
        rebuildAlbums();
    });

    m_stack = new QStackedWidget(this);

    m_songList = new SongListView;
    m_stack->addWidget(m_songList);

    auto *artistPage = new QWidget;
    auto *artistLayout = new QVBoxLayout(artistPage);
    artistLayout->setContentsMargins(0, 0, 0, 0);
    auto *artistScroll = new QScrollArea(artistPage);
    artistScroll->setWidgetResizable(true);
    artistScroll->setFrameShape(QFrame::NoFrame);
    artistScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
    albumScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
        // 歌曲列表可能经过“本地与缓存/在线/已缓存”过滤，行号必须对应当前视图。
        emit playRequested(m_filtered, row);
    });
    connect(m_songList, &SongListView::heartRequested, this, &LibraryPage::heartRequested);
    connect(m_songList, &SongListView::addToPlaylistRequested, this, &LibraryPage::addToPlaylistRequested);
    connect(m_songList, &SongListView::removeFromPlaylistRequested, this, &LibraryPage::removeFromPlaylistRequested);
    connect(m_songList, &SongListView::deleteFromLibraryRequested, this, &LibraryPage::deleteFromLibraryRequested);
}

QPushButton *LibraryPage::addTopButton(const QString &text, const QString &icon)
{
    auto *btn = new QPushButton(text, this);
    btn->setIcon(QIcon(icon));
    btn->setIconSize(QSize(16, 16));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;background:rgba(255,255,255,0.06);color:#9A9AA5;"
        "padding:5px 14px;border-radius:999px;font-size:12px;}"
        "QPushButton:hover{background:rgba(236,65,65,0.16);color:#EC4141;}"));
    return btn;
}

void LibraryPage::setSongs(const QList<core::Song> &songs, qint64 playingId)
{
    m_songs = songs;
    m_playingId = playingId;
    applyFilter();
    rebuildArtists();
    rebuildAlbums();
}

QList<core::Song> LibraryPage::currentSongs() const
{
    return m_filtered;
}

void LibraryPage::setPlaylistMenuItems(const QList<QPair<int, QString>> &items)
{
    m_songList->setPlaylistMenuItems(items);
}

void LibraryPage::applyFilter()
{
    m_filtered.clear();
    for (const core::Song &s : m_songs) {
        const bool online = s.isOnline();
        switch (m_filter) {
        case 0: m_filtered.append(s); break;
        case 1: if (!online) m_filtered.append(s); break;
        case 2: if (online && s.isCached() && !s.isDownloaded()) m_filtered.append(s); break;
        case 3: if (online && s.isDownloaded()) m_filtered.append(s); break;
        default: break;
        }
    }
    m_songList->setSongs(m_filtered, m_playingId);
}

void LibraryPage::rebuildArtists()
{
    while (QLayoutItem *item = m_artistGrid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto artists = core::SearchService::artists(m_filtered);
    for (int i = 0; i < artists.size(); ++i) {
        const auto &a = artists[i];
        auto *card = new CoverCard;
        card->setFixedCardSize(120, 120);
        QPixmap cover = a.coverPath.isEmpty() ? QPixmap() : QPixmap(a.coverPath);
        if (cover.isNull())
            cover = CoverProvider::placeholder(a.name, 120, 60);
        else
            cover = cover.scaled(120, 120, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        card->setCover(cover);
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
    const auto albums = core::SearchService::albums(m_filtered);
    for (int i = 0; i < albums.size(); ++i) {
        const auto &a = albums[i];
        auto *card = new CoverCard;
        QPixmap cover = a.coverPath.isEmpty() ? QPixmap() : QPixmap(a.coverPath);
        if (cover.isNull())
            cover = CoverProvider::placeholder(a.name, 150, 8);
        else
            cover = cover.scaled(150, 150, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        card->setCover(cover);
        card->setText(a.name, a.artist);
        connect(card, &CoverCard::clicked, this, [this, name = a.name, artist = a.artist] {
            emit albumClicked(name, artist);
        });
        m_albumGrid->addWidget(card, i / 5, i % 5);
    }
    m_albumGrid->setColumnStretch(5, 1);
}

} // namespace ui
