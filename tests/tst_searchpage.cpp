#include "core/LibraryService.h"
#include "core/MusicSourceRegistry.h"
#include "core/NeteaseApiClient.h"
#include "core/QqMusicSource.h"
#include "core/SettingsService.h"
#include "ui/SearchPage.h"

#include <QDir>
#include <QListWidget>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QToolTip>
#include <QUuid>
#include <QtTest>

using namespace core;
using namespace ui;

namespace {

SearchResultItem resultItem(SourceId source, const QString &remoteId, bool playable)
{
    SearchResultItem item;
    item.type = SearchItemType::Song;
    item.source = source;
    item.remoteId = remoteId;
    item.title = QStringLiteral("晴天");
    item.artist = QStringLiteral("周杰伦");
    item.album = QStringLiteral("叶惠美");
    item.durationMs = source == SourceId::Netease ? 269000 : 270000;
    item.sourceRank = 0;
    item.popularity = source == SourceId::Netease ? 99.0 : -1.0;
    item.playable = playable;
    if (!playable)
        item.availabilityError = QStringLiteral("版权限制");
    item.song.source = int(source);
    item.song.remoteId = remoteId;
    item.song.filePath = QStringLiteral("%1://%2")
                             .arg(source == SourceId::Netease
                                      ? QStringLiteral("netease")
                                      : QStringLiteral("qqmusic"), remoteId);
    item.song.title = item.title;
    item.song.artist = item.artist;
    item.song.album = item.album;
    item.song.durationMs = item.durationMs;
    return item;
}

template <typename Source>
class FixedSearchSource final : public Source
{
public:
    explicit FixedSearchSource(const SearchResultItem &item)
        : m_item(item)
    {
    }

    void search(const SearchRequest &request, MusicSource::SearchResponseFn ok,
                MusicSource::ErrFn err = {}) override
    {
        requests.append(request);
        if (failSearch) {
            if (err)
                err(searchError);
            return;
        }
        SearchResponse response;
        response.source = this->sourceId();
        response.category = request.category;
        response.generation = request.generation;
        response.offset = request.offset;
        response.items = { m_item };
        if (ok)
            ok(response);
    }

    void searchSuggestions(const QString &, int, MusicSource::SearchSuggestionsFn ok,
                           MusicSource::ErrFn = {}) override
    {
        ++suggestionRequestCount;
        if (ok)
            ok(suggestionResults);
    }

    void hotSearch(int, MusicSource::HotSearchFn ok,
                   MusicSource::ErrFn = {}) override
    {
        ++hotSearchRequestCount;
        if (ok)
            ok(hotSearchResults);
    }

    QList<SearchRequest> requests;
    QList<SearchSuggestion> suggestionResults;
    QList<HotSearchTerm> hotSearchResults;
    QString searchError = QStringLiteral("来源离线");
    int suggestionRequestCount = 0;
    int hotSearchRequestCount = 0;
    bool failSearch = false;

private:
    SearchResultItem m_item;
};

template <typename Source>
class DeferredSearchSource final : public Source
{
public:
    struct PendingSearch {
        SearchRequest request;
        MusicSource::SearchResponseFn ok;
        MusicSource::ErrFn err;
    };

    void search(const SearchRequest &request, MusicSource::SearchResponseFn ok,
                MusicSource::ErrFn err = {}) override
    {
        pending.append({ request, std::move(ok), std::move(err) });
    }

    void cancelSearch(quint64 generation) override
    {
        // 故意保留回调，用于模拟底层取消后仍迟到的网络响应。
        cancelledGenerations.append(generation);
    }

    void succeed(int index, const SearchResultItem &item)
    {
        QVERIFY(index >= 0 && index < pending.size());
        PendingSearch value = pending.takeAt(index);
        SearchResponse response;
        response.source = this->sourceId();
        response.category = value.request.category;
        response.generation = value.request.generation;
        response.offset = value.request.offset;
        response.items = { item };
        if (value.ok)
            value.ok(response);
    }

    QList<PendingSearch> pending;
    QList<quint64> cancelledGenerations;
};

} // namespace

class SearchPageTest : public QObject
{
    Q_OBJECT

private slots:
    void aggregatesSourcesAndExposesPreferredStableSong();
    void routesScopeAndCategoryToSelectedSource();
    void providesHistoryDiscoverySuggestionsAndKeyboardSelection();
    void fallsBackToCacheWithoutClearingHealthySource();
    void keepsNeteaseResultsWhenQqFails();
    void ignoresLateResponsesAndDestroyedPageCallbacks();
};

