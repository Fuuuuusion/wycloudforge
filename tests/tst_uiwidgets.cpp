#include "ui/SongListView.h"
#include "ui/SongListModel.h"
#include "ui/PlayerBar.h"
#include "ui/ProgressSlider.h"
#include "ui/LyricWidget.h"
#include "ui/CoverCard.h"
#include "ui/FavoritesPage.h"
#include "ui/RecommendPage.h"
#include "ui/SongListPage.h"
#include "ui/AccountPanel.h"
#include "ui/AccountSettingsButton.h"
#include "ui/SideBar.h"
#include "ui/AiReportPage.h"
#include "ui/SidebarFooter.h"
#include "ui/DownloadPage.h"
#include "ui/SourceIcons.h"
#include "ui/ThemeManager.h"
#include "ui/TitleBar.h"
#include "core/NeteaseApiClient.h"
#include "core/QqMusicSource.h"
#include "core/SearchAggregator.h"
#include "core/SettingsService.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QToolButton>
#include <QtTest>

using namespace core;
using namespace ui;

class OfflineRecommendSource final : public NeteaseApiClient
{
public:
    using NeteaseApiClient::NeteaseApiClient;

    void recommendSongs(JsonArrayFn, ErrFn err = {}) override
    {
        if (err)
            err(QStringLiteral("离线测试来源"));
    }
};

class RecordingQqRecommendSource final : public QqMusicSource
{
public:
    using QqMusicSource::QqMusicSource;

    void recommendSongs(JsonArrayFn ok, ErrFn = {}) override
    {
        ++recommendCalls;
        if (ok)
            ok(songs);
    }

    void topPlaylists(const QString &, int offset, JsonArrayFn ok, ErrFn = {}) override
    {
        playlistOffsets.append(offset);
        if (ok)
            ok(playlists);
    }

    void userPlaylists(const QString &, JsonArrayFn ok, ErrFn = {}) override
    {
        ++userPlaylistCalls;
        if (ok)
            ok(QJsonArray());
    }

    QJsonArray songs;
    QJsonArray playlists;
    QList<int> playlistOffsets;
    int recommendCalls = 0;
    int userPlaylistCalls = 0;
};

class SongListViewTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void batchEntryAndStableSelection();
    void rowUpdatePreservesModelAndBatchSelection();
    void favoriteActionKeepsPlaySignalSeparate();
    void singleDeleteActionKeepsPlaySignalSeparate();
    void batchDeleteButtonKeepsTextAndSignal();
    void downloadStateUsesStableIdentity();
    void playerBarDownloadStateKeepsSignalsSeparate();
    void playerBarDirectionalControlsKeepGeometryAndSignals();
    void playerBarHighDpiIconsRemainComplete();
    void playerBarVisualRefinementUsesResourcesAndTooltips();
    void invalidTemporaryIdsNeverShowPlayingState();
    void playerBarHoverAnimationsStartFromRestingState();
    void playerUtilityButtonsKeepTransparentBackground();
    void playingProgressDragCommitsOnceWithoutPositionOverwrite();
    void lyricPreviewWaitsForIdleDelay();
    void songListContextMenusMatchPageSemantics();
    void cloudPlaylistBadgeIsInformationalOnly();
    void songRowActionIconsStayVerticallyCentered();
    void highlightedSearchTextKeepsRedGlyphsWithoutBackground();
    void rowHoverKeepsBackgroundClear();
    void rowHoverClearsWhenPointerLeavesViewport();
    void rowHoverRapidTransitionsRepaintInterruptedRow();
    void rowHoverClearsOverBatchBarAndPlayingStaysIndependent();
    void songListProvidesScrollableTopAndBottomSafeAreas();
    void sourcePickerRequiresSecondClickAndKeepsGroupIdentity();
    void mergedCollectionKeepsMembersAndBatchActions();
    void collectionPagesDisplayMergedSourcesOnce();
    void sourceSwitchIsVisibleAndExclusive();
    void sourceIconResourcesPreserveSizeAndAlpha();
    void sidebarFooterKeepsConfirmedGeometryAndRefreshIcon();
    void aiReportPageStartsLoadingAndCompletes();
    void manualRecommendRefreshUsesPlatformTopListsAndFeedback();
    void recommendedPlaylistsScrollAtNarrowWidths();
    void accountActionIsExplicitAndPreservesSignal();
    void qqOnlyAccountFallsBackToQqIdentity();
    void vipBadgesReflectAccountAndSelectedSong();
    void downloadPageKeepsFixedRowsAndByteProgress();
    void songListNavigationStateRestoresDetailContext();
    void guestSourcesHideBehindAuthenticatedVariant();
    void playerMetadataRefreshPreservesProgress();
    void layoutComponentsFollowConfirmedGeometry();
    void singleClickPlaysContentOnly();
    void playbackActivityTracksRealState();
    void fullCoverPlaylistCardKeepsVisibleGeometry();
    void newPlaylistCardKeepsClickContract();

private:
    QTemporaryDir m_settingsDir;
};

void SongListViewTest::initTestCase()
{
    QVERIFY(m_settingsDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("NeteaseCloneUiTest"));
    QCoreApplication::setApplicationName(QStringLiteral("NeteaseCloneUiTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    SettingsService::setThemeMode(int(ThemeMode::Dark));
    ThemeManager::instance().initialize(qApp);
}

void SongListViewTest::batchEntryAndStableSelection()
{
    Song online;
    online.id = 11;
    online.source = int(SourceId::Netease);
    online.remoteId = QStringLiteral("online-a");
    online.filePath = QStringLiteral("netease://online-a");
    online.title = QStringLiteral("在线歌曲");

    Song local;
    local.id = 12;
    local.source = int(SourceId::Local);
    local.filePath = QStringLiteral("C:/music/local-b.mp3");
    local.title = QStringLiteral("本地歌曲");

    SongListView view;
    view.resize(1200, 420);
    view.setSongs({ online, local });
    view.show();
    QApplication::processEvents();

    auto *bar = view.findChild<QWidget *>(QStringLiteral("batchBar"));
    auto *toggle = view.findChild<QPushButton *>(QStringLiteral("batchToggle"));
    auto *selectAll = view.findChild<QPushButton *>(QStringLiteral("batchSelectAll"));
    auto *summary = view.findChild<QLabel *>(QStringLiteral("batchSelectionSummary"));
    auto *done = view.findChild<QPushButton *>(QStringLiteral("batchDone"));
    QVERIFY(bar);
    QVERIFY(toggle);
    QVERIFY(selectAll);
    QVERIFY(summary);
    QVERIFY(done);
    QVERIFY(bar->isVisible());
    QVERIFY(toggle->isVisible());
    QVERIFY(!view.batchMode());
    QVERIFY(!selectAll->isVisible());

    toggle->click();
    QApplication::processEvents();
    QVERIFY(view.batchMode());
    QVERIFY(!toggle->isVisible());
    QVERIFY(selectAll->isVisible());
    QCOMPARE(summary->text(), QStringLiteral("已选择 0 首"));

    const QModelIndex first = view.model()->index(0, 0);
    const QRect firstRect = view.visualRect(first);
    QVERIFY(firstRect.isValid());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, firstRect.center());
    QCOMPARE(view.selectedSongs().size(), 1);
    QCOMPARE(view.selectedSongs().first().selectionIdentity(), online.selectionIdentity());
    QCOMPARE(summary->text(), QStringLiteral("已选择 1 首"));

    view.setSongs({ local, online });
    QCOMPARE(view.selectedSongs().size(), 1);
    QCOMPARE(view.selectedSongs().first().selectionIdentity(), online.selectionIdentity());

    view.setSongs({ local });
    QVERIFY(view.selectedSongs().isEmpty());
    QCOMPARE(summary->text(), QStringLiteral("已选择 0 首"));

    view.resize(700, 420);
    QApplication::processEvents();
    auto *more = view.findChild<QToolButton *>(QStringLiteral("batchMore"));
    auto *unfavorite = view.findChild<QPushButton *>(QStringLiteral("batchUnfavorite"));
    QVERIFY(more);
    QVERIFY(unfavorite);
    QVERIFY(more->isVisible());
    QVERIFY(!unfavorite->isVisible());

    done->click();
    QApplication::processEvents();
    QVERIFY(!view.batchMode());
    QVERIFY(bar->isVisible());
    QVERIFY(toggle->isVisible());
    QVERIFY(!more->isVisible());
}

void SongListViewTest::rowUpdatePreservesModelAndBatchSelection()
{
    Song online;
    online.id = 21;
    online.source = int(SourceId::Netease);
    online.remoteId = QStringLiteral("row-update");
    online.filePath = QStringLiteral("netease://row-update");
    online.title = QStringLiteral("更新前");

    Song local;
    local.id = 22;
    local.source = int(SourceId::Local);
    local.filePath = QStringLiteral("C:/music/row-local.mp3");
    local.title = QStringLiteral("本地歌曲");

    SongListView view;
    view.resize(1200, 420);
    view.setSongs({ online, local });
    view.show();
    QApplication::processEvents();
    view.findChild<QPushButton *>(QStringLiteral("batchToggle"))->click();
    QApplication::processEvents();
    const QRect firstRect = view.visualRect(view.model()->index(0, 0));
    QVERIFY(firstRect.isValid());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, firstRect.center());
    QCOMPARE(view.selectedSongs().size(), 1);

    QSignalSpy resetSpy(view.model(), &QAbstractItemModel::modelReset);
    QSignalSpy changedSpy(view.model(), &QAbstractItemModel::dataChanged);
    online.title = QStringLiteral("更新后");
    online.coverPath = QStringLiteral("C:/cache/cover.jpg");
    QVERIFY(view.updateSong(online));

    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(view.model()->rowCount(), 2);
    QCOMPARE(view.model()->data(view.model()->index(0, 0), SongListModel::TitleRole).toString(),
             QStringLiteral("更新后"));
    QVERIFY(view.batchMode());
    QCOMPARE(view.selectedSongs().size(), 1);
    QCOMPARE(view.selectedSongs().constFirst().selectionIdentity(), online.selectionIdentity());
}

void SongListViewTest::favoriteActionKeepsPlaySignalSeparate()
{
    Song song;
    song.id = 501;
    song.title = QStringLiteral("Favorite action");
    song.filePath = QStringLiteral("C:/music/favorite-action.mp3");

    SongListView view;
    view.resize(1000, 240);
    view.setSongs({ song });
    view.show();
    QApplication::processEvents();

    QSignalSpy heartSpy(&view, &SongListView::heartRequested);
    QSignalSpy playSpy(&view, &SongListView::playRequested);
    const QModelIndex favoriteIndex = view.model()->index(0, 6);
    const QRect favoriteRect = view.visualRect(favoriteIndex);
    QVERIFY(favoriteRect.isValid());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      favoriteRect.center());

    QCOMPARE(heartSpy.count(), 1);
    QCOMPARE(heartSpy.takeFirst().at(0).toInt(), 0);
    QCOMPARE(playSpy.count(), 0);
}

