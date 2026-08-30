#include "SearchPage.h"

#include "core/SearchService.h"
#include "ui/SongListView.h"

#include "core/LibraryService.h"
#include "core/MusicSourceRegistry.h"

#include <QButtonGroup>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>
#include <utility>

namespace ui {
namespace {

QPushButton *makeResultRow(const QString &text, const QString &sub, QWidget *parent)
{
    auto *btn = new QPushButton(parent);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#1B1B24;border:none;border-radius:6px;"
        "padding:12px 14px;text-align:left;color:#E8E8E8;}"
        "QPushButton:hover{background:#2A2A36;}"));
    btn->setText(sub.isEmpty() ? text : QStringLiteral("%1    %2").arg(text, sub));
    return btn;
}

QString sourceLabel(core::SourceId source)
{
    switch (source) {
    case core::SourceId::Netease: return QStringLiteral("网易云");
    case core::SourceId::QqMusic: return QStringLiteral("QQ音乐");
    case core::SourceId::Local: return QStringLiteral("本地");
    }
    return QStringLiteral("未知来源");
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
            "QPushButton{border:none;background:#0E0E14;color:#9A9AA5;font-size:14px;"
            "padding:7px 16px;border-radius:999px;}"
            "QPushButton:hover{background:#1B1B24;color:#E8E8E8;}"
            "QPushButton:checked{background:#3A2024;color:#EC4141;font-weight:600;}"));
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
        "QPushButton{border:none;background:#1B1B24;color:#C8C8D0;"
        "padding:7px 16px;border-radius:14px;}"
        "QPushButton:hover{background:#3A2024;color:#EC4141;}"));
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
    if (source)
        m_enabledSourceIds.insert(int(source->sourceId()));
}

void SearchPage::setSourceRegistry(core::MusicSourceRegistry *registry)
{
    m_registry = registry;
}

void SearchPage::setOnlineSourceEnabled(core::SourceId sourceId, bool enabled)
{
    if (enabled)
        m_enabledSourceIds.insert(int(sourceId));
    else
        m_enabledSourceIds.remove(int(sourceId));
}

void SearchPage::performSearch(const QList<core::Song> &allSongs, const QString &query)
{
    cancelActiveSearch();
    m_query = query.trimmed();
    ++m_searchGeneration;
    m_onlineOffset = 0;
    m_onlineLoading = false;
    m_albumCoverLookups.clear();
    m_sourceStates.clear();
    m_onlineSongs.clear();
    m_onlineList->setSongs({});
    m_onlineHeader->hide();
    m_onlineMore->hide();
    refreshLocalResults(allSongs);

    // 在线结果
    if ((m_source || m_registry) && m_lib && !m_query.isEmpty())
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
    if (m_query.isEmpty()) {
        m_results.clear();
        m_songList->setSongs({});
        return;
    }
    m_results.clear();
    const auto results = core::SearchService::search(allSongs, m_query);
    for (const auto &r : results)
        m_results.append(allSongs[r.index]);
    m_title->setText(QStringLiteral("搜索 \"%1\" · %2 个结果").arg(m_query).arg(m_results.size()));
    m_songList->setSongs(m_results);
    m_songList->setHighlightQuery(m_query);
}

void SearchPage::cancelActiveSearch()
{
    if (m_searchGeneration == 0)
        return;
    const QList<core::MusicSource *> sources = m_registry ? m_registry->onlineSources()
                                                          : QList<core::MusicSource *>{ m_source };
    for (core::MusicSource *source : sources) {
        if (source)
            source->cancelSearch(m_searchGeneration);
    }
    for (auto it = m_sourceStates.begin(); it != m_sourceStates.end(); ++it) {
        if (it->generation == m_searchGeneration
            && it->state == core::SearchLoadState::Loading) {
            it->state = core::SearchLoadState::Cancelled;
        }
    }
}

void SearchPage::updateOnlineHeader()
{
    QStringList parts;
    const QList<core::SourceId> order = {
        core::SourceId::Netease,
        core::SourceId::QqMusic
    };
    for (core::SourceId sourceId : order) {
        const auto it = m_sourceStates.constFind(int(sourceId));
        if (it == m_sourceStates.constEnd())
            continue;
        int count = 0;
        for (const core::Song &song : std::as_const(m_onlineSongs)) {
            if (song.sourceId() == sourceId)
                ++count;
        }
        QString status;
        switch (it->state) {
        case core::SearchLoadState::Loading:
            status = QStringLiteral("加载中…");
            break;
        case core::SearchLoadState::Ready:
            status = QStringLiteral("%1 首").arg(count);
            break;
        case core::SearchLoadState::Failed:
            status = it->error.isEmpty() ? QStringLiteral("失败")
                                         : QStringLiteral("失败：%1").arg(it->error);
            break;
        case core::SearchLoadState::TimedOut:
            status = QStringLiteral("超时");
            break;
        case core::SearchLoadState::Cancelled:
            status = QStringLiteral("已取消");
            break;
        case core::SearchLoadState::Idle:
            status = QStringLiteral("等待");
            break;
        }
        parts.append(QStringLiteral("%1 %2").arg(sourceLabel(sourceId), status));
    }
    m_onlineHeader->setText(parts.isEmpty() ? QStringLiteral("暂无可用在线来源")
                                            : parts.join(QStringLiteral(" · ")));
    m_onlineHeader->setVisible(!m_query.isEmpty() && !m_sourceStates.isEmpty());
}

