#include "SearchPage.h"

#include "ui/ThemeManager.h"

#include "core/SearchCache.h"
#include "core/SearchService.h"
#include "ui/SongListView.h"

#include "core/LibraryService.h"
#include "core/MusicSourceRegistry.h"

#include <QButtonGroup>
#include <QColor>
#include <QComboBox>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>
#include <utility>

namespace ui {
namespace {

QPushButton *makeResultRow(const QString &text, const QString &sub, QWidget *parent)
{
    auto *btn = new QPushButton(parent);
    btn->setObjectName(QStringLiteral("searchResultRow"));
    btn->setCursor(Qt::PointingHandCursor);
    setThemedStyleSheet(btn, QStringLiteral(
        "QPushButton{background:@surfaceAlt;border:none;border-radius:6px;"
        "padding:12px 14px;text-align:left;color:@textPrimary;}"
        "QPushButton:hover{background:@surfaceHover;}"));
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

QString itemTypeLabel(core::SearchItemType type)
{
    switch (type) {
    case core::SearchItemType::Song: return QStringLiteral("单曲");
    case core::SearchItemType::Artist: return QStringLiteral("歌手");
    case core::SearchItemType::Album: return QStringLiteral("专辑");
    case core::SearchItemType::Playlist: return QStringLiteral("歌单");
    case core::SearchItemType::Lyric: return QStringLiteral("歌词");
    }
    return QStringLiteral("结果");
}

core::SearchResultItem localSongItem(const core::Song &song, int rank)
{
    core::SearchResultItem item;
    item.type = core::SearchItemType::Song;
    item.source = core::SourceId::Local;
    item.title = song.title;
    item.subtitle = song.artist;
    item.artist = song.artist;
    item.album = song.album;
    item.durationMs = song.durationMs;
    item.sourceRank = rank;
    item.song = song;
    return item;
}

constexpr int kLocalSongsPage = 0;
constexpr int kOnlineSongsPage = 1;
constexpr int kLocalArtistsPage = 2;
constexpr int kLocalAlbumsPage = 3;
constexpr int kGenericResultsPage = 4;
constexpr int kAssistantPage = 5;

} // namespace

SearchPage::SearchPage(QWidget *parent)
    : QWidget(parent)
{
    m_localSearch = new core::SearchService(this);
    m_searchCache = new core::SearchCache;
    m_suggestionTimer = new QTimer(this);
    m_suggestionTimer->setSingleShot(true);
    m_suggestionTimer->setInterval(280);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    auto *titleRow = new QHBoxLayout;
    auto *back = new QPushButton(QStringLiteral("返回"), this);
    back->setFixedSize(68, 32);
    back->setCursor(Qt::PointingHandCursor);
    setThemedStyleSheet(back, QStringLiteral(
        "QPushButton{border:none;background:@surfaceAlt;color:@textSecondary;border-radius:16px;}"
        "QPushButton:hover{background:@accentSoft;color:@accent;}"));
    m_title = new QLabel(QStringLiteral("搜索"), this);
    m_title->setProperty("class", "pageTitle");
    titleRow->addWidget(back);
    titleRow->addSpacing(12);
    titleRow->addWidget(m_title);
    titleRow->addStretch(1);
    layout->addLayout(titleRow);
    connect(back, &QPushButton::clicked, this, &SearchPage::backRequested);

    auto *filterRow = new QWidget(this);
    auto *filterLayout = new QHBoxLayout(filterRow);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setSpacing(8);
    m_scopeCombo = new QComboBox(filterRow);
    m_scopeCombo->addItem(QStringLiteral("综合"), int(core::SearchScope::All));
    m_scopeCombo->addItem(QStringLiteral("本地"), int(core::SearchScope::Local));
    m_scopeCombo->addItem(QStringLiteral("网易云"), int(core::SearchScope::Netease));
    m_scopeCombo->addItem(QStringLiteral("QQ音乐"), int(core::SearchScope::QqMusic));
    m_categoryCombo = new QComboBox(filterRow);
    m_categoryCombo->addItem(QStringLiteral("综合分类"), int(core::SearchCategory::All));
    m_categoryCombo->addItem(QStringLiteral("单曲"), int(core::SearchCategory::Songs));
    m_categoryCombo->addItem(QStringLiteral("歌手"), int(core::SearchCategory::Artists));
    m_categoryCombo->addItem(QStringLiteral("专辑"), int(core::SearchCategory::Albums));
    m_categoryCombo->addItem(QStringLiteral("歌单"), int(core::SearchCategory::Playlists));
    m_categoryCombo->addItem(QStringLiteral("歌词"), int(core::SearchCategory::Lyrics));
    m_categoryCombo->setCurrentIndex(1);
    m_sortCombo = new QComboBox(filterRow);
    m_sortCombo->addItem(QStringLiteral("综合排序"), int(core::SearchSortMode::Comprehensive));
    m_sortCombo->addItem(QStringLiteral("热度"), int(core::SearchSortMode::Popularity));
    m_sortCombo->addItem(QStringLiteral("最近播放"), int(core::SearchSortMode::RecentPlayed));
    m_sortCombo->addItem(QStringLiteral("本地优先"), int(core::SearchSortMode::LocalFirst));
    filterLayout->addWidget(new QLabel(QStringLiteral("范围"), filterRow));
    filterLayout->addWidget(m_scopeCombo);
    filterLayout->addWidget(new QLabel(QStringLiteral("分类"), filterRow));
    filterLayout->addWidget(m_categoryCombo);
    filterLayout->addWidget(new QLabel(QStringLiteral("排序"), filterRow));
    filterLayout->addWidget(m_sortCombo);
    filterLayout->addStretch(1);
    layout->addWidget(filterRow);

    auto *tabRow = new QWidget(this);
    m_legacyTabs = tabRow;
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
        setThemedStyleSheet(btn, QStringLiteral(
            "QPushButton{border:none;background:@pageBackground;color:@textSecondary;font-size:14px;"
            "padding:7px 16px;border-radius:999px;}"
            "QPushButton:hover{background:@surfaceAlt;color:@textPrimary;}"
            "QPushButton:checked{background:@accentSoft;color:@accent;font-weight:600;}"));
        group->addButton(btn, i);
        tabLayout->addWidget(btn);
    }
    tabLayout->addStretch(1);
    layout->addWidget(tabRow);
    tabRow->hide();

    m_stack = new QStackedWidget(this);
    m_songList = new SongListView;
    m_stack->addWidget(m_songList);

    auto *onlinePage = new QWidget;
    auto *onlineLayout = new QVBoxLayout(onlinePage);
    onlineLayout->setContentsMargins(0, 0, 0, 0);
    onlineLayout->setSpacing(6);
    m_onlineHeader = new QLabel(onlinePage);
    setThemedStyleSheet(m_onlineHeader, QStringLiteral("color:@textTertiary;font-size:12px;"));
    m_onlineHeader->hide();
    auto *retryRow = new QWidget(onlinePage);
    auto *retryLayout = new QHBoxLayout(retryRow);
    retryLayout->setContentsMargins(0, 0, 0, 0);
    retryLayout->setSpacing(6);
    m_retryNetease = new QPushButton(QStringLiteral("重试网易云"), retryRow);
    m_retryQq = new QPushButton(QStringLiteral("重试QQ音乐"), retryRow);
    m_retryNetease->hide();
    m_retryQq->hide();
    retryLayout->addWidget(m_retryNetease);
    retryLayout->addWidget(m_retryQq);
    retryLayout->addStretch(1);
    m_onlineList = new SongListView;
    onlineLayout->addWidget(m_onlineHeader);
    onlineLayout->addWidget(retryRow);
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