void SongListViewTest::singleDeleteActionKeepsPlaySignalSeparate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString downloadPath = dir.filePath(QStringLiteral("downloaded.mp3"));
    QFile download(downloadPath);
    QVERIFY(download.open(QIODevice::WriteOnly));
    QVERIFY(download.write("download") > 0);
    download.close();

    Song song;
    song.id = 601;
    song.source = int(SourceId::Netease);
    song.remoteId = QStringLiteral("delete-action");
    song.filePath = QStringLiteral("netease://delete-action");
    song.downloadPath = downloadPath;
    song.title = QStringLiteral("Delete action");

    SongListView view;
    view.resize(1200, 260);
    view.setSongs({ song });
    view.setDownloadActionMode(SongListView::DeleteDownloadAction);
    view.show();
    QApplication::processEvents();

    QSignalSpy deleteSpy(&view, &SongListView::deleteDownloadRequested);
    QSignalSpy playSpy(&view, &SongListView::playRequested);
    const QRect actionRect = view.visualRect(view.model()->index(0, 7));
    QVERIFY(actionRect.isValid());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, actionRect.center());
    QCOMPARE(deleteSpy.count(), 1);
    QCOMPARE(deleteSpy.takeFirst().at(0).toInt(), 0);
    QCOMPARE(playSpy.count(), 0);
}

void SongListViewTest::batchDeleteButtonKeepsTextAndSignal()
{
    Song song;
    song.id = 701;
    song.filePath = QStringLiteral("C:/music/batch-delete.mp3");
    song.title = QStringLiteral("Batch delete");

    SongListView view;
    view.resize(1200, 260);
    view.setSongs({ song });
    view.show();
    QApplication::processEvents();

    view.findChild<QPushButton *>(QStringLiteral("batchToggle"))->click();
    QApplication::processEvents();
    const QRect selectionRect = view.visualRect(view.model()->index(0, 0));
    QVERIFY(selectionRect.isValid());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, selectionRect.center());

    auto *batchDelete = view.findChild<QPushButton *>(QStringLiteral("batchDelete"));
    QVERIFY(batchDelete);
    QCOMPARE(batchDelete->size(), QSize(108, 30));
    QCOMPARE(batchDelete->text(), QStringLiteral("按来源删除"));
    QCOMPARE(batchDelete->accessibleName(), QStringLiteral("按来源删除"));

    QSignalSpy batchDeleteSpy(&view, &SongListView::batchDeleteRequested);
    batchDelete->click();
    QCOMPARE(batchDeleteSpy.count(), 1);
    const QList<QVariant> arguments = batchDeleteSpy.takeFirst();
    QCOMPARE(qvariant_cast<QList<Song>>(arguments.at(0)).size(), 1);
}

void SongListViewTest::downloadStateUsesStableIdentity()
{
    Song song;
    song.id = 801;
    song.source = int(SourceId::Netease);
    song.remoteId = QStringLiteral("download-state");
    song.filePath = QStringLiteral("netease://download-state");
    song.title = QStringLiteral("Download state");

    SongListView view;
    view.resize(1000, 260);
    view.setSongs({ song });
    view.show();
    QApplication::processEvents();

    QSignalSpy downloadSpy(&view, &SongListView::downloadRequested);
    QSignalSpy deleteSpy(&view, &SongListView::deleteDownloadRequested);
    const QModelIndex actionIndex = view.model()->index(0, 7);
    const QRect actionRect = view.visualRect(actionIndex);
    QVERIFY(actionRect.isValid());

    const QSet<QString> active{ song.selectionIdentity() };
    view.setDownloadingIdentities(active);
    QVERIFY(actionIndex.data(SongListModel::DownloadingRole).toBool());
    QCOMPARE(actionIndex.data(Qt::ToolTipRole).toString(), QStringLiteral("下载中"));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, actionRect.center());
    QCOMPARE(downloadSpy.count(), 0);
    QCOMPARE(deleteSpy.count(), 0);

    view.setDownloadingIdentities({});
    QVERIFY(!actionIndex.data(SongListModel::DownloadingRole).toBool());
    QCOMPARE(actionIndex.data(Qt::ToolTipRole).toString(), QStringLiteral("下载"));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, actionRect.center());
    QCOMPARE(downloadSpy.count(), 1);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString downloadPath = dir.filePath(QStringLiteral("completed.mp3"));
    QFile download(downloadPath);
    QVERIFY(download.open(QIODevice::WriteOnly));
    QVERIFY(download.write("completed") > 0);
    download.close();
    song.downloadPath = downloadPath;
    view.setDownloadingIdentities(active);
    QVERIFY(view.updateSong(song));
    view.setDownloadingIdentities({});
    QCOMPARE(actionIndex.data(Qt::ToolTipRole).toString(), QStringLiteral("删除下载"));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, actionRect.center());
    QCOMPARE(deleteSpy.count(), 1);
    QCOMPARE(downloadSpy.count(), 1);
}

void SongListViewTest::playerBarDownloadStateKeepsSignalsSeparate()
{
    Song song;
    song.id = 901;
    song.source = int(SourceId::Netease);
    song.remoteId = QStringLiteral("playerbar-download");
    song.filePath = QStringLiteral("netease://playerbar-download");
    song.title = QStringLiteral("PlayerBar download");

    PlayerBar bar;
    bar.setSong(song, false);
    auto *button = bar.findChild<QPushButton *>(QStringLiteral("downloadActionButton"));
    QVERIFY(button);
    QVERIFY(button->isEnabled());
    QCOMPARE(button->toolTip(), QStringLiteral("下载"));

    QSignalSpy downloadSpy(&bar, &PlayerBar::downloadRequested);
    QSignalSpy deleteSpy(&bar, &PlayerBar::deleteDownloadRequested);
    button->click();
    QCOMPARE(downloadSpy.count(), 1);
    QCOMPARE(deleteSpy.count(), 0);

    bar.setDownloadActive(true);
    QVERIFY(!button->isEnabled());
    QCOMPARE(button->toolTip(), QStringLiteral("下载中"));
    button->click();
    QCOMPARE(downloadSpy.count(), 1);
    QCOMPARE(deleteSpy.count(), 0);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString downloadPath = dir.filePath(QStringLiteral("playerbar-complete.mp3"));
    QFile download(downloadPath);
    QVERIFY(download.open(QIODevice::WriteOnly));
    QVERIFY(download.write("complete") > 0);
    download.close();
    song.downloadPath = downloadPath;
    bar.setSong(song, false);
    bar.setDownloadActive(false);
    QVERIFY(button->isEnabled());
    QCOMPARE(button->toolTip(), QStringLiteral("删除下载"));
    button->click();
    QCOMPARE(downloadSpy.count(), 1);
    QCOMPARE(deleteSpy.count(), 1);
}

void SongListViewTest::playerBarDirectionalControlsKeepGeometryAndSignals()
{
    PlayerBar bar;
    auto *previous = bar.findChild<QPushButton *>(QStringLiteral("previousTrackButton"));
    auto *next = bar.findChild<QPushButton *>(QStringLiteral("nextTrackButton"));
    auto *favorite = bar.findChild<QPushButton *>(QStringLiteral("favoriteActionButton"));
    QVERIFY(previous);
    QVERIFY(next);
    QVERIFY(favorite);
    QCOMPARE(previous->size(), QSize(30, 30));
    QCOMPARE(next->size(), QSize(30, 30));
    QCOMPARE(previous->toolTip(), QStringLiteral("上一首"));
    QCOMPARE(next->toolTip(), QStringLiteral("下一首"));

    bar.setFixedWidth(680);
    bar.show();
    QApplication::processEvents();
    const auto visibleRectInBar = [&bar](QWidget *widget) {
        return QRect(widget->mapTo(&bar, QPoint(0, 0)), widget->size());
    };
    QVERIFY(previous->isVisibleTo(&bar));
    QVERIFY(next->isVisibleTo(&bar));
    QVERIFY(favorite->isVisibleTo(&bar));
    QVERIFY(bar.rect().contains(visibleRectInBar(previous)));
    QVERIFY(bar.rect().contains(visibleRectInBar(next)));
    QVERIFY(bar.rect().contains(visibleRectInBar(favorite)));

    auto *leftBox = bar.findChild<QWidget *>(QStringLiteral("playerLeftBox"));
    auto *actionBox = bar.findChild<QWidget *>(QStringLiteral("playerActionBox"));
    auto *centerBox = bar.findChild<QWidget *>(QStringLiteral("playerCenterBox"));
    QVERIFY(leftBox);
    QVERIFY(actionBox);
    QVERIFY(centerBox);
    QVERIFY(actionBox->rect().contains(
        QRect(favorite->mapTo(actionBox, QPoint(0, 0)), favorite->size())));
    QVERIFY(centerBox->rect().contains(QRect(previous->mapTo(centerBox, QPoint(0, 0)), previous->size())));
    QVERIFY(centerBox->rect().contains(QRect(next->mapTo(centerBox, QPoint(0, 0)), next->size())));
    QCOMPARE(previous->size(), QSize(30, 30));
    QCOMPARE(next->size(), QSize(30, 30));

    QSignalSpy previousSpy(&bar, &PlayerBar::prevClicked);
    QSignalSpy nextSpy(&bar, &PlayerBar::nextClicked);
    previous->click();
    next->click();
    QCOMPARE(previousSpy.count(), 1);
    QCOMPARE(nextSpy.count(), 1);
}

void SongListViewTest::playerBarHighDpiIconsRemainComplete()
{
    PlayerBar bar;
    bar.setFixedWidth(680);
    bar.show();
    bar.raise();
    bar.activateWindow();
    QApplication::processEvents();

    auto *previous = bar.findChild<QPushButton *>(QStringLiteral("previousTrackButton"));
    auto *next = bar.findChild<QPushButton *>(QStringLiteral("nextTrackButton"));
    auto *favorite = bar.findChild<QPushButton *>(QStringLiteral("favoriteActionButton"));
    QVERIFY(previous);
    QVERIFY(next);
    QVERIFY(favorite);
    QVERIFY2(previous->devicePixelRatioF() > 1.0,
             "This regression must run above 100% scale to expose source-pixmap cropping");

    QTest::mouseMove(&bar, QPoint(1, 1), 1);
    QTest::qWait(220);
    previous->clearFocus();
    next->clearFocus();
    favorite->clearFocus();

    const QImage previousImage = previous->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const QImage nextImage = next->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const QImage favoriteImage = favorite->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(previousImage.size(), nextImage.size());

    const auto mirroredDifferences = [](const QImage &left, const QImage &right) {
        int differences = 0;
        for (int y = 0; y < left.height(); ++y) {
            for (int x = 0; x < left.width(); ++x) {
                const QColor a = left.pixelColor(x, y);
                const QColor b = right.pixelColor(right.width() - 1 - x, y);
                if (qAbs(a.red() - b.red()) > 3 || qAbs(a.green() - b.green()) > 3
                    || qAbs(a.blue() - b.blue()) > 3 || qAbs(a.alpha() - b.alpha()) > 3) {
                    ++differences;
                }
            }
        }
        return differences;
    };

    const int directionalDifferences = mirroredDifferences(previousImage, nextImage);
    const int favoriteDifferences = mirroredDifferences(favoriteImage, favoriteImage);
    QVERIFY2(directionalDifferences <= 120,
             qPrintable(QStringLiteral("directional mirrored pixel differences: %1")
                            .arg(directionalDifferences)));
    QVERIFY2(favoriteDifferences <= 180,
             qPrintable(QStringLiteral("favorite mirrored pixel differences: %1")
                            .arg(favoriteDifferences)));
}