void SearchPageTest::aggregatesSourcesAndExposesPreferredStableSong()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("WyCloudForgeTests"));
    QCoreApplication::setApplicationName(QStringLiteral("SearchPage"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    const QString downloadDir = dir.filePath(QStringLiteral("downloads"));
    QVERIFY(QDir().mkpath(downloadDir));
    SettingsService::setOnlineDownloadDir(downloadDir);
    LibraryService::setDatabasePathOverride(dir.filePath(QStringLiteral("library.db")));
    LibraryService library;
    QVERIFY2(library.openDatabase(), qPrintable(library.lastError()));

    FixedSearchSource<NeteaseApiClient> netease(
        resultItem(SourceId::Netease, QStringLiteral("netease-sunny"), false));
    FixedSearchSource<QqMusicSource> qq(
        resultItem(SourceId::QqMusic, QStringLiteral("qq-sunny"), true));
    MusicSourceRegistry registry;
    registry.registerSource(&netease);
    registry.registerSource(&qq);

    SearchPage page;
    page.setSourceProvider(&netease, &library);
    page.setSourceRegistry(&registry);
    page.setOnlineSourceEnabled(SourceId::QqMusic, true);
    page.setSearchCategory(SearchCategory::All);
    page.performSearch(QStringLiteral("晴天"));

    const QList<SearchResultGroup> groups = page.onlineResultGroups();
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups.constFirst().variants.size(), 2);
    QCOMPARE(groups.constFirst().sourceConsensus, 2);
    QCOMPARE(groups.constFirst().preferredSong().sourceId(), SourceId::QqMusic);
    QCOMPARE(groups.constFirst().preferredSong().effectiveRemoteId(),
             QStringLiteral("qq-sunny"));

    const QList<Song> visibleSongs = page.currentSongs();
    QCOMPARE(visibleSongs.size(), 1);
    QCOMPARE(visibleSongs.constFirst().stableIdentity(), QStringLiteral("2:qq-sunny"));
}