    auto *genericPage = new QWidget;
    auto *genericOuter = new QVBoxLayout(genericPage);
    genericOuter->setContentsMargins(0, 0, 0, 0);
    auto *genericScroll = new QScrollArea(genericPage);
    genericScroll->setWidgetResizable(true);
    genericScroll->setFrameShape(QFrame::NoFrame);
    genericScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *genericContent = new QWidget;
    m_genericLayout = new QVBoxLayout(genericContent);
    m_genericLayout->setContentsMargins(0, 0, 0, 0);
    m_genericLayout->setSpacing(6);
    genericScroll->setWidget(genericContent);
    genericOuter->addWidget(genericScroll, 1);
    m_genericSongsHeader = new QLabel(QStringLiteral("单曲"), genericPage);
    setThemedStyleSheet(m_genericSongsHeader, QStringLiteral(
        "color:@textPrimary;font-size:14px;font-weight:600;"));
    m_genericSongList = new SongListView(genericPage);
    m_genericSongsHeader->hide();
    m_genericSongList->hide();
    genericOuter->addWidget(m_genericSongsHeader);
    genericOuter->addWidget(m_genericSongList, 2);
    m_stack->addWidget(genericPage);

    auto *assistantPage = new QWidget;
    auto *assistantLayout = new QVBoxLayout(assistantPage);
    assistantLayout->setContentsMargins(0, 0, 0, 0);
    assistantLayout->setSpacing(14);
    auto *assistantTop = new QWidget(assistantPage);
    auto *assistantTopLayout = new QHBoxLayout(assistantTop);
    assistantTopLayout->setContentsMargins(0, 0, 0, 0);
    m_assistantHeader = new QLabel(QStringLiteral("搜索历史"), assistantTop);
    m_clearHistory = new QPushButton(QStringLiteral("清空历史"), assistantTop);
    assistantTopLayout->addWidget(m_assistantHeader);
    assistantTopLayout->addStretch(1);
    assistantTopLayout->addWidget(m_clearHistory);
    m_discoveryPanel = new QWidget(assistantPage);
    auto *discoveryLayout = new QVBoxLayout(m_discoveryPanel);
    discoveryLayout->setContentsMargins(0, 0, 0, 0);
    discoveryLayout->setSpacing(18);
    m_historyList = new QListWidget(m_discoveryPanel);
    m_historyList->setObjectName(QStringLiteral("searchHistoryList"));
    m_historyList->setMaximumHeight(150);
    discoveryLayout->addWidget(m_historyList);

    auto *hotColumns = new QWidget(m_discoveryPanel);
    auto *hotColumnsLayout = new QHBoxLayout(hotColumns);
    hotColumnsLayout->setContentsMargins(0, 0, 0, 0);
    hotColumnsLayout->setSpacing(24);
    auto makeHotColumn = [hotColumns](const QString &title, const QString &objectName,
                                      const QString &retryObjectName,
                                      QListWidget **list, QPushButton **retryButton) {
        auto *column = new QWidget(hotColumns);
        auto *columnLayout = new QVBoxLayout(column);
        columnLayout->setContentsMargins(0, 0, 0, 0);
        columnLayout->setSpacing(12);
        auto *header = new QWidget(column);
        auto *headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(8);
        auto *label = new QLabel(title, header);
        setThemedStyleSheet(label, QStringLiteral(
            "color:@textPrimary;font-size:15px;font-weight:600;background:transparent;"));
        *retryButton = new QPushButton(QStringLiteral("重试"), header);
        (*retryButton)->setObjectName(retryObjectName);
        (*retryButton)->setCursor(Qt::PointingHandCursor);
        setThemedStyleSheet(*retryButton, QStringLiteral(
            "QPushButton{border:none;background:transparent;color:@accent;padding:2px 4px;}"
            "QPushButton:hover{background:transparent;color:@accentHover;text-decoration:underline;}"
            "QPushButton:pressed{background:transparent;color:@accentPressed;}"));
        (*retryButton)->hide();
        *list = new QListWidget(column);
        (*list)->setObjectName(objectName);
        headerLayout->addWidget(label);
        headerLayout->addStretch(1);
        headerLayout->addWidget(*retryButton);
        columnLayout->addWidget(header);
        columnLayout->addWidget(*list, 1);
        return column;
    };
    hotColumnsLayout->addWidget(makeHotColumn(QStringLiteral("网易云音乐热搜"),
                                               QStringLiteral("neteaseHotSearchList"),
                                               QStringLiteral("neteaseHotSearchRetry"),
                                               &m_neteaseHotList, &m_hotRetryNetease), 1);
    hotColumnsLayout->addWidget(makeHotColumn(QStringLiteral("QQ 音乐热搜"),
                                               QStringLiteral("qqHotSearchList"),
                                               QStringLiteral("qqHotSearchRetry"),
                                               &m_qqHotList, &m_hotRetryQq), 1);
    discoveryLayout->addWidget(hotColumns, 1);

    m_assistantList = new QListWidget(assistantPage);
    m_assistantList->setObjectName(QStringLiteral("searchAssistantList"));
    const QList<QListWidget *> assistantLists = {
        m_historyList, m_neteaseHotList, m_qqHotList, m_assistantList
    };
    for (QListWidget *list : assistantLists) {
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setFrameShape(QFrame::NoFrame);
        list->setSelectionMode(QAbstractItemView::NoSelection);
        list->setFocusPolicy(Qt::NoFocus);
        list->setMouseTracking(true);
        setThemedStyleSheet(list, QStringLiteral(
            "QListWidget{background:transparent;border:none;}"
            "QListWidget::item{padding:7px 2px;color:@textPrimary;background:transparent;}"
            "QListWidget::item:hover{background:transparent;color:@accentHover;}"
            "QListWidget::item:selected{background:transparent;color:@accent;}"));
    }
    assistantLayout->addWidget(assistantTop);
    assistantLayout->addWidget(m_discoveryPanel, 1);
    assistantLayout->addWidget(m_assistantList, 1);
    m_stack->addWidget(assistantPage);

    layout->addWidget(m_stack, 1);
    connect(group, &QButtonGroup::idClicked, m_stack, &QStackedWidget::setCurrentIndex);