void SongListViewTest::invalidTemporaryIdsNeverShowPlayingState()
{
    Song temporary;
    temporary.id = -1;
    temporary.source = int(SourceId::Netease);
    temporary.remoteId = QStringLiteral("temporary-search-result");
    temporary.filePath = QStringLiteral("netease://temporary-search-result");
    temporary.title = QStringLiteral("尚未播放");

    SongListModel model;
    model.setSongs({ temporary }, -1);
    QVERIFY(!model.index(0, 0).data(SongListModel::IsPlayingRole).toBool());

    Song playing = temporary;
    playing.id = 42;
    model.setPlayingSong(playing);
    QVERIFY(model.index(0, 0).data(SongListModel::IsPlayingRole).toBool());

    temporary.id = 42;
    model.setSongs({ temporary }, 42);
    QVERIFY(model.index(0, 0).data(SongListModel::IsPlayingRole).toBool());
}

void SongListViewTest::playerBarHoverAnimationsStartFromRestingState()
{
    Song song;
    song.id = 902;
    song.source = int(SourceId::Netease);
    song.remoteId = QStringLiteral("hover-actions");
    song.filePath = QStringLiteral("netease://hover-actions");
    song.title = QStringLiteral("Hover");

    PlayerBar bar;
    bar.resize(1200, 204);
    bar.setSong(song, false);
    bar.show();
    QApplication::processEvents();
    auto *favorite = bar.findChild<QPushButton *>(QStringLiteral("favoriteActionButton"));
    auto *download = bar.findChild<QPushButton *>(QStringLiteral("downloadActionButton"));
    QVERIFY(favorite);
    QVERIFY(download);

    QEnterEvent favoriteEnter(favorite->rect().center(), favorite->rect().center(),
                              favorite->mapToGlobal(favorite->rect().center()));
    QApplication::sendEvent(favorite, &favoriteEnter);
    QVERIFY(favorite->property("hoverProgress").toReal() < 0.95);
    QTRY_VERIFY_WITH_TIMEOUT(favorite->property("hoverProgress").toReal() > 0.99, 500);

    QEvent favoriteLeave(QEvent::Leave);
    QApplication::sendEvent(favorite, &favoriteLeave);
    QTRY_VERIFY_WITH_TIMEOUT(favorite->property("hoverProgress").toReal() < 0.01, 500);
    QEnterEvent downloadEnter(download->rect().center(), download->rect().center(),
                              download->mapToGlobal(download->rect().center()));
    QApplication::sendEvent(download, &downloadEnter);
    QVERIFY(download->property("hoverProgress").toReal() < 0.95);
    QTRY_VERIFY_WITH_TIMEOUT(download->property("hoverProgress").toReal() > 0.99, 500);
}

void SongListViewTest::playerUtilityButtonsKeepTransparentBackground()
{
    PlayerBar bar;
    QFile theme(QStringLiteral(":/theme.qss"));
    QVERIFY(theme.open(QIODevice::ReadOnly));
    bar.setStyleSheet(ThemeManager::instance().renderStyleSheet(QString::fromUtf8(theme.readAll())));
    bar.resize(1200, 204);
    bar.show();
    QApplication::processEvents();

    const QStringList names = {
        QStringLiteral("playerModeButton"), QStringLiteral("playerLyricsButton"),
        QStringLiteral("playerQueueButton"), QStringLiteral("playerMuteButton"),
    };
    for (const QString &name : names) {
        auto *button = bar.findChild<QPushButton *>(name);
        QVERIFY2(button, qPrintable(name));
        const QImage image = button->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        QCOMPARE(image.pixelColor(image.width() / 2, 4).alpha(), 0);
    }
}

void SongListViewTest::playingProgressDragCommitsOnceWithoutPositionOverwrite()
{
    Song song;
    song.id = 903;
    song.filePath = QStringLiteral("C:/music/progress.mp3");
    song.title = QStringLiteral("Progress");
    song.durationMs = 100000;

    PlayerBar bar;
    bar.resize(1200, 204);
    bar.setSong(song, false);
    bar.setPosition(30000);
    bar.show();
    QApplication::processEvents();
    auto *progress = bar.findChild<ProgressSlider *>(QStringLiteral("playerProgress"));
    QVERIFY(progress);
    QSignalSpy seekSpy(&bar, &PlayerBar::seekRequested);

    const QPoint target(qRound(progress->width() * 0.78), progress->height() / 2);
    QTest::mousePress(progress, Qt::LeftButton, Qt::NoModifier,
                      QPoint(6, progress->height() / 2));
    QTest::mouseMove(progress, target);
    QVERIFY(progress->isDragging());
    const int draggedValue = progress->value();
    QVERIFY(draggedValue > 700);
    bar.setPosition(31000); // 模拟播放状态持续回写位置
    QCOMPARE(progress->value(), draggedValue);

    QTest::mouseRelease(progress, Qt::LeftButton, Qt::NoModifier, target);
    QCOMPARE(seekSpy.count(), 1);
    QVERIFY(!progress->isDragging());
    const qint64 requested = seekSpy.takeFirst().at(0).toLongLong();
    QVERIFY(requested > 70000 && requested < 85000);
}

void SongListViewTest::guestSourcesHideBehindAuthenticatedVariant()
{
    Song netease = MusicSource::makeOnlineSong(
        SourceId::Netease, QStringLiteral("netease"), QStringLiteral("n-guest"),
        QStringLiteral("同曲"), QStringLiteral("歌手"), QStringLiteral("专辑"),
        200000, {});
    Song qq = MusicSource::makeOnlineSong(
        SourceId::QqMusic, QStringLiteral("qqmusic"), QStringLiteral("q-auth"),
        QStringLiteral("同曲"), QStringLiteral("歌手"), QStringLiteral("专辑"),
        201000, {});
    const QList<SearchResultGroup> groups =
        SearchAggregator::aggregateSongsPreservingOrder({ netease, qq });
    QCOMPARE(groups.size(), 1);

    SongListModel model;
    model.setSourceAccessStates({
        { int(SourceId::Netease), SourceAccessState::Guest },
        { int(SourceId::QqMusic), SourceAccessState::Authenticated },
    });
    model.setSearchResultGroups(groups);
    const QList<SongSourceChoice> choices = model.sourceChoicesAt(0);
    QCOMPARE(choices.size(), 2);
    for (const SongSourceChoice &choice : choices) {
        if (choice.source == SourceId::Netease) {
            QVERIFY(choice.guest);
            QVERIFY(!choice.visible);
        } else if (choice.source == SourceId::QqMusic) {
            QVERIFY(!choice.guest);
            QVERIFY(choice.visible);
        }
    }
    QCOMPARE(model.activeSourceAt(0), SourceId::QqMusic);

    model.setSourceAccessStates({
        { int(SourceId::Netease), SourceAccessState::Guest },
        { int(SourceId::QqMusic), SourceAccessState::Guest },
    });
    for (const SongSourceChoice &choice : model.sourceChoicesAt(0))
        QVERIFY(choice.visible);

    model.setSourceAccessStates({
        { int(SourceId::Netease), SourceAccessState::Unavailable },
        { int(SourceId::QqMusic), SourceAccessState::Guest },
    });
    for (const SongSourceChoice &choice : model.sourceChoicesAt(0)) {
        if (choice.source == SourceId::Netease) {
            QVERIFY(!choice.available);
            QVERIFY(choice.unavailableReason.contains(QStringLiteral("不可用")));
        } else if (choice.source == SourceId::QqMusic) {
            QVERIFY(choice.available);
        }
    }
}

void SongListViewTest::playerMetadataRefreshPreservesProgress()
{
    Song song;
    song.id = 906;
    song.filePath = QStringLiteral("C:/music/same-song.mp3");
    song.title = QStringLiteral("原标题");
    song.durationMs = 100000;

    PlayerBar bar;
    bar.setSong(song, false);
    bar.setPosition(42000);
    auto *progress = bar.findChild<ProgressSlider *>(QStringLiteral("playerProgress"));
    QVERIFY(progress);
    const int before = progress->value();
    song.title = QStringLiteral("更新标题");
    bar.setSong(song, true);
    QCOMPARE(progress->value(), before);
}

