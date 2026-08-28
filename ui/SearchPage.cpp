#include "SearchPage.h"

#include "core/SearchService.h"
#include "ui/SongListView.h"

#include "core/LibraryService.h"

#include <QButtonGroup>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace ui {
namespace {

QPushButton *makeResultRow(const QString &text, const QString &sub, QWidget *parent)
{
    auto *btn = new QPushButton(parent);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(QStringLiteral(
        "QPushButton{background:rgba(255,255,255,0.05);border:none;border-radius:6px;"
        "padding:12px 14px;text-align:left;color:#E8E8E8;}"
        "QPushButton:hover{background:rgba(255,255,255,0.08);}"));
    btn->setText(sub.isEmpty() ? text : QStringLiteral("%1    %2").arg(text, sub));
    return btn;
}

} // namespace

SearchPage::SearchPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    m_title = new QLabel(QStringLiteral("搜索"), this);
    m_title->setProperty("class", "pageTitle");
    layout->addWidget(m_title);

    auto *tabRow = new QWidget(this);
    auto *tabLayout = new QHBoxLayout(tabRow);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(30);
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    const QStringList names = { QStringLiteral("单曲"), QStringLiteral("在线"), QStringLiteral("歌手"), QStringLiteral("专辑") };
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

    auto *onlinePage = new QWidget;
    auto *onlineLayout = new QVBoxLayout(onlinePage);
    onlineLayout->setContentsMargins(0, 0, 0, 0);
    onlineLayout->setSpacing(6);
    m_onlineHeader = new QLabel(onlinePage);
    m_onlineHeader->setStyleSheet(QStringLiteral("color:#6E6E7A;font-size:12px;"));
    m_onlineHeader->hide();
    m_onlineList = new SongListView;
    m_onlineMore = new QPushButton(QStringLiteral("加载更多"), onlinePage);
    m_onlineMore->setCursor(Qt::PointingHandCursor);
    m_onlineMore->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;background:rgba(255,255,255,0.06);color:#C8C8D0;"
        "padding:7px 16px;border-radius:14px;}"
        "QPushButton:hover{background:rgba(236,65,65,0.16);color:#EC4141;}"));
    m_onlineMore->hide();
    onlineLayout->addWidget(m_onlineHeader);
    onlineLayout->addWidget(m_onlineMore, 0, Qt::AlignLeft);
    onlineLayout->addWidget(m_onlineList, 1);
    m_stack->addWidget(onlinePage);

    auto *artistPage = new QWidget;
    auto *artistOuter = new QVBoxLayout(artistPage);
    artistOuter->setContentsMargins(0, 0, 0, 0);
    auto *artistScroll = new QScrollArea(artistPage);
    artistScroll->setWidgetResizable(true);
    artistScroll->setFrameShape(QFrame::NoFrame);
    artistScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *artistContent = new QWidget;
    m_artistLayout = new QVBoxLayout(artistContent);
    m_artistLayout->setContentsMargins(0, 0, 0, 0);
    m_artistLayout->setSpacing(6);
    artistScroll->setWidget(artistContent);
    artistOuter->addWidget(artistScroll);
    m_stack->addWidget(artistPage);

    auto *albumPage = new QWidget;
    auto *albumOuter = new QVBoxLayout(albumPage);
    albumOuter->setContentsMargins(0, 0, 0, 0);
    auto *albumScroll = new QScrollArea(albumPage);
    albumScroll->setWidgetResizable(true);
    albumScroll->setFrameShape(QFrame::NoFrame);
    albumScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *albumContent = new QWidget;
    m_albumLayout = new QVBoxLayout(albumContent);
    m_albumLayout->setContentsMargins(0, 0, 0, 0);
    m_albumLayout->setSpacing(6);
    albumScroll->setWidget(albumContent);
    albumOuter->addWidget(albumScroll);
    m_stack->addWidget(albumPage);

    layout->addWidget(m_stack, 1);
    connect(group, &QButtonGroup::idClicked, m_stack, &QStackedWidget::setCurrentIndex);

    connect(m_songList, &SongListView::playRequested, this, [this](int row) {
        emit playRequested(m_results, row);
    });
    connect(m_songList, &SongListView::heartRequested, this, &SearchPage::heartRequested);
    connect(m_songList, &SongListView::addToPlaylistRequested, this, &SearchPage::addToPlaylistRequested);
    connect(m_songList, &SongListView::removeFromPlaylistRequested, this, &SearchPage::removeFromPlaylistRequested);
    connect(m_songList, &SongListView::deleteFromLibraryRequested, this, &SearchPage::deleteFromLibraryRequested);
    connect(m_onlineList, &SongListView::playRequested, this, [this](int row) {
        emit playRequested(m_onlineSongs, row);
    });
    connect(m_onlineList, &SongListView::heartRequested, this, &SearchPage::heartRequested);
    connect(m_onlineList, &SongListView::addToPlaylistRequested, this, &SearchPage::addToPlaylistRequested);
    connect(m_onlineList, &SongListView::deleteFromLibraryRequested, this, &SearchPage::deleteFromLibraryRequested);
    connect(m_onlineMore, &QPushButton::clicked, this, [this] {
        if (!m_onlineLoading && !m_query.isEmpty())
            loadOnlinePage(m_onlineOffset + m_onlinePageSize);
    });
}

