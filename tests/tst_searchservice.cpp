#include "core/SearchAggregator.h"
#include "core/SearchCache.h"
#include "core/SearchService.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

using namespace core;

namespace {

Song makeSong(const QString &title, const QString &artist = {}, const QString &album = {})
{
    Song song;
    song.title = title;
    song.artist = artist;
    song.album = album;
    song.filePath = QStringLiteral("C:/music/%1.mp3").arg(title);
    return song;
}

SearchResultItem makeOnlineItem(SourceId source, const QString &remoteId,
                                const QString &title, const QString &artist,
                                qint64 durationMs = 200000, int rank = 0,
                                double popularity = -1.0)
{
    SearchResultItem item;
    item.type = SearchItemType::Song;
    item.source = source;
    item.remoteId = remoteId;
    item.title = title;
    item.artist = artist;
    item.durationMs = durationMs;
    item.sourceRank = rank;
    item.popularity = popularity;
    item.song.source = int(source);
    item.song.remoteId = remoteId;
    item.song.filePath = QStringLiteral("%1://%2")
                             .arg(source == SourceId::Netease
                                      ? QStringLiteral("netease")
                                      : QStringLiteral("qqmusic"), remoteId);
    item.song.title = title;
    item.song.artist = artist;
    item.song.durationMs = durationMs;
    return item;
}

} // namespace

class SearchServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void ranksTitleMatchesBeforeMetadataMatches();
    void matchesArtistTitleCombination();
    void normalizesUnicodeAndCase();
    void capsCandidatesButKeepsTotalCount();
    void returnsEveryNonOverlappingHighlight();
    void discardsOlderAsyncGeneration();
    void searchesRetainedOnlineMetadata();
    void exactTitleOutranksHotterWeakMatch();
    void normalizesHeatInsideEachSource();
    void rewardsCrossSourceConsensusAfterHeat();
    void groupsSameRecordingAndRejectsVersionMismatches();
    void selectsDownloadedThenPlayableVariant();
    void producesStableFinalOrder();
    void rejectsAmbiguousVariantsRegardlessOfArrivalOrder();
    void honorsOptionalSortModes();
    void persistsSearchCacheAndHistory();
    void ignoresCorruptCacheAndRecoversAfterRewrite();
};

void SearchServiceTest::ranksTitleMatchesBeforeMetadataMatches()
{
    const QList<Song> songs = {
        makeSong(QStringLiteral("别的歌"), QStringLiteral("晴天")),
        makeSong(QStringLiteral("雨后晴天")),
        makeSong(QStringLiteral("晴天之后")),
        makeSong(QStringLiteral("晴天")),
        makeSong(QStringLiteral("另一首"), {}, QStringLiteral("晴天"))
    };

    const SearchService::Batch batch = SearchService::searchBatch(songs, QStringLiteral("晴天"));
    QCOMPARE(batch.totalMatches, songs.size());
    QCOMPARE(batch.results.size(), songs.size());
    QCOMPARE(batch.results.at(0).index, 3);
    QCOMPARE(batch.results.at(0).score, 1000);
    QCOMPARE(batch.results.at(1).index, 2);
    QCOMPARE(batch.results.at(1).score, 900);
    QCOMPARE(batch.results.at(2).index, 1);
    QCOMPARE(batch.results.at(2).score, 800);
    QCOMPARE(batch.results.at(3).index, 0);
    QCOMPARE(batch.results.at(3).score, 700);
    QCOMPARE(batch.results.at(4).index, 4);
    QCOMPARE(batch.results.at(4).score, 550);
}

void SearchServiceTest::matchesArtistTitleCombination()
{
    const QList<Song> songs = {
        makeSong(QStringLiteral("晴天"), QStringLiteral("周杰伦")),
        makeSong(QStringLiteral("晴朗的一天"), QStringLiteral("其他歌手"))
    };

    const SearchService::Batch spaced = SearchService::searchBatch(
        songs, QStringLiteral("周杰伦 晴天"));
    QCOMPARE(spaced.totalMatches, 1);
    QCOMPARE(spaced.results.constFirst().index, 0);
    QCOMPARE(spaced.results.constFirst().score, 750);

    const SearchService::Batch joined = SearchService::searchBatch(
        songs, QStringLiteral("周杰伦晴天"));
    QCOMPARE(joined.totalMatches, 1);
    QCOMPARE(joined.results.constFirst().index, 0);
}