void SongListViewTest::lyricPreviewWaitsForIdleDelay()
{
    QList<LyricLine> lines;
    for (int i = 0; i < 20; ++i)
        lines.append({ qint64(i) * 1000, QStringLiteral("第 %1 行").arg(i) });
    LyricWidget lyric;
    lyric.resize(500, 260);
    lyric.setPreviewReturnDelay(80);
    lyric.setLyrics(lines);
    lyric.setPosition(2000);
    lyric.show();
    QApplication::processEvents();

    QWheelEvent wheel(QPointF(250, 130), QPointF(250, 130), QPoint(), QPoint(0, -120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&lyric, &wheel);
    QVERIFY(lyric.isPreviewing());
    const qreal previewOffset = lyric.previewOffset();
    QVERIFY(previewOffset > 0);
    lyric.setPosition(4000);
    QVERIFY(lyric.isPreviewing());
    QCOMPARE(lyric.previewOffset(), previewOffset);

    QTest::qWait(45);
    QWheelEvent again(QPointF(250, 130), QPointF(250, 130), QPoint(), QPoint(0, -120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&lyric, &again);
    QTest::qWait(45);
    QVERIFY(lyric.isPreviewing()); // 第二次滚动重新计算 5 秒空闲期
    QTRY_VERIFY_WITH_TIMEOUT(!lyric.isPreviewing(), 300);
    QCOMPARE(lyric.previewOffset(), 0.0);
}

void SongListViewTest::songListContextMenusMatchPageSemantics()
{
    SongListPage page;
    auto *more = page.findChild<QToolButton *>(QStringLiteral("songListMoreButton"));
    QVERIFY(more);
    page.setReadOnlyContext();
    QVERIFY(more->isHidden());

    page.setPlaybackQueueContext();
    QVERIFY(!more->isHidden());
    QVERIFY(more->menu());
    QCOMPARE(more->menu()->actions().size(), 2);
    QCOMPARE(more->menu()->actions().at(0)->text(), QStringLiteral("保存为新建歌单"));
    QCOMPARE(more->menu()->actions().at(1)->text(), QStringLiteral("清空播放列表"));

    page.setPlaylistContext(2);
    QVERIFY(more->menu());
    QVERIFY(more->menu()->actions().size() >= 3);
}

void SongListViewTest::cloudPlaylistBadgeIsInformationalOnly()
{
    SideBar sidebar;
    SideBar::PlaylistItem local;
    local.id = 2;
    local.name = QStringLiteral("本地歌单");
    SideBar::PlaylistItem cloud;
    cloud.cloud = true;
    cloud.source = SourceId::QqMusic;
    cloud.remoteId = QStringLiteral("cloud-1");
    cloud.name = QStringLiteral("QQ 云歌单");
    sidebar.setPlaylists({ local, cloud });
    sidebar.show();
    QApplication::processEvents();

    const QList<QLabel *> badges = sidebar.findChildren<QLabel *>(
        QStringLiteral("cloudPlaylistSourceBadge"));
    QCOMPARE(badges.size(), 1);
    QVERIFY(!badges.first()->pixmap().isNull());
    QVERIFY(badges.first()->testAttribute(Qt::WA_TransparentForMouseEvents));

    QSignalSpy cloudSpy(&sidebar, &SideBar::cloudPlaylistSelected);
    QPushButton *cloudButton = nullptr;
    for (QPushButton *button : sidebar.findChildren<QPushButton *>()) {
        if (button->text() == cloud.name) {
            cloudButton = button;
            break;
        }
    }
    QVERIFY(cloudButton);
    cloudButton->click();
    QCOMPARE(cloudSpy.count(), 1);
    QCOMPARE(cloudSpy.first().at(0).toInt(), int(SourceId::QqMusic));
    QCOMPARE(cloudSpy.first().at(1).toString(), cloud.remoteId);
}

void SongListViewTest::playerBarVisualRefinementUsesResourcesAndTooltips()
{
    const QStringList resources = {
        QStringLiteral(":/icons/player-mode-loop.png"),
        QStringLiteral(":/icons/player-mode-single.png"),
        QStringLiteral(":/icons/player-mode-shuffle.png"),
        QStringLiteral(":/icons/player-lyrics.png"),
        QStringLiteral(":/icons/player-queue.png"),
    };
    for (const QString &path : resources) {
        const QImage image(path);
        QVERIFY2(!image.isNull(), qPrintable(path));
        QCOMPARE(image.size(), QSize(200, 200));
        QVERIFY(!image.createAlphaMask().isNull());
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString coverPath = dir.filePath(QStringLiteral("cover.png"));
    QImage sourceCover(80, 80, QImage::Format_ARGB32);
    sourceCover.fill(QColor(QStringLiteral("#456789")));
    QVERIFY(sourceCover.save(coverPath));

    Song song;
    song.id = 907;
    song.source = int(SourceId::Netease);
    song.remoteId = QStringLiteral("player-visual");
    song.filePath = QStringLiteral("netease://player-visual");
    song.title = QStringLiteral("一首足够长以验证右侧省略规则的歌曲名称");
    song.artist = QStringLiteral("一位足够长以验证右侧省略规则的歌手名称");
    song.coverPath = coverPath;

    PlayerBar bar;
    QFile theme(QStringLiteral(":/theme.qss"));
    QVERIFY(theme.open(QIODevice::ReadOnly));
    bar.setStyleSheet(ThemeManager::instance().renderStyleSheet(QString::fromUtf8(theme.readAll())));
    bar.resize(1200, 204);
    bar.setSong(song, false);
    bar.show();
    QApplication::processEvents();

    QCOMPARE(bar.height(), 204);
    auto *title = bar.findChild<QLabel *>(QStringLiteral("playerTitle"));
    auto *artist = bar.findChild<QLabel *>(QStringLiteral("playerArtist"));
    auto *cover = bar.findChild<QLabel *>(QStringLiteral("playerCover"));
    auto *actionBox = bar.findChild<QWidget *>(QStringLiteral("playerActionBox"));
    auto *favorite = bar.findChild<QPushButton *>(QStringLiteral("favoriteActionButton"));
    auto *download = bar.findChild<QPushButton *>(QStringLiteral("downloadActionButton"));
    auto *mode = bar.findChild<QPushButton *>(QStringLiteral("playerModeButton"));
    QVERIFY(title);
    QVERIFY(artist);
    QVERIFY(cover);
    QVERIFY(actionBox);
    QVERIFY(favorite);
    QVERIFY(download);
    QVERIFY(mode);
    QVERIFY(title->isVisible());
    QVERIFY(artist->isVisible());
    QCOMPARE(title->toolTip(), song.title);
    QCOMPARE(artist->toolTip(), song.artist);
    QCOMPARE(cover->size(), QSize(128, 128));

    const QPixmap roundedCover = cover->pixmap();
    QVERIFY(!roundedCover.isNull());
    QCOMPARE(roundedCover.size(), QSize(128, 128));
    QCOMPARE(roundedCover.toImage().pixelColor(0, 0).alpha(), 0);
    QVERIFY(roundedCover.toImage().pixelColor(64, 64).alpha() > 0);

    song.coverPath.clear();
    bar.setSong(song, false);
    const QPixmap placeholderCover = cover->pixmap();
    QVERIFY(!placeholderCover.isNull());
    QCOMPARE(placeholderCover.toImage().pixelColor(0, 0).alpha(), 0);
    QVERIFY(placeholderCover.toImage().pixelColor(64, 64).alpha() > 0);

    const QPoint favoriteCenter = favorite->mapTo(actionBox, favorite->rect().center());
    const QPoint downloadCenter = download->mapTo(actionBox, download->rect().center());
    QVERIFY(favoriteCenter.x() < downloadCenter.x());
    QVERIFY(qAbs(favoriteCenter.y() - downloadCenter.y()) <= 1);

    QSet<quint64> modeIconKeys;
    for (int playbackMode = 0; playbackMode < 3; ++playbackMode) {
        bar.setMode(playbackMode);
        modeIconKeys.insert(mode->icon().cacheKey());
        QVERIFY(!mode->icon().pixmap(QSize(20, 20)).isNull());
    }
    QCOMPARE(modeIconKeys.size(), 3);

    const QStringList forbiddenStateLabels = {
        QStringLiteral("本地"), QStringLiteral("☁ 在线"),
        QStringLiteral("☁ 已缓存"), QStringLiteral("☁ 已下载"),
    };
    for (QLabel *label : bar.findChildren<QLabel *>())
        QVERIFY2(!forbiddenStateLabels.contains(label->text()), qPrintable(label->text()));

    const QString error = QStringLiteral("测试播放失败原因");
    bar.setPlaybackError(error);
    QCOMPARE(bar.toolTip(), error);
    QCOMPARE(title->toolTip(), error);
    QCOMPARE(cover->toolTip(), error);
}

void SongListViewTest::songRowActionIconsStayVerticallyCentered()
{
    Song song;
    song.id = 908;
    song.source = int(SourceId::Netease);
    song.remoteId = QStringLiteral("centered-actions");
    song.filePath = QStringLiteral("netease://centered-actions");
    song.title = QStringLiteral("Centered actions");

    SongListView view;
    view.resize(1000, 260);
    setThemedStyleSheet(&view, QStringLiteral("QTableView{background:@pageBackground;}"));
    view.setSongs({ song });
    view.show();
    QApplication::processEvents();

    const auto iconBounds = [&view](const QRect &logicalRect, const QColor &target) {
        const QPixmap shot = view.viewport()->grab();
        const qreal dpr = shot.devicePixelRatio();
        const QImage image = shot.toImage().convertToFormat(QImage::Format_ARGB32);
        const QRect area(qRound(logicalRect.left() * dpr), qRound(logicalRect.top() * dpr),
                         qRound(logicalRect.width() * dpr), qRound(logicalRect.height() * dpr));
        QRect bounds;
        for (int y = qMax(0, area.top()); y <= qMin(image.height() - 1, area.bottom()); ++y) {
            for (int x = qMax(0, area.left()); x <= qMin(image.width() - 1, area.right()); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.alpha() < 32 || qAbs(pixel.red() - target.red()) > 12
                    || qAbs(pixel.green() - target.green()) > 12
                    || qAbs(pixel.blue() - target.blue()) > 12) {
                    continue;
                }
                bounds = bounds.isNull() ? QRect(x, y, 1, 1) : bounds.united(QRect(x, y, 1, 1));
            }
        }
        return qMakePair(bounds, dpr);
    };

    const QRect favoriteRect = view.visualRect(view.model()->index(0, 6));
    const QRect downloadRect = view.visualRect(view.model()->index(0, 7));
    QVERIFY(favoriteRect.isValid());
    QVERIFY(downloadRect.isValid());

    const auto favoriteResult = iconBounds(favoriteRect, QColor(QStringLiteral("#6E6E7A")));
    const auto downloadResult = iconBounds(downloadRect, QColor(QStringLiteral("#9A9AA5")));
    QVERIFY(!favoriteResult.first.isNull());
    QVERIFY(!downloadResult.first.isNull());
    const qreal favoriteCenterY = (favoriteResult.first.top() + favoriteResult.first.bottom())
        / (2.0 * favoriteResult.second);
    const qreal downloadCenterY = (downloadResult.first.top() + downloadResult.first.bottom())
        / (2.0 * downloadResult.second);
    // The first model row includes a 42px non-content safe area above the
    // 112px song block. Compare against the delegate's actual content center,
    // not the full visualRect center.
    const qreal favoriteContentCenterY = favoriteRect.top() + 42.0 + 55.5;
    const qreal downloadContentCenterY = downloadRect.top() + 42.0 + 55.5;
    QVERIFY2(qAbs(favoriteCenterY - favoriteContentCenterY) <= 1.0,
             qPrintable(QStringLiteral("favorite center=%1 expected=%2 bounds=%3,%4 %5x%6 dpr=%7")
                            .arg(favoriteCenterY).arg(favoriteContentCenterY)
                            .arg(favoriteResult.first.x()).arg(favoriteResult.first.y())
                            .arg(favoriteResult.first.width()).arg(favoriteResult.first.height())
                            .arg(favoriteResult.second)));
    QVERIFY2(qAbs(downloadCenterY - downloadContentCenterY) <= 1.0,
             qPrintable(QStringLiteral("download center=%1 expected=%2 bounds=%3,%4 %5x%6 dpr=%7")
                            .arg(downloadCenterY).arg(downloadContentCenterY)
                            .arg(downloadResult.first.x()).arg(downloadResult.first.y())
                            .arg(downloadResult.first.width()).arg(downloadResult.first.height())
                            .arg(downloadResult.second)));
    // The heart SVG intentionally keeps breathing room inside its 24px
    // viewport; its opaque contour is about 18px, still 1.33x the former
    // 18px viewport rendering.
    QVERIFY(favoriteResult.first.width() / favoriteResult.second >= 17.0);
    QVERIFY(downloadResult.first.width() / downloadResult.second >= 16.0);
}

void SongListViewTest::highlightedSearchTextKeepsRedGlyphsWithoutBackground()
{
    Song song;
    song.id = 909;
    song.filePath = QStringLiteral("C:/music/highlight.mp3");
    song.title = QStringLiteral("Target song");
    song.artist = QStringLiteral("Artist");

    SongListView view;
    view.resize(1000, 260);
    setThemedStyleSheet(&view, QStringLiteral("QTableView{background:@pageBackground;}"));
    view.setSongs({ song });
    view.setHighlightQuery(QStringLiteral("Target"));
    view.show();
    QApplication::processEvents();

    const QRect titleRect = view.visualRect(view.model()->index(0, 2)).adjusted(12, 0, -8, 0);
    const QPixmap shot = view.viewport()->grab();
    const qreal dpr = shot.devicePixelRatio();
    const QImage image = shot.toImage().convertToFormat(QImage::Format_ARGB32);
    const QRect area(qRound(titleRect.left() * dpr), qRound(titleRect.top() * dpr),
                     qRound(titleRect.width() * dpr), qRound(titleRect.height() * dpr));
    int redPixels = 0;
    int longestRun = 0;
    for (int y = qMax(0, area.top()); y <= qMin(image.height() - 1, area.bottom()); ++y) {
        int currentRun = 0;
        for (int x = qMax(0, area.left()); x <= qMin(image.width() - 1, area.right()); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            const bool red = pixel.red() > 120 && pixel.red() > pixel.green() * 1.8
                && pixel.red() > pixel.blue() * 1.5;
            if (red) {
                ++redPixels;
                longestRun = qMax(longestRun, ++currentRun);
            } else {
                currentRun = 0;
            }
        }
    }
    QVERIFY(redPixels > 10);
    QVERIFY2(longestRun <= qCeil(12.0 * dpr),
             qPrintable(QStringLiteral("highlight background run: %1").arg(longestRun)));
}

void SongListViewTest::rowHoverKeepsBackgroundClear()
{
    Song song;
    song.id = 901;
    song.filePath = QStringLiteral("C:/music/hover-background.mp3");
    song.title = QStringLiteral("Hover background");
    song.artist = QStringLiteral("Artist");

    SongListView view;
    view.resize(1000, 240);
    setThemedStyleSheet(&view, QStringLiteral("QTableView{background:@pageBackground;}"));
    view.setSongs({ song });
    view.show();
    QApplication::processEvents();

    const QRect artistRect = view.visualRect(view.model()->index(0, 2));
    QVERIFY(artistRect.isValid());
    const QPoint samplePoint(artistRect.left() + 5, artistRect.top() + 5);
    const QColor idleColor = view.viewport()->grab().toImage().pixelColor(samplePoint);

    QTest::mouseMove(view.viewport(), artistRect.center());
    QTest::qWait(240);
    const QColor hoverColor = view.viewport()->grab().toImage().pixelColor(samplePoint);
    QCOMPARE(hoverColor, idleColor);
}

void SongListViewTest::rowHoverClearsWhenPointerLeavesViewport()
{
    Song song;
    song.id = 902;
    song.filePath = QStringLiteral("C:/music/hover-leave.mp3");
    song.title = QStringLiteral("Hover leave");
    song.artist = QStringLiteral("Artist");

    SongListView view;
    view.resize(1000, 240);
    view.setSongs({ song });
    view.show();
    QApplication::processEvents();

    const QRect titleRect = view.visualRect(view.model()->index(0, 1));
    const QPoint localPos = titleRect.center();
    QMouseEvent move(QEvent::MouseMove, localPos,
                     view.viewport()->mapToGlobal(localPos),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &move);
    QApplication::processEvents();
    QCOMPARE(view.hoveredRow(), 0);

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(view.viewport(), &leave);
    QApplication::processEvents();
    QCOMPARE(view.hoveredRow(), -1);
}

void SongListViewTest::rowHoverRapidTransitionsRepaintInterruptedRow()
{
    Song first;
    first.id = 903;
    first.filePath = QStringLiteral("C:/music/hover-rapid-first.mp3");
    first.title = QStringLiteral("Rapid hover first");
    first.artist = QStringLiteral("First artist");
    first.album = QStringLiteral("First album");

    Song second;
    second.id = 904;
    second.filePath = QStringLiteral("C:/music/hover-rapid-second.mp3");
    second.title = QStringLiteral("Rapid hover second");
    second.artist = QStringLiteral("Second artist");
    second.album = QStringLiteral("Second album");

    SongListView view;
    view.resize(1000, 320);
    view.setSongs({ first, second });
    view.show();
    view.raise();
    view.activateWindow();
    QApplication::processEvents();

    const auto rowContentRect = [&view](int row) {
        QRect result = view.visualRect(view.model()->index(row, 0)).united(
            view.visualRect(view.model()->index(row, 7)));
        if (row == 0)
            result.setTop(result.top() + 42);
        result.setHeight(112);
        return result.intersected(view.viewport()->rect());
    };
    const auto rowPoint = [&view](int row) {
        const QRect titleRect = view.visualRect(view.model()->index(row, 2));
        return row == 0
            ? QPoint(titleRect.center().x(), titleRect.top() + 42 + 56)
            : titleRect.center();
    };
    const auto captureViewport = [&view] {
        QScreen *screen = view.screen();
        const QPoint viewportOffset = view.viewport()->mapTo(&view, QPoint(0, 0));
        return screen->grabWindow(view.winId(), viewportOffset.x(), viewportOffset.y(),
                                  view.viewport()->width(), view.viewport()->height());
    };

    auto *batchBar = view.findChild<QWidget *>(QStringLiteral("batchBar"));
    QVERIFY(batchBar);
    QTest::mouseMove(batchBar, batchBar->rect().center(), 1);
    QTest::qWait(250);
    const QPixmap idlePixmap = captureViewport();

    QTest::mouseMove(view.viewport(), rowPoint(0), 1);
    QTest::qWait(50);
    QTest::mouseMove(view.viewport(), rowPoint(1), 1);
    QTest::qWait(50);
    QTest::mouseMove(batchBar, batchBar->rect().center(), 1);
    QTest::qWait(250);

    QCOMPARE(view.hoveredRow(), -1);
    const QPixmap finalPixmap = captureViewport();
    QCOMPARE(finalPixmap.size(), idlePixmap.size());
    const QImage idle = idlePixmap.toImage();
    const QImage finalImage = finalPixmap.toImage();
    const qreal dpr = idlePixmap.devicePixelRatio();
    const QRect logicalRow = rowContentRect(0);
    const QRect physicalRow(qRound(logicalRow.x() * dpr), qRound(logicalRow.y() * dpr),
                            qRound(logicalRow.width() * dpr),
                            qRound(logicalRow.height() * dpr));
    int differingPixels = 0;
    for (int y = physicalRow.top(); y <= physicalRow.bottom(); ++y) {
        for (int x = physicalRow.left(); x <= physicalRow.right(); ++x) {
            if (idle.pixel(x, y) != finalImage.pixel(x, y))
                ++differingPixels;
        }
    }
    QCOMPARE(differingPixels, 0);
}

void SongListViewTest::rowHoverClearsOverBatchBarAndPlayingStaysIndependent()
{
    Song song;
    song.id = 902;
    song.filePath = QStringLiteral("C:/music/playing-hover-independent.mp3");
    song.title = QStringLiteral("Playing hover independent");

    SongListView view;
    view.resize(1000, 240);
    view.setSongs({ song }, song.id);
    view.show();
    QApplication::processEvents();

    const QRect titleRect = view.visualRect(view.model()->index(0, 1));
    const QPoint localPos = titleRect.center();
    QMouseEvent move(QEvent::MouseMove, localPos, view.viewport()->mapToGlobal(localPos),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &move);
    QApplication::processEvents();
    QCOMPARE(view.hoveredRow(), 0);

    auto *batchBar = view.findChild<QWidget *>(QStringLiteral("batchBar"));
    QVERIFY(batchBar);
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(batchBar, &enter);
    QApplication::processEvents();
    QCOMPARE(view.hoveredRow(), -1);
    QVERIFY(view.model()->index(0, 0).data(SongListModel::IsPlayingRole).toBool());
}

void SongListViewTest::songListProvidesScrollableTopAndBottomSafeAreas()
{
    QList<Song> songs;
    for (int i = 0; i < 3; ++i) {
        Song song;
        song.id = 910 + i;
        song.filePath = QStringLiteral("C:/music/safe-%1.mp3").arg(i);
        song.title = QStringLiteral("Safe %1").arg(i);
        songs.append(song);
    }

    SongListView view;
    view.resize(1000, 420);
    view.setSongs(songs);
    view.show();
    QApplication::processEvents();

    const QRect first = view.visualRect(view.model()->index(0, 0));
    const QRect middle = view.visualRect(view.model()->index(1, 0));
    const QRect last = view.visualRect(view.model()->index(2, 0));
    QCOMPARE(middle.height(), 112);
    QCOMPARE(first.height() - middle.height(), 42);
    QCOMPARE(last.height() - middle.height(), 16);
    QCOMPARE(first.width(), middle.width());
    QCOMPARE(last.width(), middle.width());
}

void SongListViewTest::sourcePickerRequiresSecondClickAndKeepsGroupIdentity()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString localPath = dir.filePath(QStringLiteral("same-song.mp3"));
    QFile localFile(localPath);
    QVERIFY(localFile.open(QIODevice::WriteOnly));
    QVERIFY(localFile.write("audio") > 0);
    localFile.close();

    auto makeItem = [](SourceId source, const QString &remoteId, const QString &filePath,
                       bool playable) {
        SearchResultItem item;
        item.type = SearchItemType::Song;
        item.source = source;
        item.remoteId = remoteId;
        item.title = QStringLiteral("同一首歌");
        item.artist = QStringLiteral("同一歌手");
        item.durationMs = 180000;
        item.playable = playable;
        item.availabilityError = playable ? QString() : QStringLiteral("版权限制");
        item.song.source = int(source);
        item.song.remoteId = remoteId;
        item.song.filePath = filePath;
        item.song.title = item.title;
        item.song.artist = item.artist;
        item.song.durationMs = item.durationMs;
        return item;
    };

    SearchResultGroup group;
    group.identity = QStringLiteral("recording:test-group");
    SearchResultVariant localVariant;
    localVariant.item = makeItem(SourceId::Local, QString(), localPath, true);
    SearchResultVariant neteaseVariant;
    neteaseVariant.item = makeItem(SourceId::Netease, QStringLiteral("netease-id"),
                                    QStringLiteral("netease://netease-id"), true);
    SearchResultVariant unavailableNeteaseVariant;
    unavailableNeteaseVariant.item = makeItem(
        SourceId::Netease, QStringLiteral("netease-unavailable"),
        QStringLiteral("netease://netease-unavailable"), false);
    SearchResultVariant qqVariant;
    qqVariant.item = makeItem(SourceId::QqMusic, QStringLiteral("qq-id"),
                              QStringLiteral("qqmusic://qq-id"), false);
    group.variants = { localVariant, unavailableNeteaseVariant, neteaseVariant, qqVariant };

    SongListView view;
    view.resize(1200, 320);
    view.setSourceAccessStates({
        { int(SourceId::Netease), SourceAccessState::Authenticated },
        { int(SourceId::QqMusic), SourceAccessState::Authenticated }
    });
    view.setSearchResultGroups({ group });
    view.show();
    QApplication::processEvents();

    QCOMPARE(view.model()->columnCount(), 8);
    QCOMPARE(view.model()->index(0, 0).data(SongListModel::StableIdentityRole).toString(),
             group.identity);
    QCOMPARE(view.songs().constFirst().sourceId(), SourceId::Local);

    QSignalSpy playSpy(&view, &SongListView::playRequested);
    QSignalSpy sourceSpy(&view, &SongListView::sourceActivated);
    const QRect sourceRect = view.visualRect(view.model()->index(0, 3));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, sourceRect.center());
    QCOMPARE(playSpy.count(), 0);
    QCOMPARE(sourceSpy.count(), 0);

    auto *local = view.findChild<QToolButton *>(QStringLiteral("sourceChoice_0"));
    auto *netease = view.findChild<QToolButton *>(QStringLiteral("sourceChoice_1"));
    auto *qq = view.findChild<QToolButton *>(QStringLiteral("sourceChoice_2"));
    QVERIFY(local);
    QVERIFY(netease);
    QVERIFY(qq);
    QVERIFY(local->isVisible());
    QVERIFY(netease->isVisible());
    QVERIFY(qq->isVisible());

    qq->click();
    QCOMPARE(playSpy.count(), 0);
    QCOMPARE(sourceSpy.count(), 0);
    QVERIFY(qq->toolTip().contains(QStringLiteral("版权限制")));

    netease->click();
    QCOMPARE(sourceSpy.count(), 1);
    QCOMPARE(playSpy.count(), 1);
    QCOMPARE(view.songs().constFirst().sourceId(), SourceId::Netease);
    QCOMPARE(view.songs().constFirst().remoteId, QStringLiteral("netease-id"));
    QCOMPARE(view.model()->index(0, 0).data(SongListModel::StableIdentityRole).toString(),
             group.identity);
}