void SearchPage::setSourceProvider(core::MusicSource *source, core::LibraryService *library)
{
    m_source = source;
    m_lib = library;
}

void SearchPage::performSearch(const QList<core::Song> &allSongs, const QString &query)
{
    m_query = query.trimmed();
    ++m_searchGeneration;
    m_onlineOffset = 0;
    m_onlineLoading = false;
    m_albumCoverLookups.clear();
    m_onlineSongs.clear();
    m_onlineList->setSongs({});
    m_onlineHeader->hide();
    m_onlineMore->hide();
    refreshLocalResults(allSongs);

    // 在线结果
    if (m_source && m_lib && !m_query.isEmpty())
        loadOnlinePage(0);

    while (QLayoutItem *item = m_artistLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto artists = core::SearchService::artists(m_results);
    for (const auto &a : artists) {
        if (!a.name.contains(query, Qt::CaseInsensitive))
            continue;
        auto *row = makeResultRow(a.name, QStringLiteral("%1 首").arg(a.count), this);
        connect(row, &QPushButton::clicked, this, [this, name = a.name] {
            emit artistClicked(name);
        });
        m_artistLayout->addWidget(row);
    }
    m_artistLayout->addStretch(1);

    while (QLayoutItem *item = m_albumLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto albums = core::SearchService::albums(m_results);
    for (const auto &a : albums) {
        if (!a.name.contains(query, Qt::CaseInsensitive) && !a.artist.contains(query, Qt::CaseInsensitive))
            continue;
        auto *row = makeResultRow(a.name, a.artist + QStringLiteral(" · %1 首").arg(a.count), this);
        connect(row, &QPushButton::clicked, this, [this, name = a.name, artist = a.artist] {
            emit albumClicked(name, artist);
        });
        m_albumLayout->addWidget(row);
    }
    m_albumLayout->addStretch(1);
}

void SearchPage::refreshLocalResults(const QList<core::Song> &allSongs)
{
    if (m_query.isEmpty())
        return;
    m_results.clear();
    const auto results = core::SearchService::search(allSongs, m_query);
    for (const auto &r : results)
        m_results.append(allSongs[r.index]);
    m_title->setText(QStringLiteral("搜索 \"%1\" · %2 个结果").arg(m_query).arg(m_results.size()));
    m_songList->setSongs(m_results);
    m_songList->setHighlightQuery(m_query);
}

void SearchPage::loadOnlinePage(int offset)
{
    if (!m_source || !m_lib || m_query.isEmpty() || m_onlineLoading)
        return;
    const int generation = m_searchGeneration;
    m_onlineLoading = true;
    m_onlineMore->setEnabled(false);
    m_onlineMore->setText(QStringLiteral("加载中…"));
    m_source->searchSongsPage(m_query, m_onlinePageSize, offset,
                              [this, generation, offset](const QJsonArray &arr) {
        if (generation != m_searchGeneration)
            return;
        if (offset == 0)
            m_onlineSongs.clear();
        for (const QJsonValue &v : arr) {
            const core::Song s0 = m_source->songFromJson(v.toObject());
            bool duplicate = false;
            for (const core::Song &existing : m_onlineSongs) {
                if (existing.onlineId == s0.onlineId) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
                continue;
            core::Song s = s0;
            s.id = m_lib->upsertOnlineSong(s);
            if (s.id > 0) {
                const core::Song stored = m_lib->songById(s.id);
                s.coverPath = stored.coverPath;
                s.cachePath = stored.cachePath;
                s.lyricPath = stored.lyricPath;
            }
            m_onlineSongs.append(s);
        }
        m_onlineOffset = offset;
        m_onlineLoading = false;
        m_onlineList->setSongs(m_onlineSongs);
        m_onlineHeader->setText(QStringLiteral("在线结果(%1) · %2 首")
                                    .arg(m_source->sourceName()).arg(m_onlineSongs.size()));
        m_onlineHeader->setVisible(!m_onlineSongs.isEmpty());
        m_onlineMore->setVisible(arr.size() >= m_onlinePageSize);
        m_onlineMore->setEnabled(true);
        m_onlineMore->setText(QStringLiteral("加载更多"));
        for (const core::Song &s : m_onlineSongs)
            ensureCover(s);
    }, [this, generation](const QString &) {
        if (generation != m_searchGeneration)
            return;
        m_onlineLoading = false;
        m_onlineMore->setEnabled(true);
        m_onlineMore->setText(QStringLiteral("加载更多"));
    });
}

void SearchPage::refreshOnlineCovers()
{
    if (!m_lib || m_onlineSongs.isEmpty())
        return;
    bool changed = false;
    for (core::Song &song : m_onlineSongs) {
        const core::Song stored = m_lib->songById(song.id);
        if (stored.coverPath != song.coverPath) {
            song.coverPath = stored.coverPath;
            changed = true;
        }
    }
    if (changed)
        m_onlineList->setSongs(m_onlineSongs);
}

void SearchPage::ensureCover(const core::Song &song)
{
    if (!song.isOnline() || song.id <= 0 || !m_source || !m_lib)
        return;
    if (song.coverUrl.isEmpty() && song.albumId > 0 && !m_albumCoverLookups.contains(song.albumId)) {
        m_albumCoverLookups.insert(song.albumId);
        const qint64 albumId = song.albumId;
        m_source->albumDetail(albumId, [this, albumId](const QJsonObject &obj) {
            const QString coverUrl = obj.value(QStringLiteral("album")).toObject()
                                         .value(QStringLiteral("picUrl")).toString();
            if (coverUrl.isEmpty())
                return;
            for (core::Song &item : m_onlineSongs) {
                if (item.albumId != albumId || !item.coverUrl.isEmpty())
                    continue;
                item.coverUrl = coverUrl;
                m_lib->upsertOnlineSong(item);
                ensureCover(item);
            }
        });
        return;
    }
    if (song.coverUrl.isEmpty())
        return;
    const core::Song current = m_lib->songById(song.id);
    if (!current.coverPath.isEmpty()) {
        setOnlineCover(song.id, current.coverPath);
        return;
    }
    const QString path = m_lib->songCoverCachePath(song);
    if (QFileInfo::exists(path)) {
        m_lib->setSongCoverPath(song.id, path);
        return;
    }
    const QUrl url(song.coverUrl);
    const qint64 id = song.id;
    m_source->downloadToFile(url, path, [this, id, path](bool ok) {
        if (ok && m_lib) {
            setOnlineCover(id, path);
            m_lib->setSongCoverPath(id, path);
        }
    });
}

void SearchPage::setOnlineCover(qint64 songId, const QString &path)
{
    bool changed = false;
    for (core::Song &song : m_onlineSongs) {
        if (song.id == songId && song.coverPath != path) {
            song.coverPath = path;
            changed = true;
        }
    }
    if (changed)
        m_onlineList->setSongs(m_onlineSongs);
}

} // namespace ui