void SearchServiceTest::normalizesUnicodeAndCase()
{
    const QList<Song> songs = {
        makeSong(QStringLiteral("ＡＢＣ")),
        makeSong(QStringLiteral("Café"))
    };

    const SearchService::Batch width = SearchService::searchBatch(songs, QStringLiteral("abc"));
    QCOMPARE(width.totalMatches, 1);
    QCOMPARE(width.results.constFirst().index, 0);
    QCOMPARE(width.results.constFirst().score, 1000);

    const SearchService::Batch caseFolded = SearchService::searchBatch(
        songs, QStringLiteral("CAFÉ"));
    QCOMPARE(caseFolded.totalMatches, 1);
    QCOMPARE(caseFolded.results.constFirst().index, 1);
}

void SearchServiceTest::capsCandidatesButKeepsTotalCount()
{
    QList<Song> songs;
    songs.reserve(500);
    for (int i = 0; i < 500; ++i)
        songs.append(makeSong(QStringLiteral("命中歌曲 %1").arg(i)));

    const SearchService::Batch batch = SearchService::searchBatch(
        songs, QStringLiteral("命中"), 25);
    QCOMPARE(batch.totalMatches, 500);
    QCOMPARE(batch.results.size(), 25);
    for (int i = 0; i < batch.results.size(); ++i)
        QCOMPARE(batch.results.at(i).index, i);
}

void SearchServiceTest::returnsEveryNonOverlappingHighlight()
{
    const QList<QPair<int, int>> ranges = SearchService::highlightRanges(
        QStringLiteral("晴天又是晴天"), QStringLiteral("晴天"));
    const QList<QPair<int, int>> expected = { { 0, 2 }, { 4, 6 } };
    QCOMPARE(ranges, expected);
}

void SearchServiceTest::discardsOlderAsyncGeneration()
{
    SearchService service;
    QList<Song> oldSongs;
    oldSongs.reserve(50000);
    for (int i = 0; i < 50000; ++i)
        oldSongs.append(makeSong(QStringLiteral("旧关键词 %1").arg(i)));
    service.updateSnapshot(oldSongs);

    QList<QPair<quint64, QString>> completions;
    QList<QList<Song>> completedSongs;
    connect(&service, &SearchService::searchFinished, this,
            [&](quint64 generation, const QString &query,
                const QList<Song> &songs, int) {
        completions.append({ generation, query });
        completedSongs.append(songs);
    });
    service.searchAsync(QStringLiteral("旧关键词"), 200);

    const Song current = makeSong(QStringLiteral("新关键词"));
    service.updateSnapshot({ current });
    const quint64 currentGeneration = service.searchAsync(QStringLiteral("新关键词"), 200);

    QTRY_COMPARE_WITH_TIMEOUT(completions.size(), 1, 5000);
    QCOMPARE(completions.constFirst().first, currentGeneration);
    QCOMPARE(completions.constFirst().second, QStringLiteral("新关键词"));
    QCOMPARE(completedSongs.constFirst().size(), 1);
    QCOMPARE(completedSongs.constFirst().constFirst().title, current.title);
    QTest::qWait(100);
    QCOMPARE(completions.size(), 1);
}

void SearchServiceTest::searchesRetainedOnlineMetadata()
{
    Song online = makeSong(QStringLiteral("已下架但保留的歌曲"),
                           QStringLiteral("远端歌手"), QStringLiteral("远端专辑"));
    online.source = int(SourceId::QqMusic);
    online.remoteId = QStringLiteral("retained-online-record");
    online.filePath = QStringLiteral("qqmusic://retained-online-record");
    online.cachePath.clear();
    online.downloadPath.clear();

    QVERIFY(!online.isLocallyAvailable());
    const SearchService::Batch batch = SearchService::searchBatch(
        { online }, QStringLiteral("远端歌手"));
    QCOMPARE(batch.totalMatches, 1);
    QCOMPARE(batch.results.constFirst().index, 0);
}