void SongListViewTest::mergedCollectionKeepsMembersAndBatchActions()
{
    auto makeSong = [](qint64 id, SourceId source, const QString &remoteId) {
        Song song;
        song.id = id;
        song.source = int(source);
        song.remoteId = remoteId;
        song.filePath = QStringLiteral("%1://%2")
                            .arg(source == SourceId::Netease
                                     ? QStringLiteral("netease") : QStringLiteral("qqmusic"),
                                 remoteId);
        song.title = QStringLiteral("我不难过");
        song.artist = QStringLiteral("孙燕姿");
        song.album = QStringLiteral("未完成");
        song.durationMs = source == SourceId::Netease ? 320400 : 320000;
        return song;
    };
    const QList<SearchResultGroup> groups = SearchAggregator::aggregateSongsPreservingOrder({
        makeSong(101, SourceId::Netease, QStringLiteral("287398")),
        makeSong(102, SourceId::QqMusic, QStringLiteral("001fsNdn1zuZnA"))
    });
    QCOMPARE(groups.size(), 1);

    SongListView view;
    view.resize(1200, 420);
    view.setMergedCollectionActions(true);
    view.setSearchResultGroups(groups);
    view.show();
    QApplication::processEvents();

    QCOMPARE(view.songs().size(), 1);
    QCOMPARE(view.memberSongsAt(0).size(), 2);

    auto *batchToggle = view.findChild<QPushButton *>(QStringLiteral("batchToggle"));
    auto *selectAll = view.findChild<QPushButton *>(QStringLiteral("batchSelectAll"));
    auto *favorite = view.findChild<QPushButton *>(QStringLiteral("batchFavorite"));
    QVERIFY(batchToggle);
    QVERIFY(selectAll);
    QVERIFY(favorite);
    QSignalSpy favoriteSpy(&view, &SongListView::batchFavoriteRequested);

    batchToggle->click();
    selectAll->click();
    QCOMPARE(view.selectedSongs().size(), 1);
    QCOMPARE(view.selectedMemberSongs().size(), 2);
    favorite->click();
    QCOMPARE(favoriteSpy.count(), 1);
    const QList<QVariant> arguments = favoriteSpy.takeFirst();
    QCOMPARE(qvariant_cast<QList<Song>>(arguments.at(0)).size(), 2);
}

