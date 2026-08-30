#include "core/SearchService.h"

#include <QtTest>

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

QTEST_GUILESS_MAIN(SearchServiceTest)
#include "tst_searchservice.moc"