void SearchPage::loadOnlinePage(int offset)
{
    if ((!m_source && !m_registry) || !m_lib || m_query.isEmpty() || m_onlineLoading)
        return;
    const quint64 generation = m_searchGeneration;
    m_onlineLoading = true;
    m_onlineMore->setEnabled(false);
    m_onlineMore->setText(QStringLiteral("加载中…"));
    QList<core::MusicSource *> sources = m_registry ? m_registry->onlineSources()
                                                    : QList<core::MusicSource *>{ m_source };
    sources.removeAll(nullptr);
    for (auto it = sources.begin(); it != sources.end();) {
        if (!m_enabledSourceIds.contains(int((*it)->sourceId())))
            it = sources.erase(it);
        else
            ++it;
    }
    if (sources.isEmpty()) {
        m_onlineLoading = false;
        m_onlineHeader->setText(QStringLiteral("暂无可用在线来源"));
        m_onlineHeader->show();
        return;
    }
    auto pending = std::make_shared<int>(sources.size());
    auto hasMore = std::make_shared<bool>(false);
    const QPointer<SearchPage> guard(this);
    if (offset == 0)
        m_onlineSongs.clear();
    for (core::MusicSource *source : std::as_const(sources)) {
        core::SearchSourceState state;
        state.source = source->sourceId();
        state.state = core::SearchLoadState::Loading;
        state.generation = generation;
        state.offset = offset;
        m_sourceStates.insert(int(source->sourceId()), state);
    }
    updateOnlineHeader();

    const auto renderResults = [guard, generation, offset, hasMore](bool finished) {
        if (!guard || generation != guard->m_searchGeneration)
            return;
        guard->m_onlineList->setSongs(guard->m_onlineSongs);
        guard->updateOnlineHeader();
        if (finished) {
            guard->m_onlineOffset = offset;
            guard->m_onlineLoading = false;
            guard->m_onlineMore->setVisible(*hasMore);
            guard->m_onlineMore->setEnabled(true);
            guard->m_onlineMore->setText(QStringLiteral("加载更多"));
        }
        for (const core::Song &song : std::as_const(guard->m_onlineSongs))
            guard->ensureCover(song);
    };
    const auto finishSource = [guard, generation, pending, renderResults] {
        if (!guard || generation != guard->m_searchGeneration)
            return;
        const bool finished = --(*pending) == 0;
        // 每个来源独立完成时立即展示其结果；离线或响应慢的来源不能让
        // 已返回的网易云/QQ 结果一直停留在“加载中”的空页面。
        renderResults(finished);
    };
    for (core::MusicSource *source : sources) {
        core::SearchRequest request;
        request.keywords = m_query;
        request.category = core::SearchCategory::Songs;
        request.scope = source->sourceId() == core::SourceId::Netease
            ? core::SearchScope::Netease : core::SearchScope::QqMusic;
        request.limit = m_onlinePageSize;
        request.offset = offset;
        request.generation = generation;
        auto settled = std::make_shared<bool>(false);
        const int sourceKey = int(source->sourceId());
        QTimer::singleShot(m_sourceTimeoutMs, this,
                           [guard, source, sourceKey, generation, settled, finishSource] {
            if (!guard || generation != guard->m_searchGeneration || *settled)
                return;
            *settled = true;
            source->cancelSearch(generation);
            auto state = guard->m_sourceStates.value(sourceKey);
            state.state = core::SearchLoadState::TimedOut;
            state.error = QStringLiteral("请求超时");
            guard->m_sourceStates.insert(sourceKey, state);
            finishSource();
        });
        source->search(request,
                       [guard, sourceKey, generation, settled, hasMore, finishSource]
                       (const core::SearchResponse &response) {
        if (!guard || generation != guard->m_searchGeneration || *settled
            || response.generation != generation)
            return;
        *settled = true;
        *hasMore = *hasMore || response.hasMore;
        for (const core::SearchResultItem &item : response.items) {
            if (item.type != core::SearchItemType::Song)
                continue;
            const core::Song s0 = item.song;
            bool duplicate = false;
            for (const core::Song &existing : std::as_const(guard->m_onlineSongs)) {
                if (existing.stableIdentity() == s0.stableIdentity()) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
                continue;
            core::Song s = s0;
            const core::Song stored = guard->m_lib->songByRemoteId(
                s.source, s.effectiveRemoteId());
            if (stored.id > 0) {
                s.id = stored.id;
                s.coverPath = stored.coverPath;
                s.cachePath = stored.cachePath;
                s.downloadPath = stored.downloadPath;
                s.lyricPath = stored.lyricPath;
            }
            guard->m_onlineSongs.append(s);
        }
        auto state = guard->m_sourceStates.value(sourceKey);
        state.state = core::SearchLoadState::Ready;
        state.hasMore = response.hasMore;
        state.error.clear();
        guard->m_sourceStates.insert(sourceKey, state);
        finishSource();
    }, [guard, sourceKey, generation, settled, finishSource](const QString &message) {
        if (!guard || generation != guard->m_searchGeneration || *settled)
            return;
        *settled = true;
        auto state = guard->m_sourceStates.value(sourceKey);
        state.state = core::SearchLoadState::Failed;
        state.error = message;
        guard->m_sourceStates.insert(sourceKey, state);
        finishSource();
    });
    }
}

void SearchPage::refreshOnlineCovers()
{
    if (!m_lib || m_onlineSongs.isEmpty())
        return;
    bool changed = false;
    for (core::Song &song : m_onlineSongs) {
        core::Song stored = m_lib->songById(song.id);
        if (stored.id <= 0 && song.hasRemoteIdentity())
            stored = m_lib->songByRemoteId(song.source, song.effectiveRemoteId());
        if (stored.id > 0 && song.id != stored.id) {
            song.id = stored.id;
            song.cachePath = stored.cachePath;
            song.downloadPath = stored.downloadPath;
            song.lyricPath = stored.lyricPath;
            changed = true;
        }
        if (stored.id > 0 && stored.coverPath != song.coverPath) {
            song.coverPath = stored.coverPath;
            changed = true;
        }
    }
    if (changed)
        m_onlineList->setSongs(m_onlineSongs);
}

QList<core::Song> SearchPage::currentSongs() const
{
    // 本地与在线列表共用操作信号，必须按当前可见页返回对应数据。
    // 旧实现始终返回本地结果，在线歌曲收藏/加入歌单时会写入错误 id，
    // 本地结果为空时则完全不会持久化。
    return m_stack && m_stack->currentIndex() == 1 ? m_onlineSongs : m_results;
}

void SearchPage::setPlaylistMenuItems(const QList<QPair<int, QString>> &items)
{
    m_songList->setPlaylistMenuItems(items);
    m_onlineList->setPlaylistMenuItems(items);
}

void SearchPage::ensureCover(const core::Song &song)
{
    core::MusicSource *source = m_registry ? m_registry->sourceFor(song) : m_source;
    if (!song.isOnline() || !song.hasRemoteIdentity() || !source || !m_lib)
        return;
    const QString albumIdentity = QStringLiteral("%1:%2").arg(song.source).arg(song.effectiveAlbumRemoteId());
    if (song.coverUrl.isEmpty() && !song.effectiveAlbumRemoteId().isEmpty()
        && !m_albumCoverLookups.contains(albumIdentity)) {
        m_albumCoverLookups.insert(albumIdentity);
        const QString albumId = song.effectiveAlbumRemoteId();
        const quint64 generation = m_searchGeneration;
        const QPointer<SearchPage> guard(this);
        source->albumDetail(albumId, [guard, generation, albumId, sourceId = song.source]
                            (const QJsonObject &obj) {
            if (!guard || generation != guard->m_searchGeneration)
                return;
            const QString coverUrl = obj.value(QStringLiteral("album")).toObject()
                                         .value(QStringLiteral("picUrl")).toString();
            if (coverUrl.isEmpty())
                return;
            for (core::Song &item : guard->m_onlineSongs) {
                if (item.source != sourceId || item.effectiveAlbumRemoteId() != albumId || !item.coverUrl.isEmpty())
                    continue;
                item.coverUrl = coverUrl;
                guard->ensureCover(item);
            }
        });
        return;
    }
    if (song.coverUrl.isEmpty())
        return;
    const core::Song current = song.id > 0 ? m_lib->songById(song.id) : core::Song{};
    if (!current.coverPath.isEmpty()) {
        setOnlineCover(song.stableIdentity(), current.coverPath);
        return;
    }
    const QString path = m_lib->songCoverCachePath(song);
    if (QFileInfo::exists(path)) {
        setOnlineCover(song.stableIdentity(), path);
        if (song.id > 0)
            m_lib->setSongCoverPath(song.id, path);
        return;
    }
    const QString identity = song.stableIdentity();
    if (m_coverDownloads.contains(identity))
        return;
    m_coverDownloads.insert(identity);
    const QUrl url(song.coverUrl);
    const qint64 id = song.id;
    const QPointer<SearchPage> guard(this);
    source->downloadToFile(url, path, [guard, identity, id, path](bool ok) {
        if (!guard)
            return;
        guard->m_coverDownloads.remove(identity);
        if (ok && guard->m_lib) {
            guard->setOnlineCover(identity, path);
            if (id > 0)
                guard->m_lib->setSongCoverPath(id, path);
        }
    });
}

void SearchPage::setOnlineCover(const QString &stableIdentity, const QString &path)
{
    bool changed = false;
    for (core::Song &song : m_onlineSongs) {
        if (song.stableIdentity() == stableIdentity && song.coverPath != path) {
            song.coverPath = path;
            changed = true;
        }
    }
    if (changed)
        m_onlineList->setSongs(m_onlineSongs);
}

} // namespace ui