void SongListViewTest::collectionPagesDisplayMergedSourcesOnce()
{
    auto makeSong = [](qint64 id, SourceId source, const QString &remoteId) {
        Song song;
        song.id = id;
        song.source = int(source);
        song.remoteId = remoteId;
        song.filePath = QStringLiteral("%1://%2")
                            .arg(source == SourceId::Netease
                                     ? QStringLiteral("netease") : QStringLiteral("qqmusic"),
                                 remoteId);
        song.title = QStringLiteral("我不难过");
        song.artist = QStringLiteral("孙燕姿");
        song.album = QStringLiteral("未完成");
        song.durationMs = source == SourceId::Netease ? 320400 : 320000;
        return song;
    };
    const QList<Song> songs = {
        makeSong(201, SourceId::Netease, QStringLiteral("287398")),
        makeSong(202, SourceId::QqMusic, QStringLiteral("001fsNdn1zuZnA"))
    };

    FavoritesPage favorites;
    favorites.setSongs(songs, -1);
    QCOMPARE(favorites.currentSongs().size(), 1);
    QCOMPARE(favorites.memberSongsAt(0).size(), 2);

    SongListPage playlist;
    playlist.showContent(songs, QStringLiteral("测试歌单"), QStringLiteral("1 首"),
                         -1, true, QString(), true);
    playlist.resize(1200, 520);
    playlist.show();
    QApplication::processEvents();
    QCOMPARE(playlist.currentSongs().size(), 1);
    QCOMPARE(playlist.memberSongsAt(0).size(), 2);

    auto *playlistView = playlist.findChild<SongListView *>();
    QVERIFY(playlistView);
    QSignalSpy playSpy(&playlist, &SongListPage::playRequested);
    const QRect sourceRect = playlistView->visualRect(playlistView->model()->index(0, 3));
    QTest::mouseClick(playlistView->viewport(), Qt::LeftButton, Qt::NoModifier,
                      sourceRect.center());
    auto *qq = playlistView->findChild<QToolButton *>(QStringLiteral("sourceChoice_2"));
    QVERIFY(qq);
    qq->click();
    QCOMPARE(playSpy.count(), 1);
    const QList<QVariant> playArguments = playSpy.takeFirst();
    const QList<Song> playQueue = qvariant_cast<QList<Song>>(playArguments.at(0));
    QCOMPARE(playQueue.size(), 1);
    QCOMPARE(playQueue.constFirst().remoteId, QStringLiteral("001fsNdn1zuZnA"));
    QCOMPARE(playArguments.at(1).toInt(), 0);

    SongListPage ordinaryDetails;
    ordinaryDetails.showContent(songs, QStringLiteral("普通详情"), QStringLiteral("2 首"),
                                -1, false, QString(), false);
    QCOMPARE(ordinaryDetails.currentSongs().size(), 2);
}

void SongListViewTest::sourceSwitchIsVisibleAndExclusive()
{
    RecommendPage page;
    page.resize(1000, 700);
    page.show();
    QApplication::processEvents();

    auto *netease = page.findChild<QPushButton *>(QStringLiteral("neteaseSourceSwitch"));
    auto *qq = page.findChild<QPushButton *>(QStringLiteral("qqSourceSwitch"));
    QVERIFY(netease);
    QVERIFY(qq);
    QVERIFY(netease->isVisible());
    QVERIFY(qq->isVisible());
    QCOMPARE(netease->size(), QSize(40, 40));
    QCOMPARE(qq->size(), QSize(40, 40));
    QVERIFY(netease->isChecked());
    QVERIFY(!qq->isChecked());
    const auto fillColor = [](QPushButton *button) {
        const QImage image = button->grab().toImage();
        return image.pixelColor(image.width() / 10, image.height() / 2);
    };
    QCOMPARE(fillColor(netease), QColor(QStringLiteral("#3A2024")));
    QCOMPARE(fillColor(qq), QColor(Qt::transparent));

    QSignalSpy activationSpy(&page, &RecommendPage::sourceActivationRequested);
    qq->click();
    QApplication::processEvents();
    QCOMPARE(activationSpy.count(), 1);
    QCOMPARE(activationSpy.takeFirst().at(0).toInt(), int(SourceId::QqMusic));
    QVERIFY(!netease->isChecked());
    QVERIFY(qq->isChecked());
    QCOMPARE(fillColor(netease), QColor(Qt::transparent));
    QCOMPARE(fillColor(qq), QColor(QStringLiteral("#3A2024")));
}

void SongListViewTest::sourceIconResourcesPreserveSizeAndAlpha()
{
    const QList<SourceId> sources = {
        SourceId::Local, SourceId::Netease, SourceId::QqMusic
    };
    for (SourceId source : sources) {
        const QImage original(sourceIconPath(source, true));
        const QImage disabled(sourceIconPath(source, false));
        QVERIFY2(!original.isNull(), qPrintable(sourceIconPath(source, true)));
        QVERIFY2(!disabled.isNull(), qPrintable(sourceIconPath(source, false)));
        QCOMPARE(original.size(), QSize(200, 200));
        QCOMPARE(disabled.size(), original.size());
        for (int y = 0; y < original.height(); ++y) {
            for (int x = 0; x < original.width(); ++x) {
                const QColor sourcePixel = original.pixelColor(x, y);
                const QColor disabledPixel = disabled.pixelColor(x, y);
                const int expectedGray = qRound(0.299 * sourcePixel.red()
                    + 0.587 * sourcePixel.green() + 0.114 * sourcePixel.blue());
                QCOMPARE(disabledPixel.alpha(), sourcePixel.alpha());
                QCOMPARE(disabledPixel.red(), disabledPixel.green());
                QCOMPARE(disabledPixel.green(), disabledPixel.blue());
                QVERIFY(qAbs(disabledPixel.red() - expectedGray) <= 1);
            }
        }
    }
}

void SongListViewTest::sidebarFooterKeepsConfirmedGeometryAndRefreshIcon()
{
    SidebarFooter footer;
    footer.resize(240, 128);
    footer.show();
    QApplication::processEvents();

    auto *settings = footer.settingsButton();
    auto *refresh = footer.refreshButton();
    auto *ai = footer.aiReportButton();
    auto *download = footer.downloadButton();
    QVERIFY(settings);
    QVERIFY(refresh);
    QVERIFY(ai);
    QVERIFY(download);
    QCOMPARE(settings->size(), QSize(28, 28));
    QCOMPARE(refresh->size(), QSize(28, 28));
    QCOMPARE(ai->size(), QSize(28, 28));
    QCOMPARE(download->size(), QSize(28, 28));
    QCOMPARE(refresh->iconSize(), QSize(18, 18));
    QCOMPARE(ai->iconSize(), QSize(18, 18));
    QCOMPARE(settings->geometry().left(), 18);
    QCOMPARE(settings->geometry().bottom(), footer.height() - 18 - 1);
    QCOMPARE(refresh->geometry().left() - settings->geometry().right() - 1, 12);
    QCOMPARE(ai->geometry().left() - refresh->geometry().right() - 1, 12);
    QCOMPARE(download->geometry().left() - ai->geometry().right() - 1, 12);
    QCOMPARE(refresh->geometry().bottom(), settings->geometry().bottom());
    QCOMPARE(download->geometry().bottom(), settings->geometry().bottom());

    const QImage aiIcon = ai->icon().pixmap(ai->iconSize()).toImage();
    QVERIFY(!aiIcon.isNull());
    QCOMPARE(ai->toolTip(), QStringLiteral("AI 听歌报告"));

    const QImage icon = refresh->icon().pixmap(refresh->iconSize()).toImage();
    QVERIFY(!icon.isNull());
    QColor strongestPixel;
    int strongestAlpha = -1;
    for (int y = 0; y < icon.height(); ++y) {
        for (int x = 0; x < icon.width(); ++x) {
            const QColor pixel = icon.pixelColor(x, y);
            if (pixel.alpha() > strongestAlpha) {
                strongestAlpha = pixel.alpha();
                strongestPixel = pixel;
            }
        }
    }
    QVERIFY(strongestAlpha > 0);
    QCOMPARE(strongestPixel.rgb(), themeColor(ThemeColor::TextSecondary).rgb());

    QSignalSpy refreshSpy(&footer, &SidebarFooter::refreshClicked);
    QSignalSpy aiSpy(&footer, &SidebarFooter::aiReportClicked);
    QSignalSpy downloadSpy(&footer, &SidebarFooter::downloadClicked);
    refresh->click();
    ai->click();
    download->click();
    QCOMPARE(refreshSpy.count(), 1);
    QCOMPARE(aiSpy.count(), 1);
    QCOMPARE(downloadSpy.count(), 1);
}