void SearchServiceTest::exactTitleOutranksHotterWeakMatch()
{
    SearchResultItem exact = makeOnlineItem(SourceId::Netease, QStringLiteral("exact"),
                                            QStringLiteral("晴天"), QStringLiteral("其他歌手"),
                                            200000, 20, 1.0);
    SearchResultItem weak = makeOnlineItem(SourceId::Netease, QStringLiteral("weak"),
                                           QStringLiteral("另一首歌"), QStringLiteral("晴天"),
                                           200000, 0, 1000000.0);
    SearchAggregateOptions options;
    options.query = QStringLiteral("晴天");
    const QList<SearchResultGroup> groups = SearchAggregator::aggregate({ weak, exact }, options);

    QCOMPARE(groups.size(), 2);
    QCOMPARE(groups.constFirst().preferredItem().remoteId, QStringLiteral("exact"));
    QCOMPARE(groups.constFirst().relevanceScore, 1000);
    QVERIFY(groups.at(1).heatPercentile > groups.constFirst().heatPercentile);
}

void SearchServiceTest::normalizesHeatInsideEachSource()
{
    const SearchResultItem nTop = makeOnlineItem(
        SourceId::Netease, QStringLiteral("n-top"), QStringLiteral("测试甲"),
        QStringLiteral("歌手甲"), 200000, 0, 1000000000.0);
    const SearchResultItem nLow = makeOnlineItem(
        SourceId::Netease, QStringLiteral("n-low"), QStringLiteral("测试乙"),
        QStringLiteral("歌手乙"), 200000, 1, 100.0);
    const SearchResultItem qTop = makeOnlineItem(
        SourceId::QqMusic, QStringLiteral("q-top"), QStringLiteral("测试丙"),
        QStringLiteral("歌手丙"), 200000, 0, 100.0);
    const SearchResultItem qLow = makeOnlineItem(
        SourceId::QqMusic, QStringLiteral("q-low"), QStringLiteral("测试丁"),
        QStringLiteral("歌手丁"), 200000, 1, 1.0);
    SearchAggregateOptions options;
    options.query = QStringLiteral("测试");
    const QList<SearchResultGroup> groups = SearchAggregator::aggregate(
        { nLow, qLow, qTop, nTop }, options);

    QHash<QString, double> heat;
    for (const SearchResultGroup &group : groups)
        heat.insert(group.preferredItem().remoteId, group.heatPercentile);
    QCOMPARE(heat.value(QStringLiteral("n-top")), 1.0);
    QCOMPARE(heat.value(QStringLiteral("q-top")), 1.0);
    QCOMPARE(heat.value(QStringLiteral("n-low")), 0.0);
    QCOMPARE(heat.value(QStringLiteral("q-low")), 0.0);
}

void SearchServiceTest::rewardsCrossSourceConsensusAfterHeat()
{
    SearchResultItem netease = makeOnlineItem(
        SourceId::Netease, QStringLiteral("n-consensus"), QStringLiteral("晴天"),
        QStringLiteral("周杰伦"), 240000);
    SearchResultItem qq = makeOnlineItem(
        SourceId::QqMusic, QStringLiteral("q-consensus"), QStringLiteral("晴天"),
        QStringLiteral("周杰伦"), 242000);
    SearchResultItem local;
    local.type = SearchItemType::Song;
    local.source = SourceId::Local;
    local.title = QStringLiteral("晴空");
    local.artist = QStringLiteral("本地歌手");
    local.song = makeSong(local.title, local.artist);
    local.song.filePath = QStringLiteral("C:/missing-for-ranking.wav");

    SearchAggregateOptions options;
    options.query = QStringLiteral("晴");
    const QList<SearchResultGroup> groups = SearchAggregator::aggregate(
        { local, qq, netease }, options);

    QCOMPARE(groups.size(), 2);
    QCOMPARE(groups.constFirst().sourceConsensus, 2);
    QVERIFY(groups.constFirst().hasSource(SourceId::Netease));
    QVERIFY(groups.constFirst().hasSource(SourceId::QqMusic));
}