void SearchPageTest::routesScopeAndCategoryToSelectedSource()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("WyCloudForgeTests"));
    QCoreApplication::setApplicationName(QStringLiteral("SearchPageRouting"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QSettings().clear();
    SettingsService::setOnlineDownloadDir(dir.filePath(QStringLiteral("downloads")));
    LibraryService::setDatabasePathOverride(dir.filePath(QStringLiteral("library.db")));
    LibraryService library;
    QVERIFY2(library.openDatabase(), qPrintable(library.lastError()));

    FixedSearchSource<NeteaseApiClient> netease(
        resultItem(SourceId::Netease, QStringLiteral("netease-routing"), true));
    FixedSearchSource<QqMusicSource> qq(
        resultItem(SourceId::QqMusic, QStringLiteral("qq-routing"), true));
    MusicSourceRegistry registry;
    registry.registerSource(&netease);
    registry.registerSource(&qq);

    SearchPage page;
    page.setSourceProvider(&netease, &library);
    page.setSourceRegistry(&registry);
    page.setOnlineSourceEnabled(SourceId::QqMusic, true);
    page.setSearchScope(SearchScope::Netease);
    page.setSearchCategory(SearchCategory::Albums);
    page.performSearch(QStringLiteral("叶惠美"));

    QCOMPARE(netease.requests.size(), 1);
    QCOMPARE(qq.requests.size(), 0);
    QCOMPARE(netease.requests.constFirst().scope, SearchScope::Netease);
    QCOMPARE(netease.requests.constFirst().category, SearchCategory::Albums);

    page.setSearchScope(SearchScope::QqMusic);
    QCOMPARE(netease.requests.size(), 1);
    QCOMPARE(qq.requests.size(), 1);
    QCOMPARE(qq.requests.constFirst().scope, SearchScope::QqMusic);
    QCOMPARE(qq.requests.constFirst().category, SearchCategory::Albums);
}

void SearchPageTest::providesHistoryDiscoverySuggestionsAndKeyboardSelection()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("WyCloudForgeTests"));
    QCoreApplication::setApplicationName(QStringLiteral("SearchPageAssistant"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QSettings().clear();
    SettingsService::setOnlineDownloadDir(dir.filePath(QStringLiteral("downloads")));
    LibraryService::setDatabasePathOverride(dir.filePath(QStringLiteral("library.db")));
    LibraryService library;
    QVERIFY2(library.openDatabase(), qPrintable(library.lastError()));

    const QString token = QStringLiteral("阶段五")
        + QUuid::createUuid().toString(QUuid::Id128).left(8);
    FixedSearchSource<NeteaseApiClient> netease(
        resultItem(SourceId::Netease, QStringLiteral("netease-assistant"), true));
    FixedSearchSource<QqMusicSource> qq(
        resultItem(SourceId::QqMusic, QStringLiteral("qq-assistant"), true));
    netease.hotSearchResults = {
        { SourceId::Netease, token + QStringLiteral("热搜"), QStringLiteral("热门"), 100.0, 0 }
    };
    netease.suggestionResults = {
        { SourceId::Netease, SearchItemType::Song, token + QStringLiteral("网易"),
          QStringLiteral("在线联想"), QStringLiteral("netease-suggestion") }
    };
    qq.suggestionResults = {
        { SourceId::QqMusic, SearchItemType::Artist, token + QStringLiteral("QQ"),
          QStringLiteral("在线联想"), QStringLiteral("qq-suggestion") }
    };
    MusicSourceRegistry registry;
    registry.registerSource(&netease);
    registry.registerSource(&qq);

    Song local;
    local.id = 1;
    local.filePath = dir.filePath(QStringLiteral("local.mp3"));
    local.title = token + QStringLiteral("本地");
    local.artist = QStringLiteral("本地歌手");

    SearchPage page;
    page.setSourceProvider(&netease, &library);
    page.setSourceRegistry(&registry);
    page.setOnlineSourceEnabled(SourceId::QqMusic, true);
    page.setLocalSongs({ local });
    page.performSearch(token + QStringLiteral("历史"));
    page.showSearchAssistant();

    QStringList queries = page.assistantQueries();
    QVERIFY(queries.contains(token + QStringLiteral("历史")));
    QVERIFY(queries.contains(token + QStringLiteral("热搜")));
    QVERIFY(netease.hotSearchRequestCount > 0);

    page.previewSearchText(token);
    QTRY_VERIFY_WITH_TIMEOUT(netease.suggestionRequestCount > 0, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(qq.suggestionRequestCount > 0, 1000);
    queries = page.assistantQueries();
    QVERIFY(queries.contains(token + QStringLiteral("本地")));
    QVERIFY(queries.contains(token + QStringLiteral("网易")));
    QVERIFY(queries.contains(token + QStringLiteral("QQ")));

    page.moveSearchAssistantSelection(1);
    QCOMPARE(page.resolvedSearchText(QStringLiteral("后备词")),
             page.assistantQueries().constFirst());

    auto *assistant = page.findChild<QListWidget *>(QStringLiteral("searchAssistantList"));
    QVERIFY(assistant);
    QCOMPARE(assistant->selectionMode(), QAbstractItemView::NoSelection);
    QVERIFY(assistant->currentRow() >= 0);
    QVERIFY(assistant->selectedItems().isEmpty());
    QVERIFY(!assistant->currentItem()->toolTip().isEmpty());

    page.show();
    QApplication::processEvents();
    QToolTip::showText(page.mapToGlobal(QPoint(20, 20)), QStringLiteral("搜索提示"), &page);
    QTRY_VERIFY(QToolTip::isVisible());
    page.hide();
    QTRY_VERIFY(!QToolTip::isVisible());
    QCOMPARE(assistant->currentRow(), -1);
}

void SearchPageTest::fallsBackToCacheWithoutClearingHealthySource()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("WyCloudForgeTests"));
    QCoreApplication::setApplicationName(QStringLiteral("SearchPageCache"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QSettings().clear();
    SettingsService::setOnlineDownloadDir(dir.filePath(QStringLiteral("downloads")));
    LibraryService::setDatabasePathOverride(dir.filePath(QStringLiteral("library.db")));
    LibraryService library;
    QVERIFY2(library.openDatabase(), qPrintable(library.lastError()));
    const QString query = QStringLiteral("缓存降级")
        + QUuid::createUuid().toString(QUuid::Id128);

    {
        FixedSearchSource<NeteaseApiClient> netease(
            resultItem(SourceId::Netease, QStringLiteral("netease-cached"), true));
        MusicSourceRegistry registry;
        registry.registerSource(&netease);
        SearchPage page;
        page.setSourceProvider(&netease, &library);
        page.setSourceRegistry(&registry);
        page.performSearch(query);
        QCOMPARE(page.sourceState(SourceId::Netease).state, SearchLoadState::Ready);
        QVERIFY(!page.sourceState(SourceId::Netease).fromCache);
        QCOMPARE(page.onlineResultItems().size(), 1);
    }

    FixedSearchSource<NeteaseApiClient> offlineNetease(
        resultItem(SourceId::Netease, QStringLiteral("unused"), true));
    offlineNetease.failSearch = true;
    FixedSearchSource<QqMusicSource> healthyQq(
        resultItem(SourceId::QqMusic, QStringLiteral("qq-live"), true));
    MusicSourceRegistry registry;
    registry.registerSource(&offlineNetease);
    registry.registerSource(&healthyQq);
    SearchPage page;
    page.setSourceProvider(&offlineNetease, &library);
    page.setSourceRegistry(&registry);
    page.setOnlineSourceEnabled(SourceId::QqMusic, true);
    page.performSearch(query);

    const SearchSourceState neteaseState = page.sourceState(SourceId::Netease);
    QCOMPARE(neteaseState.state, SearchLoadState::Ready);
    QVERIFY(neteaseState.fromCache);
    QVERIFY(neteaseState.error.contains(QStringLiteral("显示缓存")));
    QCOMPARE(page.sourceState(SourceId::QqMusic).state, SearchLoadState::Ready);
    QCOMPARE(page.onlineResultItems().size(), 2);
    QCOMPARE(page.onlineResultGroups().size(), 1);
    QCOMPARE(page.onlineResultGroups().constFirst().variants.size(), 2);
}

void SearchPageTest::keepsNeteaseResultsWhenQqFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("WyCloudForgeTests"));
    QCoreApplication::setApplicationName(QStringLiteral("SearchPageQqOffline"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QSettings().clear();
    SettingsService::setOnlineDownloadDir(dir.filePath(QStringLiteral("downloads")));
    LibraryService::setDatabasePathOverride(dir.filePath(QStringLiteral("library.db")));
    LibraryService library;
    QVERIFY2(library.openDatabase(), qPrintable(library.lastError()));

    FixedSearchSource<NeteaseApiClient> healthyNetease(
        resultItem(SourceId::Netease, QStringLiteral("netease-live"), true));
    FixedSearchSource<QqMusicSource> offlineQq(
        resultItem(SourceId::QqMusic, QStringLiteral("unused"), true));
    offlineQq.failSearch = true;
    MusicSourceRegistry registry;
    registry.registerSource(&healthyNetease);
    registry.registerSource(&offlineQq);

    SearchPage page;
    page.setSourceProvider(&healthyNetease, &library);
    page.setSourceRegistry(&registry);
    page.setOnlineSourceEnabled(SourceId::QqMusic, true);
    page.performSearch(QStringLiteral("QQ 离线反向隔离"));

    QCOMPARE(page.sourceState(SourceId::Netease).state, SearchLoadState::Ready);
    QCOMPARE(page.sourceState(SourceId::QqMusic).state, SearchLoadState::Failed);
    QCOMPARE(page.onlineResultItems().size(), 1);
    QCOMPARE(page.currentSongs().size(), 1);
    QCOMPARE(page.currentSongs().constFirst().stableIdentity(),
             QStringLiteral("1:netease-live"));
}

void SearchPageTest::ignoresLateResponsesAndDestroyedPageCallbacks()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("WyCloudForgeTests"));
    QCoreApplication::setApplicationName(QStringLiteral("SearchPageLateResponse"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QSettings().clear();
    SettingsService::setOnlineDownloadDir(dir.filePath(QStringLiteral("downloads")));
    LibraryService::setDatabasePathOverride(dir.filePath(QStringLiteral("library.db")));
    LibraryService library;
    QVERIFY2(library.openDatabase(), qPrintable(library.lastError()));

    DeferredSearchSource<NeteaseApiClient> source;
    MusicSourceRegistry registry;
    registry.registerSource(&source);

    SearchPage page;
    page.setSourceProvider(&source, &library);
    page.setSourceRegistry(&registry);
    page.setSearchScope(SearchScope::Netease);
    page.performSearch(QStringLiteral("旧查询"));
    page.performSearch(QStringLiteral("新查询"));
    QCOMPARE(source.pending.size(), 2);
    QCOMPARE(source.cancelledGenerations.size(), 1);

    source.succeed(1, resultItem(SourceId::Netease,
                                 QStringLiteral("new-response"), true));
    QCOMPARE(page.currentSongs().size(), 1);
    QCOMPARE(page.currentSongs().constFirst().effectiveRemoteId(),
             QStringLiteral("new-response"));

    source.succeed(0, resultItem(SourceId::Netease,
                                 QStringLiteral("late-old-response"), true));
    QCOMPARE(page.currentSongs().size(), 1);
    QCOMPARE(page.currentSongs().constFirst().effectiveRemoteId(),
             QStringLiteral("new-response"));

    auto *destroyedPage = new SearchPage;
    destroyedPage->setSourceProvider(&source, &library);
    destroyedPage->setSourceRegistry(&registry);
    destroyedPage->setSearchScope(SearchScope::Netease);
    destroyedPage->performSearch(QStringLiteral("页面销毁"));
    QCOMPARE(source.pending.size(), 1);
    delete destroyedPage;
    source.succeed(0, resultItem(SourceId::Netease,
                                 QStringLiteral("after-destroy"), true));
    QVERIFY(source.pending.isEmpty());
}

QTEST_MAIN(SearchPageTest)
#include "tst_searchpage.moc"