void SongListViewTest::aiReportPageStartsLoadingAndCompletes()
{
    AiReportPage page;
    page.show();
    QApplication::processEvents();

    auto *status = page.findChild<QLabel *>(QStringLiteral("aiReportStatus"));
    auto *report = page.findChild<QTextBrowser *>(QStringLiteral("aiReportView"));
    auto *generate = page.findChild<QPushButton *>(QStringLiteral("aiReportGenerateButton"));
    QVERIFY(status);
    QVERIFY(report);
    QVERIFY(generate);

    page.startDemo();
    QCOMPARE(status->text(), QStringLiteral("AI 正在分析你的听歌偏好…"));
    QVERIFY(!report->isVisible());
    QVERIFY(!generate->isVisible());

    page.setDemoDelayMsForTesting(0);
    page.completeForTesting();
    QVERIFY(report->isVisible());
    QVERIFY(generate->isVisible());
    QVERIFY(report->toPlainText().contains(QStringLiteral("AI 听歌报告（演示版）")));
    QVERIFY(report->toPlainText().contains(QStringLiteral("推荐歌曲")));

    page.startDemo();
    QVERIFY(status->isVisible());
    QVERIFY(!report->isVisible());
    QVERIFY(!generate->isVisible());
}

void SongListViewTest::manualRecommendRefreshUsesPlatformTopListsAndFeedback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QSettings::Format previousFormat = QSettings::defaultFormat();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    const QString previousQqUserId = SettingsService::qqUserId();
    struct RestoreSettings
    {
        QString qqUserId;
        QSettings::Format format = QSettings::NativeFormat;
        ~RestoreSettings()
        {
            SettingsService::setQqUserId(qqUserId);
            SettingsService::setRecommendCachePathOverride(QString());
            QSettings::setDefaultFormat(format);
        }
    } restore{ previousQqUserId, previousFormat };

    const QString recommendPath = dir.filePath(QStringLiteral("recommend.json"));
    SettingsService::setRecommendCachePathOverride(recommendPath);
    SettingsService::setQqUserId(QStringLiteral("qq-refresh-test"));
    QSettings().sync();
    QCOMPARE(SettingsService::qqUserId(), QStringLiteral("qq-refresh-test"));

    RecordingQqRecommendSource source;
    source.songs = QJsonArray{
        QJsonObject{
            { QStringLiteral("remoteId"), QStringLiteral("QQ_REFRESH_SONG") },
            { QStringLiteral("title"), QStringLiteral("刷新歌曲") },
            { QStringLiteral("artist"), QStringLiteral("刷新歌手") },
            { QStringLiteral("album"), QStringLiteral("刷新专辑") },
            { QStringLiteral("durationMs"), QStringLiteral("180000") }
        }
    };
    source.playlists = QJsonArray{
        QJsonObject{
            { QStringLiteral("remoteId"), QStringLiteral("QQ_REFRESH_PLAYLIST") },
            { QStringLiteral("name"), QStringLiteral("刷新歌单") }
        }
    };

    RecommendPage page;
    page.setSourceProvider(&source, nullptr);
    QCOMPARE(page.activeSourceId(), SourceId::QqMusic);
    QSignalSpy stateSpy(&page, &RecommendPage::refreshStateChanged);

    page.refresh(true);
    QCOMPARE(source.recommendCalls, 1);
    QCOMPARE(source.userPlaylistCalls, 0);
    QCOMPARE(source.playlistOffsets, QList<int>{ 6 });
    QCOMPARE(page.currentSongs().size(), 1);
    QCOMPARE(page.currentSongs().first().remoteId, QStringLiteral("QQ_REFRESH_SONG"));
    QCOMPARE(stateSpy.count(), 2);
    QCOMPARE(stateSpy.at(0).at(0).toBool(), true);
    QCOMPARE(stateSpy.at(1).at(0).toBool(), false);
    QCOMPARE(stateSpy.at(1).at(1).toString(), QStringLiteral("推荐内容已刷新"));
    const QString qqCachePath = QFileInfo(recommendPath).absolutePath()
        + QStringLiteral("/recommend-qq.json");
    QVERIFY(QFileInfo(qqCachePath).isFile());

    stateSpy.clear();
    page.refresh(true);
    QCOMPARE(source.recommendCalls, 2);
    QCOMPARE(source.userPlaylistCalls, 0);
    QCOMPARE(source.playlistOffsets, (QList<int>{ 6, 12 }));
    QCOMPARE(stateSpy.count(), 2);
    QCOMPARE(stateSpy.at(1).at(1).toString(),
             QStringLiteral("刷新成功，平台返回内容未变化"));
}

void SongListViewTest::recommendedPlaylistsScrollAtNarrowWidths()
{
    struct CachePathGuard
    {
        ~CachePathGuard()
        {
            SettingsService::setRecommendCachePathOverride(QString());
        }
    } cachePathGuard;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SettingsService::setRecommendCachePathOverride(
        dir.filePath(QStringLiteral("recommend.json")));
    const QString cachePath = SettingsService::recommendCachePath();
    QVERIFY(QDir().mkpath(QFileInfo(cachePath).absolutePath()));
    QJsonArray playlists;
    for (int i = 0; i < 6; ++i) {
        playlists.append(QJsonObject{
            { QStringLiteral("id"), QString::number(i + 1) },
            { QStringLiteral("name"), QStringLiteral("推荐歌单 %1").arg(i + 1) }
        });
    }
    QFile cache(cachePath);
    QVERIFY(cache.open(QIODevice::WriteOnly | QIODevice::Truncate));
    cache.write(QJsonDocument(QJsonObject{
        { QStringLiteral("songs"), QJsonArray() },
        { QStringLiteral("playlists"), playlists }
    }).toJson(QJsonDocument::Compact));
    cache.close();

    OfflineRecommendSource source;
    RecommendPage page;
    page.resize(700, 700);
    page.setSourceProvider(&source, nullptr);
    page.refresh();
    page.show();
    QApplication::processEvents();

    auto *scroll = page.findChild<QScrollArea *>(QStringLiteral("recommendedPlaylistScroll"));
    auto *host = page.findChild<QWidget *>(QStringLiteral("recommendedPlaylistHost"));
    QVERIFY(scroll);
    QVERIFY(host);
    QCOMPARE(scroll->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QCOMPARE(page.findChildren<CoverCard *>().size(), 6);
    QVERIFY(host->minimumWidth() >= 6 * 132 + 5 * 16);
    QTRY_VERIFY(scroll->horizontalScrollBar()->maximum() > 0);

    scroll->horizontalScrollBar()->setValue(scroll->horizontalScrollBar()->maximum());
    QCOMPARE(scroll->horizontalScrollBar()->value(), scroll->horizontalScrollBar()->maximum());

    page.resize(700, 328);
    QApplication::processEvents();
    auto *topScroll = page.findChild<QScrollArea *>(QStringLiteral("recommendTopScroll"));
    auto *dailyTitle = page.findChild<QLabel *>(QStringLiteral("recommendedDailyTitle"));
    QVERIFY(topScroll);
    QVERIFY(dailyTitle);
    QVERIFY(dailyTitle->isVisible());
    QVERIFY(page.findChild<SongListView *>()->height() >= 130);
    QVERIFY(topScroll->geometry().bottom() < dailyTitle->geometry().top());
    QVERIFY(topScroll->verticalScrollBar()->maximum() > 0);
}

void SongListViewTest::accountActionIsExplicitAndPreservesSignal()
{
    AccountPanel panel;
    panel.resize(200, 52);
    panel.show();
    QApplication::processEvents();

    auto *button = panel.findChild<QPushButton *>(QStringLiteral("accountActionButton"));
    QVERIFY(button);
    QVERIFY(button->isVisible());
    QVERIFY(!button->text().trimmed().isEmpty());

    QSignalSpy accountSpy(&panel, &AccountPanel::accountClicked);
    button->click();
    QCOMPARE(accountSpy.count(), 1);
}

void SongListViewTest::qqOnlyAccountFallsBackToQqIdentity()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QSettings().clear();
    SettingsService::setOnlineUid(0);
    SettingsService::setQqUserId(QStringLiteral("wx-qq-user"));
    SettingsService::setQqNickname(QStringLiteral("微信登录的 QQ 账号"));
    SettingsService::setAccountDisplaySource(0);

    AccountPanel panel;
    auto *button = panel.findChild<QPushButton *>(QStringLiteral("accountActionButton"));
    QVERIFY(button);
    QCOMPARE(button->text(), QStringLiteral("微信登录的 QQ 账号"));
    QCOMPARE(SettingsService::accountDisplaySource(), 1);
}

void SongListViewTest::vipBadgesReflectAccountAndSelectedSong()
{
    SettingsService::setOnlineUid(0);
    SettingsService::setQqUserId(QStringLiteral("vip-user"));
    SettingsService::setQqNickname(QStringLiteral("VIP 用户"));
    SettingsService::setAccountDisplaySource(1);
    SettingsService::setQqVipStatus(1);
    AccountPanel account;
    auto *accountBadge = account.findChild<QLabel *>(QStringLiteral("accountVipBadge"));
    QVERIFY(accountBadge);
    QVERIFY(accountBadge->isVisibleTo(&account));
    QVERIFY(!accountBadge->pixmap().isNull());

    PlayerBar player;
    Song vipSong;
    vipSong.id = 3001;
    vipSong.title = QStringLiteral("会员歌曲");
    vipSong.source = int(SourceId::QqMusic);
    vipSong.remoteId = QStringLiteral("vip-song");
    vipSong.accessRequirement = AccessRequirement::Vip;
    player.setSong(vipSong, false);
    auto *songBadge = player.findChild<QLabel *>(QStringLiteral("playerVipBadge"));
    QVERIFY(songBadge);
    QVERIFY(songBadge->isVisibleTo(&player));

    vipSong.accessRequirement = AccessRequirement::Purchase;
    player.setSong(vipSong, false);
    QVERIFY(!songBadge->isVisible());

    SettingsService::setQqUserId(QString());
    SettingsService::setQqNickname(QString());
    SettingsService::setQqVipStatus(-1);
    SettingsService::setAccountDisplaySource(-1);
}

