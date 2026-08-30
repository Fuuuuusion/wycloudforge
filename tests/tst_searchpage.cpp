#include "core/LibraryService.h"
#include "core/MusicSourceRegistry.h"
#include "core/NeteaseApiClient.h"
#include "core/QqMusicSource.h"
#include "core/SettingsService.h"
#include "ui/SearchPage.h"

#include <QDir>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
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
                MusicSource::ErrFn = {}) override
    {
        SearchResponse response;
        response.source = this->sourceId();
        response.category = request.category;
        response.generation = request.generation;
        response.offset = request.offset;
        response.items = { m_item };
        if (ok)
            ok(response);
    }

private:
    SearchResultItem m_item;
};

} // namespace

class SearchPageTest : public QObject
{
    Q_OBJECT

private slots:
    void aggregatesSourcesAndExposesPreferredStableSong();
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
    page.performSearch(QStringLiteral("晴天"));

    const QList<SearchResultGroup> groups = page.onlineResultGroups();
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups.constFirst().variants.size(), 2);
    QCOMPARE(groups.constFirst().sourceConsensus, 2);
    QCOMPARE(groups.constFirst().preferredSong().sourceId(), SourceId::QqMusic);
    QCOMPARE(groups.constFirst().preferredSong().effectiveRemoteId(),
             QStringLiteral("qq-sunny"));

    QStackedWidget *stack = page.findChild<QStackedWidget *>();
    QVERIFY(stack);
    stack->setCurrentIndex(1);
    const QList<Song> visibleSongs = page.currentSongs();
    QCOMPARE(visibleSongs.size(), 1);
    QCOMPARE(visibleSongs.constFirst().stableIdentity(), QStringLiteral("2:qq-sunny"));
}

QTEST_MAIN(SearchPageTest)
#include "tst_searchpage.moc"