    connect(m_scopeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        setSearchScope(static_cast<core::SearchScope>(m_scopeCombo->itemData(index).toInt()));
    });
    connect(m_categoryCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        setSearchCategory(static_cast<core::SearchCategory>(m_categoryCombo->itemData(index).toInt()));
    });
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        setSortMode(static_cast<core::SearchSortMode>(m_sortCombo->itemData(index).toInt()));
    });

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
    connect(m_onlineList, &SongListView::sourceActivated,
            this, [this](int row, const core::Song &song) {
        if (row >= 0 && row < m_onlineSongs.size())
            m_onlineSongs[row] = song;
    });
    connect(m_onlineList, &SongListView::heartRequested, this, &SearchPage::heartRequested);
    connect(m_onlineList, &SongListView::addToPlaylistRequested, this, &SearchPage::addToPlaylistRequested);
    connect(m_onlineList, &SongListView::deleteFromLibraryRequested, this, &SearchPage::deleteFromLibraryRequested);
    connect(m_genericSongList, &SongListView::playRequested, this, [this](int row) {
        emit playRequested(m_genericSongs, row);
    });
    connect(m_genericSongList, &SongListView::sourceActivated,
            this, [this](int row, const core::Song &song) {
        if (row >= 0 && row < m_genericSongs.size())
            m_genericSongs[row] = song;
    });
    connect(m_genericSongList, &SongListView::heartRequested,
            this, &SearchPage::heartRequested);
    connect(m_genericSongList, &SongListView::addToPlaylistRequested,
            this, &SearchPage::addToPlaylistRequested);
    connect(m_genericSongList, &SongListView::deleteFromLibraryRequested,
            this, &SearchPage::deleteFromLibraryRequested);
    connect(m_onlineList->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int value) {
        QScrollBar *bar = m_onlineList->verticalScrollBar();
        if (bar && bar->maximum() - value <= qMax(bar->pageStep(), 160))
            requestNextOnlinePages();
    });
    connect(m_onlineList->verticalScrollBar(), &QScrollBar::rangeChanged,
            this, [this](int minimum, int maximum) {
        if (minimum == maximum && maximum == 0)
            QTimer::singleShot(0, this, &SearchPage::requestNextOnlinePages);
    });
    connect(m_genericSongList->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int value) {
        QScrollBar *bar = m_genericSongList->verticalScrollBar();
        if (bar && bar->maximum() - value <= qMax(bar->pageStep(), 160))
            requestNextOnlinePages();
    });
    connect(m_retryNetease, &QPushButton::clicked, this, [this] {
        const auto state = sourceState(core::SourceId::Netease);
        loadOnlinePage(qMax(0, state.offset), core::SourceId::Netease);
    });
    connect(m_retryQq, &QPushButton::clicked, this, [this] {
        const auto state = sourceState(core::SourceId::QqMusic);
        loadOnlinePage(qMax(0, state.offset), core::SourceId::QqMusic);
    });
    connect(m_hotRetryNetease, &QPushButton::clicked, this, [this] {
        retryHotSearch(core::SourceId::Netease);
    });
    connect(m_hotRetryQq, &QPushButton::clicked, this, [this] {
        retryHotSearch(core::SourceId::QqMusic);
    });
    connect(m_clearHistory, &QPushButton::clicked, this, [this] {
        m_searchCache->clearHistory();
        renderAssistant();
    });
    connect(m_assistantList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item)
            emit searchTextChosen(item->data(Qt::UserRole).toString());
    });
    connect(m_assistantList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item)
            emit searchTextChosen(item->data(Qt::UserRole).toString());
    });
    const QList<QListWidget *> discoveryLists = {
        m_historyList, m_neteaseHotList, m_qqHotList
    };
    for (QListWidget *list : discoveryLists) {
        connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
            if (item && item->flags().testFlag(Qt::ItemIsEnabled))
                emit searchTextChosen(item->data(Qt::UserRole).toString());
        });
        connect(list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
            if (item && item->flags().testFlag(Qt::ItemIsEnabled))
                emit searchTextChosen(item->data(Qt::UserRole).toString());
        });
    }
    connect(m_suggestionTimer, &QTimer::timeout, this, &SearchPage::requestSuggestions);
    connect(m_localSearch, &core::SearchService::searchFinished, this,
            [this](quint64 generation, const QString &query,
                   const QList<core::Song> &songs, int totalMatches) {
        if (generation != m_localRequestGeneration || query != m_query)
            return;
        m_results = songs;
        if (totalMatches > m_results.size()) {
            m_title->setText(QStringLiteral("搜索 \"%1\" · 显示前 %2 / 共 %3 个结果")
                                 .arg(m_query).arg(m_results.size()).arg(totalMatches));
        } else {
            m_title->setText(QStringLiteral("搜索 \"%1\" · %2 个结果")
                                 .arg(m_query).arg(m_results.size()));
        }
        m_songList->setSongs(m_results);
        m_songList->setHighlightQuery(m_query);
        renderLocalGroups();
        rebuildAggregatedOnlineResults();
        renderGenericResults();
        showCurrentResultPage();
    });
}

SearchPage::~SearchPage()
{
    delete m_searchCache;
}