void SongListViewTest::downloadPageKeepsFixedRowsAndByteProgress()
{
    DownloadPage page;
    page.resize(1200, 620);
    QList<DownloadService::Task> tasks;
    for (int i = 0; i < 100; ++i) {
        DownloadService::Task task;
        task.id = i + 1;
        task.song.id = i + 1;
        task.song.title = QStringLiteral("下载歌曲 %1").arg(i + 1);
        task.song.artist = QStringLiteral("歌手");
        task.song.album = QStringLiteral("专辑");
        task.state = i == 0 ? DownloadService::Downloading : DownloadService::Queued;
        task.receivedBytes = i == 0 ? 512 * 1024 : 0;
        task.totalBytes = i == 0 ? 1024 * 1024 : 0;
        task.percent = i == 0 ? 50 : 0;
        tasks.append(task);
    }
    page.setTasks(tasks);
    page.show();
    QApplication::processEvents();

    const QList<QWidget *> rows =
        page.findChildren<QWidget *>(QStringLiteral("downloadTaskRow"));
    QCOMPARE(rows.size(), 100);
    for (QWidget *row : rows)
        QCOMPARE(row->height(), 76);
    auto *scroll = page.findChild<QScrollArea *>(QStringLiteral("downloadTaskScroll"));
    QVERIFY(scroll);
    QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
    bool foundProgress = false;
    for (QLabel *label : rows.first()->findChildren<QLabel *>()) {
        if (label->text().contains(QStringLiteral("512.0 KB / 1.0 MB"))) {
            foundProgress = true;
            break;
        }
    }
    QVERIFY(foundProgress);
}

void SongListViewTest::songListNavigationStateRestoresDetailContext()
{
    Song first;
    first.id = 7001;
    first.title = QStringLiteral("原歌单歌曲");
    first.filePath = QStringLiteral("C:/music/original.mp3");
    Song replacement = first;
    replacement.id = 7002;
    replacement.title = QStringLiteral("后续详情歌曲");

    SongListPage page;
    page.showContent({ first }, QStringLiteral("原歌单"), QStringLiteral("1 首"),
                     first.id, true, QString(), true);
    page.setPlaylistContext(88);
    const SongListPage::NavigationState saved = page.navigationState();
    page.showContent({ replacement }, QStringLiteral("歌手详情"), QStringLiteral("歌手 · 1 首"),
                     replacement.id, false);
    page.setReadOnlyContext();
    page.restoreNavigationState(saved);

    const SongListPage::NavigationState restored = page.navigationState();
    QCOMPARE(restored.title, QStringLiteral("原歌单"));
    QCOMPARE(restored.playlistContext, 88);
    QVERIFY(!restored.readOnlyContext);
    QVERIFY(restored.mergeSources);
    QCOMPARE(page.currentSongs().size(), 1);
    QCOMPARE(page.currentSongs().first().id, first.id);
}

void SongListViewTest::layoutComponentsFollowConfirmedGeometry()
{
    TitleBar titleBar;
    titleBar.resize(1200, 48);
    titleBar.show();
    QApplication::processEvents();
    auto *search = titleBar.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    QVERIFY(search);
    QCOMPARE(search->size(), QSize(520, 36));
    QVERIFY(!titleBar.findChild<QLabel *>(QStringLiteral("brandName")));
    titleBar.resize(700, 48);
    QApplication::processEvents();
    QCOMPARE(search->width(), 430);
    QVERIFY(titleBar.rect().contains(titleBar.searchRect()));
    for (int button = TitleBar::MinimizeBtn; button <= TitleBar::CloseBtn; ++button)
        QVERIFY(titleBar.rect().contains(titleBar.windowButtonRect(button)));

    SideBar sidebar;
    QCOMPARE(sidebar.width(), 240);
    const QList<QPushButton *> buttons = sidebar.findChildren<QPushButton *>();
    int navButtons = 0;
    for (QPushButton *button : buttons) {
        if (button->isCheckable() && button->height() == 84)
            ++navButtons;
    }
    QCOMPARE(navButtons, 4);

    AccountPanel account;
    QCOMPARE(account.height(), 244);
    auto *avatar = account.findChild<QPushButton *>(QStringLiteral("accountAvatarButton"));
    QVERIFY(avatar);
    QCOMPARE(avatar->size(), QSize(120, 120));

    AccountSettingsButton settings;
    QCOMPARE(settings.size(), QSize(28, 28));

    PlayerBar player;
    QFile theme(QStringLiteral(":/theme.qss"));
    QVERIFY(theme.open(QIODevice::ReadOnly));
    player.setStyleSheet(ThemeManager::instance().renderStyleSheet(QString::fromUtf8(theme.readAll())));
    QCOMPARE(player.height(), 204);
    player.resize(1200, 204);
    player.show();
    QApplication::processEvents();
    auto *leftBox = player.findChild<QWidget *>(QStringLiteral("playerLeftBox"));
    auto *actionBox = player.findChild<QWidget *>(QStringLiteral("playerActionBox"));
    auto *centerBox = player.findChild<QWidget *>(QStringLiteral("playerCenterBox"));
    auto *rightBox = player.findChild<QWidget *>(QStringLiteral("playerRightBox"));
    QVERIFY(leftBox);
    QVERIFY(actionBox);
    QVERIFY(centerBox);
    QVERIFY(rightBox);
    QVERIFY(leftBox->isVisible());
    QVERIFY(actionBox->isVisible());
    QVERIFY(centerBox->isVisible());
    QVERIFY(rightBox->isVisible());
    QCOMPARE(player.grab().toImage().pixelColor(2, 2),
             themeColor(ThemeColor::PageBackground));

    player.resize(700, 204);
    QApplication::processEvents();
    QVERIFY(player.findChild<QLabel *>(QStringLiteral("playerTitle"))->isVisible());
    QVERIFY(player.findChild<QLabel *>(QStringLiteral("playerArtist"))->isVisible());
    QVERIFY(player.findChild<QWidget *>(QStringLiteral("playerProgress"))->isVisible());
    QVERIFY(player.findChild<QPushButton *>(QStringLiteral("favoriteActionButton"))->isVisible());
    QVERIFY(player.findChild<QPushButton *>(QStringLiteral("downloadActionButton"))->isVisible());
    QVERIFY(player.findChild<QPushButton *>(QStringLiteral("previousTrackButton"))->isVisible());
    QVERIFY(player.findChild<QPushButton *>(QStringLiteral("playPauseBtn"))->isVisible());
    QVERIFY(player.findChild<QPushButton *>(QStringLiteral("nextTrackButton"))->isVisible());
    QVERIFY(player.findChild<QPushButton *>(QStringLiteral("playerModeButton"))->isVisible());
    QVERIFY(!player.findChild<QPushButton *>(QStringLiteral("playerLyricsButton"))->isVisible());
    QVERIFY(!player.findChild<QPushButton *>(QStringLiteral("playerQueueButton"))->isVisible());
}

void SongListViewTest::singleClickPlaysContentOnly()
{
    Song song;
    song.id = 1001;
    song.source = int(SourceId::Netease);
    song.remoteId = QStringLiteral("single-click-play");
    song.filePath = QStringLiteral("netease://single-click-play");
    song.title = QStringLiteral("Single click");

    SongListView view;
    view.resize(1100, 260);
    view.setSongs({ song });
    view.show();
    QApplication::processEvents();

    QSignalSpy playSpy(&view, &SongListView::playRequested);
    QSignalSpy heartSpy(&view, &SongListView::heartRequested);
    QSignalSpy downloadSpy(&view, &SongListView::downloadRequested);

    const QRect titleRect = view.visualRect(view.model()->index(0, 2));
    QVERIFY(titleRect.isValid());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, titleRect.center());
    QCOMPARE(playSpy.count(), 1);
    QCOMPARE(playSpy.takeFirst().at(0).toInt(), 0);

    const QRect heartRect = view.visualRect(view.model()->index(0, 6));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, heartRect.center());
    QCOMPARE(heartSpy.count(), 1);
    QCOMPARE(playSpy.count(), 0);

    const QRect downloadRect = view.visualRect(view.model()->index(0, 7));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, downloadRect.center());
    QCOMPARE(downloadSpy.count(), 1);
    QCOMPARE(playSpy.count(), 0);

    view.findChild<QPushButton *>(QStringLiteral("batchToggle"))->click();
    QApplication::processEvents();
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, titleRect.center());
    QCOMPARE(playSpy.count(), 0);

    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, titleRect.center());
    QCOMPARE(playSpy.count(), 0);
}

void SongListViewTest::playbackActivityTracksRealState()
{
    Song song;
    song.id = 1101;
    song.filePath = QStringLiteral("C:/music/equalizer.mp3");
    song.title = QStringLiteral("Equalizer");

    SongListView view;
    view.resize(900, 240);
    view.setSongs({ song }, song.id);
    view.show();
    QApplication::processEvents();
    QVERIFY(!view.playbackActive());

    QSignalSpy playSpy(&view, &SongListView::playRequested);
    view.setPlaybackActive(true);
    QVERIFY(view.playbackActive());
    QCOMPARE(playSpy.count(), 0);
    view.hide();
    QApplication::processEvents();
    view.show();
    QApplication::processEvents();

    view.setPlayingId(song.id);
    view.setPlaybackActive(false);
    QVERIFY(!view.playbackActive());
    QCOMPARE(playSpy.count(), 0);
}

void SongListViewTest::fullCoverPlaylistCardKeepsVisibleGeometry()
{
    QPixmap cover(220, 160);
    cover.fill(QColor(QStringLiteral("#345678")));

    CoverCard card;
    card.setFixedCardSize(132, 116);
    card.setFullCoverCard(true);
    card.setCover(cover);
    card.setText(QStringLiteral("完整封面歌单"));
    QCOMPARE(card.size(), QSize(132, 168));
    card.show();
    QApplication::processEvents();

    QSignalSpy clickSpy(&card, &CoverCard::clicked);
    QTest::mouseClick(&card, Qt::LeftButton, Qt::NoModifier, QPoint(66, 80));
    QCOMPARE(clickSpy.count(), 1);
    QCOMPARE(card.size(), QSize(132, 168));
}

void SongListViewTest::newPlaylistCardKeepsClickContract()
{
    CoverCard card;
    card.setFixedCardSize(132, 116);
    card.setFullCoverCard(true);
    card.setNewPlaylistCard(true);
    card.setText(QStringLiteral("新建歌单"));
    QCOMPARE(card.size(), QSize(132, 168));
    QCOMPARE(card.accessibleName(), QStringLiteral("新建歌单"));
    card.show();
    QApplication::processEvents();

    QSignalSpy clickSpy(&card, &CoverCard::clicked);
    QTest::mouseMove(&card, QPoint(112, 28));
    QTest::qWait(220);
    QTest::mouseClick(&card, Qt::LeftButton, Qt::NoModifier, QPoint(112, 28));
    QCOMPARE(clickSpy.count(), 1);
    QCOMPARE(card.size(), QSize(132, 168));
}

QTEST_MAIN(SongListViewTest)
#include "tst_uiwidgets.moc"