void SearchServiceTest::groupsSameRecordingAndRejectsVersionMismatches()
{
    const SearchResultItem studio = makeOnlineItem(
        SourceId::Netease, QStringLiteral("n-studio"), QStringLiteral("晴天"),
        QStringLiteral("周杰伦"), 240000);
    const SearchResultItem same = makeOnlineItem(
        SourceId::QqMusic, QStringLiteral("q-studio"), QStringLiteral("晴天"),
        QStringLiteral("周杰伦/合作歌手"), 242500);
    const SearchResultItem live = makeOnlineItem(
        SourceId::QqMusic, QStringLiteral("q-live"), QStringLiteral("晴天 (Live)"),
        QStringLiteral("周杰伦"), 240000);
    const SearchResultItem remix = makeOnlineItem(
        SourceId::QqMusic, QStringLiteral("q-remix"), QStringLiteral("晴天 Remix"),
        QStringLiteral("周杰伦"), 240000);
    const SearchResultItem cover = makeOnlineItem(
        SourceId::QqMusic, QStringLiteral("q-cover"), QStringLiteral("晴天"),
        QStringLiteral("翻唱歌手"), 240000);
    const SearchResultItem instrumental = makeOnlineItem(
        SourceId::QqMusic, QStringLiteral("q-inst"), QStringLiteral("晴天 伴奏"),
        QStringLiteral("周杰伦"), 240000);
    const SearchResultItem differentRecording = makeOnlineItem(
        SourceId::QqMusic, QStringLiteral("q-long"), QStringLiteral("晴天"),
        QStringLiteral("周杰伦"), 244000);

    QVERIFY(SearchAggregator::sameRecording(studio, same));
    QVERIFY(!SearchAggregator::sameRecording(studio, live));
    QVERIFY(!SearchAggregator::sameRecording(studio, remix));
    QVERIFY(!SearchAggregator::sameRecording(studio, cover));
    QVERIFY(!SearchAggregator::sameRecording(studio, instrumental));
    QVERIFY(!SearchAggregator::sameRecording(studio, differentRecording));

    SearchAggregateOptions options;
    options.query = QStringLiteral("晴天");
    const QList<SearchResultGroup> groups = SearchAggregator::aggregate({ studio, same }, options);
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups.constFirst().variants.size(), 2);
}

void SearchServiceTest::selectsDownloadedThenPlayableVariant()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString downloadedPath = dir.filePath(QStringLiteral("downloaded.mp3"));
    QFile downloaded(downloadedPath);
    QVERIFY(downloaded.open(QIODevice::WriteOnly));
    QCOMPARE(downloaded.write("audio"), qint64(5));
    downloaded.close();

    SearchResultItem netease = makeOnlineItem(
        SourceId::Netease, QStringLiteral("n-choice"), QStringLiteral("风衣"),
        QStringLiteral("孙燕姿"), 210000, 0, 1000.0);
    SearchResultItem qq = makeOnlineItem(
        SourceId::QqMusic, QStringLiteral("q-choice"), QStringLiteral("风衣"),
        QStringLiteral("孙燕姿"), 211000, 10, 1.0);
    qq.song.downloadPath = downloadedPath;
    SearchAggregateOptions options;
    options.query = QStringLiteral("风衣");
    options.preferredSource = SourceId::Netease;

    QList<SearchResultGroup> groups = SearchAggregator::aggregate({ netease, qq }, options);
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups.constFirst().preferredItem().remoteId, QStringLiteral("q-choice"));

    qq.song.downloadPath.clear();
    netease.playable = false;
    netease.availabilityError = QStringLiteral("版权限制");
    groups = SearchAggregator::aggregate({ netease, qq }, options);
    QCOMPARE(groups.constFirst().preferredItem().remoteId, QStringLiteral("q-choice"));
}

void SearchServiceTest::producesStableFinalOrder()
{
    const QList<SearchResultItem> items = {
        makeOnlineItem(SourceId::Netease, QStringLiteral("n-a"), QStringLiteral("测试甲"),
                       QStringLiteral("甲"), 200000, 0),
        makeOnlineItem(SourceId::QqMusic, QStringLiteral("q-b"), QStringLiteral("测试乙"),
                       QStringLiteral("乙"), 200000, 0),
        makeOnlineItem(SourceId::Netease, QStringLiteral("n-c"), QStringLiteral("测试丙"),
                       QStringLiteral("丙"), 200000, 1)
    };
    SearchAggregateOptions options;
    options.query = QStringLiteral("测试");
    const QList<SearchResultGroup> forward = SearchAggregator::aggregate(items, options);
    QList<SearchResultItem> reversed = items;
    std::reverse(reversed.begin(), reversed.end());
    const QList<SearchResultGroup> backward = SearchAggregator::aggregate(reversed, options);

    QStringList forwardIds;
    QStringList backwardIds;
    for (const SearchResultGroup &group : forward)
        forwardIds.append(group.identity);
    for (const SearchResultGroup &group : backward)
        backwardIds.append(group.identity);
    QCOMPARE(forwardIds, backwardIds);
}