void SearchPage::hideEvent(QHideEvent *event)
{
    QToolTip::hideText();
    if (m_assistantList) {
        m_assistantList->clearSelection();
        m_assistantList->setCurrentRow(-1);
        m_assistantList->viewport()->update();
    }
    if (m_suggestionTimer)
        m_suggestionTimer->stop();
    ++m_assistantGeneration;
    m_searchAssistantVisible = false;
    QWidget::hideEvent(event);
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

void SearchPage::setSourceAccessStates(
    const QHash<int, core::SourceAccessState> &states)
{
    if (m_sourceAccessStates == states)
        return;
    m_sourceAccessStates = states;
    m_onlineList->setSourceAccessStates(states);
    m_genericSongList->setSourceAccessStates(states);
    updateOnlineHeader();
    m_onlineSongs = m_onlineList->songs();
    m_genericSongs = m_genericSongList->songs();
}

void SearchPage::setLocalSongs(const QList<core::Song> &songs)
{
    m_localRequestGeneration = 0;
    m_localSnapshot = songs;
    m_localSearch->updateSnapshot(songs);
}

void SearchPage::performSearch(const QString &query)
{
    beginSearch(query, true);
}

void SearchPage::beginSearch(const QString &query, bool recordHistory)
{
    cancelActiveSearch();
    m_query = query.trimmed();
    if (m_query.isEmpty()) {
        showSearchAssistant();
        return;
    }
    m_searchAssistantVisible = false;
    if (recordHistory)
        m_searchCache->addHistory(m_query);
    ++m_searchGeneration;
    m_onlineOffset = 0;
    m_onlineLoading = false;
    m_albumCoverLookups.clear();
    m_albumCoverQueue.clear();
    for (const core::Song &queued : std::as_const(m_coverDownloadQueue))
        m_coverDownloads.remove(queued.stableIdentity());
    m_coverDownloadQueue.clear();
    m_sourceStates.clear();
    m_onlineItems.clear();
    m_onlineGroups.clear();
    m_onlineSongs.clear();
    m_onlineList->setSearchResultGroups({});
    m_onlineHeader->hide();
    startLocalSearch();

    if (m_scope != core::SearchScope::Local
        && (m_source || m_registry) && m_lib) {
        loadOnlinePage(0);
    } else {
        renderGenericResults();
        showCurrentResultPage();
    }
}

void SearchPage::previewSearchText(const QString &text)
{
    m_searchAssistantVisible = true;
    m_assistantInput = text.trimmed();
    ++m_assistantGeneration;
    m_suggestionTimer->stop();
    m_suggestions.clear();
    if (m_assistantInput.isEmpty()) {
        showSearchAssistant();
        return;
    }

    QList<core::SearchSuggestion> local;
    QSet<QString> seen;
    const QString normalized = core::SearchCache::normalizedQuery(m_assistantInput);
    auto append = [&local, &seen, &normalized](const QString &value,
                                               core::SearchItemType type,
                                               const QString &subtitle) {
        const QString text = value.trimmed();
        const QString key = core::SearchCache::normalizedQuery(text);
        if (text.isEmpty() || seen.contains(key) || !key.contains(normalized))
            return;
        core::SearchSuggestion suggestion;
        suggestion.source = core::SourceId::Local;
        suggestion.type = type;
        suggestion.text = text;
        suggestion.subtitle = subtitle;
        local.append(suggestion);
        seen.insert(key);
    };
    for (const core::Song &song : std::as_const(m_localSnapshot)) {
        append(song.title, core::SearchItemType::Song, song.artist);
        append(song.artist, core::SearchItemType::Artist, QStringLiteral("本地歌手"));
        append(song.album, core::SearchItemType::Album, song.artist);
        if (local.size() >= 8)
            break;
    }
    m_suggestions.insert(int(core::SourceId::Local), local);

    for (core::MusicSource *source : activeOnlineSources()) {
        QList<core::SearchSuggestion> cached;
        if (m_searchCache->loadSuggestions(source->sourceId(), m_assistantInput, &cached))
            m_suggestions.insert(int(source->sourceId()), cached);
    }
    renderAssistant();
    m_stack->setCurrentIndex(kAssistantPage);
    m_title->setText(QStringLiteral("搜索联想"));
    if (m_assistantInput.size() >= 2)
        m_suggestionTimer->start();
}

void SearchPage::showSearchAssistant()
{
    m_searchAssistantVisible = true;
    ++m_assistantGeneration;
    m_suggestionTimer->stop();
    m_assistantInput.clear();
    m_suggestions.clear();
    requestDiscovery();
    renderAssistant();
    m_stack->setCurrentIndex(kAssistantPage);
    m_title->setText(QStringLiteral("搜索"));
}

void SearchPage::moveSearchAssistantSelection(int direction)
{
    if (!m_assistantList || m_assistantList->count() == 0)
        return;
    int row = m_assistantList->currentRow();
    if (row < 0)
        row = direction < 0 ? m_assistantList->count() - 1 : 0;
    else
        row = (row + (direction < 0 ? -1 : 1) + m_assistantList->count())
            % m_assistantList->count();
    m_assistantList->setCurrentRow(row);
}

QString SearchPage::resolvedSearchText(const QString &fallback) const
{
    if (m_stack && m_stack->currentIndex() == kAssistantPage && m_assistantList) {
        if (QListWidgetItem *item = m_assistantList->currentItem()) {
            const QString text = item->data(Qt::UserRole).toString().trimmed();
            if (!text.isEmpty())
                return text;
        }
    }
    return fallback.trimmed();
}

void SearchPage::requestDiscovery()
{
    m_hotTerms.clear();
    m_hotErrors.clear();
    const quint64 generation = m_assistantGeneration;
    const QPointer<SearchPage> guard(this);
    for (core::MusicSource *source : activeOnlineSources()) {
        QList<core::HotSearchTerm> cached;
        if (m_searchCache->loadHotTerms(source->sourceId(), &cached))
            m_hotTerms.insert(int(source->sourceId()), cached);
        if (!source->capabilities().hotSearch)
            continue;
        requestHotSearch(source, generation);
        if (source->capabilities().defaultSearch) {
            source->defaultSearchText([guard, generation](const QString &text) {
                if (guard && generation == guard->m_assistantGeneration && !text.trimmed().isEmpty())
                    emit guard->defaultSearchTextReady(text.trimmed());
            });
        }
    }
}

void SearchPage::requestHotSearch(core::MusicSource *source, quint64 generation)
{
    if (!source || !source->capabilities().hotSearch)
        return;
    const core::SourceId sourceId = source->sourceId();
    m_hotErrors.insert(int(sourceId), QString());
    renderAssistant();
    const QPointer<SearchPage> guard(this);
    source->hotSearch(10, [guard, generation, sourceId](const QList<core::HotSearchTerm> &terms) {
        if (!guard || generation != guard->m_assistantGeneration)
            return;
        guard->m_hotTerms.insert(int(sourceId), terms);
        guard->m_hotErrors.remove(int(sourceId));
        guard->m_searchCache->storeHotTerms(sourceId, terms);
        guard->renderAssistant();
    }, [guard, generation, sourceId](const QString &message) {
        if (!guard || generation != guard->m_assistantGeneration)
            return;
        guard->m_hotErrors.insert(int(sourceId), message.trimmed().isEmpty()
            ? QStringLiteral("来源暂不可用") : message.trimmed());
        guard->renderAssistant();
    });
}

void SearchPage::retryHotSearch(core::SourceId sourceId)
{
    const QList<core::MusicSource *> sources = activeOnlineSources(sourceId);
    if (sources.isEmpty()) {
        m_hotErrors.insert(int(sourceId), QStringLiteral("来源未启用"));
        renderAssistant();
        return;
    }
    requestHotSearch(sources.constFirst(), m_assistantGeneration);
}

void SearchPage::requestSuggestions()
{
    const QString input = m_assistantInput;
    if (input.size() < 2)
        return;
    const quint64 generation = m_assistantGeneration;
    const QPointer<SearchPage> guard(this);
    for (core::MusicSource *source : activeOnlineSources()) {
        if (!source->capabilities().searchSuggestions)
            continue;
        const core::SourceId sourceId = source->sourceId();
        source->searchSuggestions(input, 8,
            [guard, generation, input, sourceId](const QList<core::SearchSuggestion> &values) {
                if (!guard || generation != guard->m_assistantGeneration
                    || input != guard->m_assistantInput) {
                    return;
                }
                guard->m_suggestions.insert(int(sourceId), values);
                guard->m_searchCache->storeSuggestions(sourceId, input, values);
                guard->renderAssistant();
            }, [guard, generation](const QString &) {
                if (guard && generation == guard->m_assistantGeneration)
                    guard->renderAssistant();
            });
    }
}

void SearchPage::renderAssistant()
{
    if (!m_assistantList || !m_discoveryPanel)
        return;
    const QString selected = m_assistantList->currentItem()
        ? m_assistantList->currentItem()->data(Qt::UserRole).toString() : QString();
    m_assistantList->clear();
    m_historyList->clear();
    m_neteaseHotList->clear();
    m_qqHotList->clear();
    if (m_hotRetryNetease) {
        m_hotRetryNetease->setVisible(
            !m_hotErrors.value(int(core::SourceId::Netease)).isEmpty()
            && m_enabledSourceIds.contains(int(core::SourceId::Netease)));
    }
    if (m_hotRetryQq) {
        m_hotRetryQq->setVisible(
            !m_hotErrors.value(int(core::SourceId::QqMusic)).isEmpty()
            && m_enabledSourceIds.contains(int(core::SourceId::QqMusic)));
    }

    auto append = [](QListWidget *list, QSet<QString> *seen,
                     const QString &query, const QString &label) {
        const QString text = query.trimmed();
        const QString key = core::SearchCache::normalizedQuery(text);
        if (!list || text.isEmpty() || (seen && seen->contains(key)))
            return;
        auto *item = new QListWidgetItem(label, list);
        item->setData(Qt::UserRole, text);
        item->setToolTip(label);
        if (seen)
            seen->insert(key);
    };

    if (m_assistantInput.isEmpty()) {
        m_discoveryPanel->show();
        m_assistantList->hide();
        const QStringList history = m_searchCache->history();
        QSet<QString> historySeen;
        for (const QString &text : history) {
            append(m_historyList, &historySeen, text, text);
            append(m_assistantList, nullptr, text, QStringLiteral("历史 · %1").arg(text));
        }
        const QList<core::SourceId> order = { core::SourceId::Netease,
                                               core::SourceId::QqMusic };
        for (core::SourceId source : order) {
            QListWidget *target = source == core::SourceId::Netease
                ? m_neteaseHotList : m_qqHotList;
            QSet<QString> sourceSeen;
            for (const core::HotSearchTerm &term : m_hotTerms.value(int(source))) {
                const QString label = term.rank >= 0
                    ? QStringLiteral("%1  %2").arg(term.rank + 1, 2).arg(term.text)
                    : term.text;
                append(target, &sourceSeen, term.text, label);
                append(m_assistantList, nullptr, term.text,
                       QStringLiteral("%1热搜 · %2").arg(sourceLabel(source), term.text));
            }
            if (target->count() == 0) {
                QString status = m_hotErrors.value(int(source));
                if (status.isEmpty()) {
                    status = m_enabledSourceIds.contains(int(source))
                        ? QStringLiteral("正在加载…") : QStringLiteral("来源未启用");
                }
                auto *item = new QListWidgetItem(status, target);
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                item->setForeground(themeColor(ThemeColor::TextTertiary));
            } else if (!m_hotErrors.value(int(source)).isEmpty()) {
                auto *item = new QListWidgetItem(
                    QStringLiteral("刷新失败 · %1").arg(m_hotErrors.value(int(source))), target);
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                item->setForeground(themeColor(ThemeColor::TextTertiary));
            }
        }
        m_historyList->setVisible(!history.isEmpty());
        m_assistantHeader->setText(QStringLiteral("搜索历史"));
        m_assistantHeader->setVisible(!history.isEmpty());
        m_clearHistory->setVisible(!history.isEmpty());
    } else {
        m_discoveryPanel->hide();
        m_assistantList->show();
        QSet<QString> seen;
        const QList<core::SourceId> order = { core::SourceId::Local,
                                               core::SourceId::Netease,
                                               core::SourceId::QqMusic };
        for (core::SourceId source : order) {
            for (const core::SearchSuggestion &suggestion : m_suggestions.value(int(source))) {
                const QString suffix = suggestion.subtitle.trimmed().isEmpty()
                    ? QString() : QStringLiteral(" · %1").arg(suggestion.subtitle.trimmed());
                append(m_assistantList, &seen, suggestion.text,
                       QStringLiteral("%1 · %2 · %3%4")
                           .arg(sourceLabel(source), itemTypeLabel(suggestion.type),
                                suggestion.text, suffix));
            }
        }
        m_assistantHeader->show();
        m_assistantHeader->setText(QStringLiteral("搜索联想 · ↑↓选择，Enter搜索"));
        m_clearHistory->hide();
    }
    if (!selected.isEmpty()) {
        for (int row = 0; row < m_assistantList->count(); ++row) {
            if (m_assistantList->item(row)->data(Qt::UserRole).toString() == selected) {
                m_assistantList->setCurrentRow(row);
                break;
            }
        }
    }

}

void SearchPage::renderLocalGroups()
{
    while (QLayoutItem *item = m_artistLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto artists = core::SearchService::artists(m_results);
    for (const auto &a : artists) {
        if (!a.name.contains(m_query, Qt::CaseInsensitive))
            continue;
        auto *row = makeResultRow(a.name, QStringLiteral("%1 首").arg(a.count),
                                  m_artistLayout->parentWidget());
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
        if (!a.name.contains(m_query, Qt::CaseInsensitive)
            && !a.artist.contains(m_query, Qt::CaseInsensitive))
            continue;
        auto *row = makeResultRow(a.name, a.artist + QStringLiteral(" · %1 首").arg(a.count),
                                  m_albumLayout->parentWidget());
        connect(row, &QPushButton::clicked, this, [this, name = a.name, artist = a.artist] {
            emit albumClicked(name, artist);
        });
        m_albumLayout->addWidget(row);
    }
    m_albumLayout->addStretch(1);
}

void SearchPage::startLocalSearch()
{
    m_localSearch->cancelAsync();
    m_localRequestGeneration = 0;
    m_results.clear();
    m_songList->setSongs({});
    renderLocalGroups();
    if (m_query.isEmpty()) {
        m_title->setText(QStringLiteral("搜索"));
        return;
    }
    if (m_scope == core::SearchScope::Netease || m_scope == core::SearchScope::QqMusic
        || m_category == core::SearchCategory::Playlists
        || m_category == core::SearchCategory::Lyrics) {
        renderGenericResults();
        showCurrentResultPage();
        return;
    }
    m_title->setText(QStringLiteral("正在搜索 \"%1\"").arg(m_query));
    m_songList->setHighlightQuery(m_query);
    m_localRequestGeneration = m_localSearch->searchAsync(m_query, 200);
}

void SearchPage::refreshLocalResults()
{
    startLocalSearch();
}

void SearchPage::cancelActiveSearch()
{
    m_localSearch->cancelAsync();
    m_localRequestGeneration = 0;
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
        for (const core::SearchResultItem &item : std::as_const(m_onlineItems)) {
            if (item.source == sourceId)
                ++count;
        }
        QString status;
        switch (it->state) {
        case core::SearchLoadState::Loading:
            status = it->fromCache
                ? QStringLiteral("缓存 %1 项 · 刷新中…").arg(count)
                : QStringLiteral("加载中…");
            break;
        case core::SearchLoadState::Ready:
            status = it->fromCache ? QStringLiteral("缓存 %1 项").arg(count)
                                   : QStringLiteral("%1 项").arg(count);
            if (!it->error.isEmpty())
                status += QStringLiteral(" · %1").arg(it->error);
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
        QString access;
        switch (m_sourceAccessStates.value(int(sourceId),
                                            core::SourceAccessState::Guest)) {
        case core::SourceAccessState::Authenticated: access = QStringLiteral("已登录"); break;
        case core::SourceAccessState::Verifying: access = QStringLiteral("验证中"); break;
        case core::SourceAccessState::Unavailable: access = QStringLiteral("不可用"); break;
        case core::SourceAccessState::Guest: access = QStringLiteral("游客"); break;
        }
        parts.append(QStringLiteral("%1(%2) %3")
                         .arg(sourceLabel(sourceId), access, status));
    }
    m_onlineHeader->setText(parts.isEmpty() ? QStringLiteral("暂无可用在线来源")
                                            : parts.join(QStringLiteral(" · ")));
    m_onlineHeader->setVisible(!m_query.isEmpty() && !m_sourceStates.isEmpty());
    const auto needsRetry = [this](core::SourceId source) {
        const auto state = m_sourceStates.value(int(source));
        return state.state == core::SearchLoadState::Failed
            || state.state == core::SearchLoadState::TimedOut
            || (state.fromCache && !state.error.isEmpty());
    };
    const auto retryVisible = [this, &needsRetry](core::SourceId source) {
        const auto state = m_sourceStates.value(int(source));
        return state.state != core::SearchLoadState::Loading && needsRetry(source);
    };
    m_retryNetease->setVisible(retryVisible(core::SourceId::Netease));
    m_retryQq->setVisible(retryVisible(core::SourceId::QqMusic));
}

void SearchPage::updateOnlineLoadingState()
{
    m_onlineLoading = false;
    for (auto it = m_sourceStates.constBegin(); it != m_sourceStates.constEnd(); ++it) {
        if (it->generation == m_searchGeneration
            && it->state == core::SearchLoadState::Loading) {
            m_onlineLoading = true;
            return;
        }
    }
}

QList<core::MusicSource *> SearchPage::activeOnlineSources(core::SourceId onlySource) const
{
    QList<core::MusicSource *> sources = m_registry ? m_registry->onlineSources()
                                                    : QList<core::MusicSource *>{ m_source };
    sources.removeAll(nullptr);
    for (auto it = sources.begin(); it != sources.end();) {
        const core::SourceId sourceId = (*it)->sourceId();
        bool accepted = m_enabledSourceIds.contains(int(sourceId));
        if (m_scope == core::SearchScope::Local)
            accepted = false;
        else if (m_scope == core::SearchScope::Netease)
            accepted = accepted && sourceId == core::SourceId::Netease;
        else if (m_scope == core::SearchScope::QqMusic)
            accepted = accepted && sourceId == core::SourceId::QqMusic;
        if (onlySource != core::SourceId::Local)
            accepted = accepted && sourceId == onlySource;
        if (!accepted)
            it = sources.erase(it);
        else
            ++it;
    }
    return sources;
}

void SearchPage::mergeOnlineResponse(const core::SearchResponse &response)
{
    for (const core::SearchResultItem &sourceItem : response.items) {
        core::SearchResultItem item = sourceItem;
        if ((item.type == core::SearchItemType::Song
             || item.type == core::SearchItemType::Lyric)
            && item.song.hasRemoteIdentity() && m_lib) {
            core::Song song = item.song;
            const core::Song stored = m_lib->songByRemoteId(song.source,
                                                             song.effectiveRemoteId());
            if (stored.id > 0) {
                song.id = stored.id;
                song.coverPath = stored.coverPath;
                song.cachePath = stored.cachePath;
                song.downloadPath = stored.downloadPath;
                song.lyricPath = stored.lyricPath;
                song.playCount = stored.playCount;
                song.lastPlayedMs = stored.lastPlayedMs;
            }
            item.song = song;
            item.source = song.sourceId();
            item.remoteId = song.effectiveRemoteId();
            if (item.title.isEmpty())
                item.title = song.title;
            if (item.artist.isEmpty())
                item.artist = song.artist;
            if (item.album.isEmpty())
                item.album = song.album;
            if (item.durationMs <= 0)
                item.durationMs = song.durationMs;
        }
        const QString identity = item.stableIdentity();
        if (identity.isEmpty())
            continue;
        bool replaced = false;
        for (core::SearchResultItem &existing : m_onlineItems) {
            if (existing.stableIdentity() == identity) {
                existing = item;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            m_onlineItems.append(item);
    }
}

void SearchPage::removeOnlineItems(const QSet<QString> &identities)
{
    if (identities.isEmpty())
        return;
    for (auto it = m_onlineItems.begin(); it != m_onlineItems.end();) {
        if (identities.contains(it->stableIdentity()))
            it = m_onlineItems.erase(it);
        else
            ++it;
    }
}

void SearchPage::loadOnlinePage(int offset, core::SourceId onlySource)
{
    if ((!m_source && !m_registry) || !m_lib || m_query.isEmpty())
        return;
    QList<core::MusicSource *> sources = activeOnlineSources(onlySource);
    if (sources.isEmpty()) {
        m_onlineHeader->setText(QStringLiteral("暂无可用在线来源"));
        m_onlineHeader->show();
        return;
    }
    const quint64 generation = m_searchGeneration;
    const QPointer<SearchPage> guard(this);
    if (offset == 0 && onlySource == core::SourceId::Local) {
        m_onlineItems.clear();
        m_onlineGroups.clear();
        m_onlineSongs.clear();
    } else if (offset == 0) {
        for (auto it = m_onlineItems.begin(); it != m_onlineItems.end();) {
            if (it->source == onlySource)
                it = m_onlineItems.erase(it);
            else
                ++it;
        }
    }
    const auto renderResults = [guard, generation] {
        if (!guard || generation != guard->m_searchGeneration)
            return;
        guard->rebuildAggregatedOnlineResults();
        guard->renderGenericResults();
        guard->updateOnlineLoadingState();
        guard->updateOnlineHeader();
        guard->showCurrentResultPage();
        for (const core::Song &song : std::as_const(guard->m_onlineSongs))
            guard->ensureCover(song);
        QTimer::singleShot(0, guard, &SearchPage::requestNextOnlinePages);
    };
    bool launched = false;
    for (core::MusicSource *source : std::as_const(sources)) {
        const int sourceKey = int(source->sourceId());
        const core::SearchSourceState previous = m_sourceStates.value(sourceKey);
        if (previous.generation == generation
            && previous.state == core::SearchLoadState::Loading) {
            continue;
        }
        if (offset > 0 && onlySource == core::SourceId::Local && !previous.hasMore)
            continue;

        core::SearchSourceState state = previous;
        state.source = source->sourceId();
        state.state = core::SearchLoadState::Loading;
        state.generation = generation;
        state.offset = offset;
        state.error.clear();
        m_sourceStates.insert(sourceKey, state);
        launched = true;

        core::SearchRequest request;
        request.keywords = m_query;
        request.category = m_category;
        request.scope = source->sourceId() == core::SourceId::Netease
            ? core::SearchScope::Netease : core::SearchScope::QqMusic;
        request.limit = m_onlinePageSize;
        request.offset = offset;
        request.generation = generation;
        auto settled = std::make_shared<bool>(false);
        auto cachedIdentities = std::make_shared<QSet<QString>>();
        bool cacheFresh = false;
        core::SearchResponse cached;
        if (m_searchCache->loadResponse(request, source->sourceId(), &cached,
                                        &cacheFresh)) {
            for (const core::SearchResultItem &item : cached.items)
                cachedIdentities->insert(item.stableIdentity());
            mergeOnlineResponse(cached);
            auto cachedState = m_sourceStates.value(int(source->sourceId()));
            cachedState.fromCache = true;
            cachedState.stale = !cacheFresh;
            cachedState.hasMore = cached.hasMore;
            m_sourceStates.insert(int(source->sourceId()), cachedState);
            renderResults();
        }
        QTimer::singleShot(m_sourceTimeoutMs, this,
                           [guard, source, sourceKey, generation, settled,
                            cachedIdentities, renderResults] {
            if (!guard || generation != guard->m_searchGeneration || *settled)
                return;
            *settled = true;
            source->cancelSearch(generation);
            auto state = guard->m_sourceStates.value(sourceKey);
            if (cachedIdentities->isEmpty()) {
                state.state = core::SearchLoadState::TimedOut;
                state.error = QStringLiteral("请求超时");
            } else {
                state.state = core::SearchLoadState::Ready;
                state.fromCache = true;
                state.error = QStringLiteral("网络超时，显示缓存");
            }
            guard->m_sourceStates.insert(sourceKey, state);
            renderResults();
        });
        source->search(request,
                       [guard, sourceKey, generation, request, settled,
                        cachedIdentities, renderResults]
                       (const core::SearchResponse &response) {
        if (!guard || generation != guard->m_searchGeneration || *settled
            || response.generation != generation)
            return;
        *settled = true;
        guard->removeOnlineItems(*cachedIdentities);
        guard->mergeOnlineResponse(response);
        guard->m_searchCache->storeResponse(request, response);
        auto state = guard->m_sourceStates.value(sourceKey);
        state.state = core::SearchLoadState::Ready;
        state.offset = response.offset;
        state.hasMore = response.hasMore;
        state.fromCache = false;
        state.stale = false;
        state.error.clear();
        guard->m_sourceStates.insert(sourceKey, state);
        guard->m_onlineOffset = qMax(guard->m_onlineOffset, response.offset);
        renderResults();
    }, [guard, sourceKey, generation, settled, cachedIdentities,
        renderResults](const QString &message) {
        if (!guard || generation != guard->m_searchGeneration || *settled)
            return;
        *settled = true;
        auto state = guard->m_sourceStates.value(sourceKey);
        if (cachedIdentities->isEmpty()) {
            state.state = core::SearchLoadState::Failed;
            state.error = message;
        } else {
            state.state = core::SearchLoadState::Ready;
            state.fromCache = true;
            state.error = QStringLiteral("刷新失败，显示缓存：%1").arg(message);
        }
        guard->m_sourceStates.insert(sourceKey, state);
        renderResults();
    });
    }
    updateOnlineLoadingState();
    updateOnlineHeader();
    if (!launched)
        return;
}

void SearchPage::requestNextOnlinePages()
{
    if (m_query.isEmpty() || !m_stack
        || (m_stack->currentIndex() != kOnlineSongsPage
            && m_stack->currentIndex() != kGenericResultsPage)) {
        return;
    }
    SongListView *activeList = m_stack->currentIndex() == kGenericResultsPage
        ? m_genericSongList : m_onlineList;
    QScrollBar *bar = activeList ? activeList->verticalScrollBar() : nullptr;
    if (bar && bar->maximum() > 0
        && bar->maximum() - bar->value() > qMax(bar->pageStep(), 160)) {
        return;
    }
    const QList<core::MusicSource *> sources = activeOnlineSources();
    for (core::MusicSource *source : sources) {
        const core::SearchSourceState state = m_sourceStates.value(int(source->sourceId()));
        if (state.generation != m_searchGeneration || !state.hasMore
            || state.state == core::SearchLoadState::Loading || !state.error.isEmpty()) {
            continue;
        }
        loadOnlinePage(state.offset + m_onlinePageSize, source->sourceId());
    }
}

void SearchPage::rebuildAggregatedOnlineResults()
{
    QList<core::SearchResultItem> songItems;
    for (const core::SearchResultItem &item : std::as_const(m_onlineItems)) {
        if (item.type == core::SearchItemType::Song)
            songItems.append(item);
    }
    if ((m_scope == core::SearchScope::All || m_scope == core::SearchScope::Local)
        && (m_category == core::SearchCategory::Songs
            || m_category == core::SearchCategory::All)) {
        int rank = 0;
        for (const core::Song &song : std::as_const(m_results))
            songItems.append(localSongItem(song, rank++));
    }
    core::SearchAggregateOptions options;
    options.query = m_query;
    options.sortMode = m_sortMode;
    options.preferredSource = m_preferredSource;
    m_onlineGroups = core::SearchAggregator::aggregate(songItems, options);
    m_onlineSongs.clear();
    m_onlineSongs.reserve(m_onlineGroups.size());
    for (const core::SearchResultGroup &group : std::as_const(m_onlineGroups)) {
        const core::Song song = group.preferredSong();
        if ((song.isOnline() && song.hasRemoteIdentity())
            || (!song.isOnline() && !song.filePath.isEmpty())) {
            m_onlineSongs.append(song);
        }
    }
    m_onlineList->setSearchResultGroups(m_onlineGroups);
    m_onlineSongs = m_onlineList->songs();
    m_onlineList->setHighlightQuery(m_query);
}

void SearchPage::renderGenericResults()
{
    if (!m_genericLayout)
        return;
    while (QLayoutItem *layoutItem = m_genericLayout->takeAt(0)) {
        delete layoutItem->widget();
        delete layoutItem;
    }

    QList<core::SearchResultItem> generic;
    for (const core::SearchResultItem &item : std::as_const(m_onlineItems)) {
        if (m_category == core::SearchCategory::All || item.type != core::SearchItemType::Song)
            generic.append(item);
    }
    if (m_scope == core::SearchScope::All || m_scope == core::SearchScope::Local) {
        if (m_category == core::SearchCategory::All
            || m_category == core::SearchCategory::Artists) {
            for (const auto &artist : core::SearchService::artists(m_results)) {
                core::SearchResultItem item;
                item.type = core::SearchItemType::Artist;
                item.source = core::SourceId::Local;
                item.remoteId = artist.name;
                item.title = artist.name;
                item.subtitle = QStringLiteral("%1 首本地歌曲").arg(artist.count);
                generic.append(item);
            }
        }
        if (m_category == core::SearchCategory::All
            || m_category == core::SearchCategory::Albums) {
            for (const auto &album : core::SearchService::albums(m_results)) {
                core::SearchResultItem item;
                item.type = core::SearchItemType::Album;
                item.source = core::SourceId::Local;
                item.remoteId = album.name + QLatin1Char('|') + album.artist;
                item.title = album.name;
                item.artist = album.artist;
                item.subtitle = QStringLiteral("%1 · %2 首本地歌曲")
                                    .arg(album.artist).arg(album.count);
                generic.append(item);
            }
        }
    }

    core::SearchAggregateOptions options;
    options.query = m_query;
    options.sortMode = m_sortMode;
    options.preferredSource = m_preferredSource;
    const QList<core::SearchResultGroup> sorted = core::SearchAggregator::aggregate(generic, options);
    m_genericSongs.clear();
    if (m_category == core::SearchCategory::All) {
        for (const core::SearchResultGroup &group : std::as_const(m_onlineGroups)) {
            const core::Song song = group.preferredSong();
            if (!song.filePath.isEmpty())
                m_genericSongs.append(song);
        }
    }
    if (m_category == core::SearchCategory::All) {
        m_genericSongList->setSearchResultGroups(m_onlineGroups);
        m_genericSongs = m_genericSongList->songs();
    } else {
        m_genericSongList->setSongs(m_genericSongs);
    }
    m_genericSongList->setHighlightQuery(m_query);
    m_genericSongsHeader->setVisible(!m_genericSongs.isEmpty());
    m_genericSongList->setVisible(!m_genericSongs.isEmpty());

    auto activate = [this](const core::SearchResultItem &item) {
        if (item.type == core::SearchItemType::Song
            || item.type == core::SearchItemType::Lyric) {
            if (!item.song.filePath.isEmpty())
                emit playRequested({ item.song }, 0);
            return;
        }
        if (item.source == core::SourceId::Local) {
            if (item.type == core::SearchItemType::Artist)
                emit artistClicked(item.title);
            else if (item.type == core::SearchItemType::Album)
                emit albumClicked(item.title, item.artist);
            return;
        }
        emit onlineResultActivated(int(item.source), int(item.type), item.remoteId, item.title);
    };
    auto addSection = [this](const QString &title) {
        auto *label = new QLabel(title, m_genericLayout->parentWidget());
        setThemedStyleSheet(label, QStringLiteral("color:@textPrimary;font-size:14px;font-weight:600;"));
        m_genericLayout->addWidget(label);
    };
    auto addItem = [this, &activate](const core::SearchResultItem &item,
                                     const QString &extra = QString()) {
        QString subtitle = item.subtitle;
        if (subtitle.isEmpty())
            subtitle = !item.artist.isEmpty() ? item.artist : item.album;
        const QString source = sourceLabel(item.source);
        auto *row = makeResultRow(item.title,
                                  QStringLiteral("%1 · %2%3")
                                      .arg(source, itemTypeLabel(item.type),
                                           extra.isEmpty() ? QString()
                                               : QStringLiteral(" · %1").arg(extra)),
                                  m_genericLayout->parentWidget());
        if (!subtitle.isEmpty())
            row->setToolTip(subtitle);
        connect(row, &QPushButton::clicked, this, [activate, item] { activate(item); });
        m_genericLayout->addWidget(row);
    };

    if (m_category == core::SearchCategory::All && !sorted.isEmpty()) {
        addSection(QStringLiteral("最佳匹配"));
        addItem(sorted.constFirst().preferredItem());
    }
    const QList<core::SearchItemType> typeOrder = {
        core::SearchItemType::Artist,
        core::SearchItemType::Song,
        core::SearchItemType::Album,
        core::SearchItemType::Playlist,
        core::SearchItemType::Lyric
    };
    int rendered = 0;
    for (core::SearchItemType type : typeOrder) {
        if (type == core::SearchItemType::Song)
            continue;
        QList<core::SearchResultItem> section;
        for (const core::SearchResultGroup &group : sorted) {
            const core::SearchResultItem item = group.preferredItem();
            if (item.type == type)
                section.append(item);
        }
        if (section.isEmpty())
            continue;
        addSection(itemTypeLabel(type));
        for (const core::SearchResultItem &item : std::as_const(section)) {
            addItem(item, item.availabilityError);
            ++rendered;
        }
    }
    if (rendered == 0 && m_genericSongs.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("当前范围和分类暂无结果"),
                                 m_genericLayout->parentWidget());
        setThemedStyleSheet(empty, QStringLiteral("color:@textTertiary;"));
        m_genericLayout->addWidget(empty);
    }
    m_genericLayout->addStretch(1);
}

void SearchPage::showCurrentResultPage()
{
    if (m_searchAssistantVisible)
        return;
    if (m_query.isEmpty()) {
        m_stack->setCurrentIndex(kAssistantPage);
        return;
    }
    switch (m_category) {
    case core::SearchCategory::Songs:
        m_stack->setCurrentIndex(m_scope == core::SearchScope::Local
                                     ? kLocalSongsPage : kOnlineSongsPage);
        break;
    case core::SearchCategory::Artists:
        m_stack->setCurrentIndex(m_scope == core::SearchScope::Local
                                     ? kLocalArtistsPage : kGenericResultsPage);
        break;
    case core::SearchCategory::Albums:
        m_stack->setCurrentIndex(m_scope == core::SearchScope::Local
                                     ? kLocalAlbumsPage : kGenericResultsPage);
        break;
    case core::SearchCategory::All:
    case core::SearchCategory::Playlists:
    case core::SearchCategory::Lyrics:
        m_stack->setCurrentIndex(kGenericResultsPage);
        break;
    }
}

void SearchPage::refreshOnlineCovers()
{
    if (!m_lib || m_onlineItems.isEmpty())
        return;
    bool changed = false;
    for (core::SearchResultItem &item : m_onlineItems) {
        if (item.type != core::SearchItemType::Song)
            continue;
        core::Song &song = item.song;
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
        rebuildAggregatedOnlineResults();
}

void SearchPage::setSortMode(core::SearchSortMode mode)
{
    if (m_sortMode == mode)
        return;
    m_sortMode = mode;
    if (m_sortCombo) {
        const int index = m_sortCombo->findData(int(mode));
        if (index >= 0) {
            const QSignalBlocker blocker(m_sortCombo);
            m_sortCombo->setCurrentIndex(index);
        }
    }
    rebuildAggregatedOnlineResults();
    renderGenericResults();
}

void SearchPage::setSearchScope(core::SearchScope scope)
{
    if (m_scope == scope)
        return;
    m_scope = scope;
    if (m_scopeCombo) {
        const int index = m_scopeCombo->findData(int(scope));
        if (index >= 0) {
            const QSignalBlocker blocker(m_scopeCombo);
            m_scopeCombo->setCurrentIndex(index);
        }
    }
    if (scope == core::SearchScope::Netease)
        m_preferredSource = core::SourceId::Netease;
    else if (scope == core::SearchScope::QqMusic)
        m_preferredSource = core::SourceId::QqMusic;
    else
        m_preferredSource = core::SourceId::Local;
    if (!m_query.isEmpty())
        beginSearch(m_query, false);
    else
        showSearchAssistant();
}

void SearchPage::setSearchCategory(core::SearchCategory category)
{
    if (m_category == category)
        return;
    m_category = category;
    if (m_categoryCombo) {
        const int index = m_categoryCombo->findData(int(category));
        if (index >= 0) {
            const QSignalBlocker blocker(m_categoryCombo);
            m_categoryCombo->setCurrentIndex(index);
        }
    }
    if (!m_query.isEmpty())
        beginSearch(m_query, false);
}

void SearchPage::setPreferredSource(core::SourceId source)
{
    if (m_preferredSource == source)
        return;
    m_preferredSource = source;
    rebuildAggregatedOnlineResults();
    renderGenericResults();
}

QList<core::Song> SearchPage::currentSongs() const
{
    // 本地与在线列表共用操作信号，必须按当前可见页返回对应数据。
    // 旧实现始终返回本地结果，在线歌曲收藏/加入歌单时会写入错误 id，
    // 本地结果为空时则完全不会持久化。
    if (!m_stack)
        return {};
    if (m_stack->currentIndex() == kOnlineSongsPage)
        return m_onlineSongs;
    if (m_stack->currentIndex() == kGenericResultsPage)
        return m_genericSongs;
    if (m_stack->currentIndex() == kLocalSongsPage)
        return m_results;
    return {};
}

QList<core::SearchResultGroup> SearchPage::onlineResultGroups() const
{
    return m_onlineGroups;
}

core::SearchSourceState SearchPage::sourceState(core::SourceId source) const
{
    core::SearchSourceState state = m_sourceStates.value(int(source));
    state.source = source;
    return state;
}

QStringList SearchPage::assistantQueries() const
{
    QStringList result;
    if (!m_assistantList)
        return result;
    for (int row = 0; row < m_assistantList->count(); ++row)
        result.append(m_assistantList->item(row)->data(Qt::UserRole).toString());
    return result;
}

void SearchPage::setPlaylistMenuItems(const QList<QPair<int, QString>> &items)
{
    m_songList->setPlaylistMenuItems(items);
    m_onlineList->setPlaylistMenuItems(items);
    m_genericSongList->setPlaylistMenuItems(items);
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
        m_albumCoverQueue.append({ song.sourceId(), song.effectiveAlbumRemoteId(),
                                   m_searchGeneration });
        startAlbumCoverLookups();
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
    m_coverDownloadQueue.append(song);
    startCoverDownloads();
}

void SearchPage::startCoverDownloads()
{
    while (m_coverDownloadsActive < 3 && !m_coverDownloadQueue.isEmpty()) {
        const core::Song song = m_coverDownloadQueue.takeFirst();
        const QString identity = song.stableIdentity();
        core::MusicSource *source = m_registry ? m_registry->sourceFor(song) : m_source;
        if (!source || song.coverUrl.isEmpty() || !m_lib) {
            m_coverDownloads.remove(identity);
            continue;
        }
        const QString path = m_lib->songCoverCachePath(song);
        const qint64 id = song.id;
        const quint64 generation = m_searchGeneration;
        ++m_coverDownloadsActive;
        const QPointer<SearchPage> guard(this);
        source->downloadToFile(QUrl(song.coverUrl), path,
                               [guard, identity, id, path, generation](bool ok) {
            if (!guard) {
                if (ok)
                    QFile::remove(path);
                return;
            }
            guard->m_coverDownloadsActive = qMax(0, guard->m_coverDownloadsActive - 1);
            guard->m_coverDownloads.remove(identity);
            if (generation != guard->m_searchGeneration) {
                if (ok)
                    QFile::remove(path);
                guard->startCoverDownloads();
                return;
            }
            if (ok && guard->m_lib) {
                guard->setOnlineCover(identity, path);
                if (id > 0)
                    guard->m_lib->setSongCoverPath(id, path);
            }
            guard->startCoverDownloads();
        });
    }
}

void SearchPage::resetAfterCacheClear()
{
    cancelActiveSearch();
    ++m_searchGeneration;
    ++m_localRequestGeneration;
    ++m_assistantGeneration;
    m_coverDownloadQueue.clear();
    m_albumCoverQueue.clear();
    m_coverDownloads.clear();
    m_albumCoverLookups.clear();
    for (core::SearchResultItem &item : m_onlineItems) {
        if (item.type != core::SearchItemType::Song
            && item.type != core::SearchItemType::Lyric) {
            continue;
        }
        core::Song &song = item.song;
        const core::Song stored = m_lib && song.hasRemoteIdentity()
            ? m_lib->songByRemoteId(song.source, song.effectiveRemoteId()) : core::Song{};
        if (stored.id > 0) {
            song.id = stored.id;
            song.coverPath = stored.coverPath;
            song.cachePath = stored.cachePath;
            song.downloadPath = stored.downloadPath;
            song.lyricPath = stored.lyricPath;
        } else {
            song.id = -1;
            song.coverPath.clear();
            song.cachePath.clear();
            song.downloadPath.clear();
            song.lyricPath.clear();
        }
    }
    rebuildAggregatedOnlineResults();
    renderGenericResults();
}

void SearchPage::startAlbumCoverLookups()
{
    while (m_albumCoverLookupsActive < 2 && !m_albumCoverQueue.isEmpty()) {
        const AlbumCoverRequest request = m_albumCoverQueue.takeFirst();
        if (request.generation != m_searchGeneration)
            continue;
        core::MusicSource *source = m_registry ? m_registry->source(request.source) : m_source;
        const QString lookupIdentity = QStringLiteral("%1:%2")
                                           .arg(int(request.source)).arg(request.albumId);
        if (!source) {
            m_albumCoverLookups.remove(lookupIdentity);
            continue;
        }
        ++m_albumCoverLookupsActive;
        const QPointer<SearchPage> guard(this);
        source->albumDetail(request.albumId,
            [guard, request, lookupIdentity](const QJsonObject &obj) {
                if (!guard)
                    return;
                guard->m_albumCoverLookupsActive = qMax(
                    0, guard->m_albumCoverLookupsActive - 1);
                if (request.generation == guard->m_searchGeneration) {
                    const QJsonObject album = obj.value(QStringLiteral("album")).toObject();
                    QString coverUrl = album.value(QStringLiteral("picUrl")).toString();
                    if (coverUrl.isEmpty())
                        coverUrl = album.value(QStringLiteral("coverUrl")).toString();
                    if (coverUrl.isEmpty())
                        coverUrl = obj.value(QStringLiteral("coverUrl")).toString();
                    if (coverUrl.isEmpty()) {
                        guard->m_albumCoverLookups.remove(lookupIdentity);
                    } else {
                        for (core::Song &item : guard->m_onlineSongs) {
                            if (item.sourceId() != request.source
                                || item.effectiveAlbumRemoteId() != request.albumId
                                || !item.coverUrl.isEmpty()) {
                                continue;
                            }
                            item.coverUrl = coverUrl;
                            guard->ensureCover(item);
                        }
                    }
                }
                guard->startAlbumCoverLookups();
            }, [guard, request, lookupIdentity](const QString &) {
                if (!guard)
                    return;
                guard->m_albumCoverLookupsActive = qMax(
                    0, guard->m_albumCoverLookupsActive - 1);
                if (request.generation == guard->m_searchGeneration)
                    guard->m_albumCoverLookups.remove(lookupIdentity);
                guard->startAlbumCoverLookups();
            });
    }
}

void SearchPage::setOnlineCover(const QString &stableIdentity, const QString &path)
{
    for (core::SearchResultItem &item : m_onlineItems) {
        if ((item.type == core::SearchItemType::Song
             || item.type == core::SearchItemType::Lyric)
            && item.song.stableIdentity() == stableIdentity
            && item.song.coverPath != path) {
            item.song.coverPath = path;
        }
    }
    for (core::SearchResultGroup &group : m_onlineGroups) {
        for (core::SearchResultVariant &variant : group.variants) {
            if (variant.item.song.stableIdentity() == stableIdentity)
                variant.item.song.coverPath = path;
        }
    }
    for (core::Song &song : m_onlineSongs) {
        if (song.stableIdentity() == stableIdentity && song.coverPath != path) {
            song.coverPath = path;
            m_onlineList->updateSong(song);
        }
    }
    for (core::Song &song : m_genericSongs) {
        if (song.stableIdentity() == stableIdentity && song.coverPath != path) {
            song.coverPath = path;
            m_genericSongList->updateSong(song);
        }
    }
}

} // namespace ui