void SearchServiceTest::rejectsAmbiguousVariantsRegardlessOfArrivalOrder()
{
    const QList<SearchResultItem> items = {
        makeOnlineItem(SourceId::Netease, QStringLiteral("n-first"),
                       QStringLiteral("晴天"), QStringLiteral("周杰伦"), 240000),
        makeOnlineItem(SourceId::Netease, QStringLiteral("n-second"),
                       QStringLiteral("晴天"), QStringLiteral("周杰伦"), 241000),
        makeOnlineItem(SourceId::QqMusic, QStringLiteral("q-only"),
                       QStringLiteral("晴天"), QStringLiteral("周杰伦"), 240500)
    };
    SearchAggregateOptions options;
    options.query = QStringLiteral("晴天");

    const QList<SearchResultGroup> forward = SearchAggregator::aggregate(items, options);
    QList<SearchResultItem> reversed = items;
    std::reverse(reversed.begin(), reversed.end());
    const QList<SearchResultGroup> backward = SearchAggregator::aggregate(reversed, options);

    QCOMPARE(forward.size(), 3);
    QCOMPARE(backward.size(), 3);
    for (const SearchResultGroup &group : forward)
        QCOMPARE(group.variants.size(), 1);
    QStringList forwardIds;
    QStringList backwardIds;
    for (const SearchResultGroup &group : forward)
        forwardIds.append(group.identity);
    for (const SearchResultGroup &group : backward)
        backwardIds.append(group.identity);
    QCOMPARE(forwardIds, backwardIds);
}

void SearchServiceTest::honorsOptionalSortModes()
{
    SearchResultItem exact = makeOnlineItem(
        SourceId::Netease, QStringLiteral("mode-exact"), QStringLiteral("晴天"),
        QStringLiteral("其他歌手"), 200000, 20, 1.0);
    SearchResultItem popularArtist = makeOnlineItem(
        SourceId::Netease, QStringLiteral("mode-popular"), QStringLiteral("另一首歌"),
        QStringLiteral("晴天"), 200000, 0, 1000000.0);
    SearchResultItem unrelated = makeOnlineItem(
        SourceId::QqMusic, QStringLiteral("mode-unrelated"), QStringLiteral("无关热门"),
        QStringLiteral("其他歌手"), 200000, 0, 1000000.0);

    SearchAggregateOptions options;
    options.query = QStringLiteral("晴天");
    options.sortMode = SearchSortMode::Popularity;
    QList<SearchResultGroup> groups = SearchAggregator::aggregate(
        { unrelated, exact, popularArtist }, options);
    QCOMPARE(groups.constFirst().preferredItem().remoteId, QStringLiteral("mode-popular"));
    QCOMPARE(groups.constLast().preferredItem().remoteId, QStringLiteral("mode-unrelated"));

    exact.song.lastPlayedMs = 100;
    popularArtist.song.lastPlayedMs = 500;
    options.sortMode = SearchSortMode::RecentPlayed;
    groups = SearchAggregator::aggregate({ exact, popularArtist }, options);
    QCOMPARE(groups.constFirst().preferredItem().remoteId, QStringLiteral("mode-popular"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString cachedPath = dir.filePath(QStringLiteral("cached.mp3"));
    QFile cached(cachedPath);
    QVERIFY(cached.open(QIODevice::WriteOnly));
    QCOMPARE(cached.write("cache"), qint64(5));
    cached.close();
    popularArtist.song.cachePath = cachedPath;
    options.sortMode = SearchSortMode::LocalFirst;
    groups = SearchAggregator::aggregate({ exact, popularArtist }, options);
    QCOMPARE(groups.constFirst().preferredItem().remoteId, QStringLiteral("mode-popular"));
}

void SearchServiceTest::persistsSearchCacheAndHistory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QCoreApplication::setOrganizationName(QStringLiteral("WyCloudForgeTests"));
    QCoreApplication::setApplicationName(QStringLiteral("SearchCache"));

    SearchCache cache(dir.filePath(QStringLiteral("cache")));
    SearchRequest request;
    request.keywords = QStringLiteral("  晴天  ");
    request.category = SearchCategory::Songs;
    request.scope = SearchScope::QqMusic;
    request.limit = 30;
    request.offset = 0;
    request.generation = 41;
    SearchResponse response;
    response.source = SourceId::QqMusic;
    response.category = SearchCategory::Songs;
    response.offset = 0;
    response.hasMore = true;
    response.generation = request.generation;
    response.items = { makeOnlineItem(SourceId::QqMusic, QStringLiteral("qq-cache"),
                                      QStringLiteral("晴天"), QStringLiteral("周杰伦"),
                                      240000, 3, 99.0) };
    QVERIFY(cache.storeResponse(request, response));

    request.keywords = QStringLiteral("晴天");
    request.generation = 42;
    SearchResponse restored;
    bool fresh = false;
    QVERIFY(cache.loadResponse(request, SourceId::QqMusic, &restored, &fresh));
    QVERIFY(fresh);
    QCOMPARE(restored.generation, quint64(42));
    QCOMPARE(restored.items.size(), 1);
    QCOMPARE(restored.items.constFirst().stableIdentity(), QStringLiteral("2:qq-cache"));
    QCOMPARE(restored.items.constFirst().popularity, 99.0);

    const QList<SearchSuggestion> suggestions = {
        { SourceId::QqMusic, SearchItemType::Song, QStringLiteral("晴天"),
          QStringLiteral("周杰伦"), QStringLiteral("qq-cache") }
    };
    QVERIFY(cache.storeSuggestions(SourceId::QqMusic, request.keywords, suggestions));
    QList<SearchSuggestion> restoredSuggestions;
    QVERIFY(cache.loadSuggestions(SourceId::QqMusic, QStringLiteral(" 晴天 "),
                                  &restoredSuggestions));
    QCOMPARE(restoredSuggestions.size(), 1);
    QCOMPARE(restoredSuggestions.constFirst().remoteId, QStringLiteral("qq-cache"));

    const QList<HotSearchTerm> terms = {
        { SourceId::Netease, QStringLiteral("风衣"), QStringLiteral("热搜"), 100.0, 0 }
    };
    QVERIFY(cache.storeHotTerms(SourceId::Netease, terms));
    QList<HotSearchTerm> restoredTerms;
    QVERIFY(cache.loadHotTerms(SourceId::Netease, &restoredTerms));
    QCOMPARE(restoredTerms.size(), 1);
    QCOMPARE(restoredTerms.constFirst().text, QStringLiteral("风衣"));

    cache.addHistory(QStringLiteral("晴天"));
    cache.addHistory(QStringLiteral("风衣"));
    cache.addHistory(QStringLiteral(" 晴天 "));
    QCOMPARE(cache.history(), QStringList({ QStringLiteral("晴天"), QStringLiteral("风衣") }));
    cache.clearHistory();
    QVERIFY(cache.history().isEmpty());
}

void SearchServiceTest::ignoresCorruptCacheAndRecoversAfterRewrite()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SearchCache cache(dir.filePath(QStringLiteral("cache")));

    SearchRequest request;
    request.keywords = QStringLiteral("损坏缓存恢复");
    request.category = SearchCategory::Songs;
    request.scope = SearchScope::QqMusic;
    request.limit = 30;
    request.generation = 61;

    SearchResponse response;
    response.source = SourceId::QqMusic;
    response.category = request.category;
    response.generation = request.generation;
    response.items = { makeOnlineItem(SourceId::QqMusic,
                                      QStringLiteral("qq-corrupt-cache"),
                                      QStringLiteral("缓存恢复"),
                                      QStringLiteral("测试歌手")) };
    QVERIFY(cache.storeResponse(request, response));

    QDir resultsDir(cache.rootPath() + QStringLiteral("/results"));
    const QStringList entries = resultsDir.entryList(
        { QStringLiteral("*.json") }, QDir::Files);
    QCOMPARE(entries.size(), 1);
    QFile damaged(resultsDir.filePath(entries.constFirst()));
    QVERIFY(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(damaged.write("{not-valid-json"), qint64(15));
    damaged.close();

    SearchResponse restored;
    bool fresh = true;
    QVERIFY(!cache.loadResponse(request, SourceId::QqMusic, &restored, &fresh));
    QVERIFY(!fresh);

    request.generation = 62;
    response.generation = request.generation;
    QVERIFY(cache.storeResponse(request, response));
    QVERIFY(cache.loadResponse(request, SourceId::QqMusic, &restored, &fresh));
    QVERIFY(fresh);
    QCOMPARE(restored.generation, quint64(62));
    QCOMPARE(restored.items.size(), 1);
    QCOMPARE(restored.items.constFirst().stableIdentity(),
             QStringLiteral("2:qq-corrupt-cache"));
}

QTEST_GUILESS_MAIN(SearchServiceTest)
#include "tst_searchservice.moc"
