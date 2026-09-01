#include "ui/SongListView.h"
#include "ui/SongListModel.h"
#include "ui/PlayerBar.h"
#include "ui/CoverCard.h"
#include "ui/RecommendPage.h"
#include "ui/AccountPanel.h"
#include "ui/AccountSettingsButton.h"
#include "ui/SideBar.h"
#include "ui/SourceIcons.h"
#include "ui/TitleBar.h"
#include "core/NeteaseApiClient.h"
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
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
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

class SongListViewTest : public QObject
{
    Q_OBJECT
private slots:
    void batchEntryAndStableSelection();
    void rowUpdatePreservesModelAndBatchSelection();
    void favoriteActionKeepsPlaySignalSeparate();
    void singleDeleteActionKeepsPlaySignalSeparate();
    void batchDeleteButtonKeepsTextAndSignal();
    void downloadStateUsesStableIdentity();
    void playerBarDownloadStateKeepsSignalsSeparate();
    void playerBarDirectionalControlsKeepGeometryAndSignals();
    void playerBarHighDpiIconsRemainComplete();
    void rowHoverKeepsBackgroundClear();
    void rowHoverClearsWhenPointerLeavesViewport();
    void rowHoverRapidTransitionsRepaintInterruptedRow();
    void rowHoverClearsOverBatchBarAndPlayingStaysIndependent();
    void songListProvidesScrollableTopAndBottomSafeAreas();
    void sourcePickerRequiresSecondClickAndKeepsGroupIdentity();
    void sourceSwitchIsVisibleAndExclusive();
    void sourceIconResourcesPreserveSizeAndAlpha();
    void recommendedPlaylistsScrollAtNarrowWidths();
    void accountActionIsExplicitAndPreservesSignal();
    void layoutComponentsFollowConfirmedGeometry();
    void singleClickPlaysContentOnly();
    void playbackActivityTracksRealState();
    void fullCoverPlaylistCardKeepsVisibleGeometry();
    void newPlaylistCardKeepsClickContract();
};

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

void SongListViewTest::rowHoverKeepsBackgroundClear()
{
    Song song;
    song.id = 901;
    song.filePath = QStringLiteral("C:/music/hover-background.mp3");
    song.title = QStringLiteral("Hover background");
    song.artist = QStringLiteral("Artist");

    SongListView view;
    view.resize(1000, 240);
    view.setStyleSheet(QStringLiteral("QTableView{background:#0E0E14;}"));
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

void SongListViewTest::recommendedPlaylistsScrollAtNarrowWidths()
{
    struct EnvironmentGuard
    {
        explicit EnvironmentGuard(const char *key, const QByteArray &value)
            : name(key), existed(qEnvironmentVariableIsSet(key)), previous(qgetenv(key))
        {
            qputenv(name.constData(), value);
        }
        ~EnvironmentGuard()
        {
            if (existed)
                qputenv(name.constData(), previous);
            else
                qunsetenv(name.constData());
        }
        QByteArray name;
        bool existed = false;
        QByteArray previous;
    };

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    EnvironmentGuard appData("APPDATA", dir.path().toUtf8());
    EnvironmentGuard localAppData("LOCALAPPDATA", dir.path().toUtf8());
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
    QCOMPARE(account.height(), 224);
    auto *avatar = account.findChild<QPushButton *>(QStringLiteral("accountAvatarButton"));
    QVERIFY(avatar);
    QCOMPARE(avatar->size(), QSize(120, 120));

    AccountSettingsButton settings;
    QCOMPARE(settings.size(), QSize(28, 28));

    PlayerBar player;
    QCOMPARE(player.height(), 224);
    player.resize(1200, 224);
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
    QCOMPARE(player.grab().toImage().pixelColor(2, 2), QColor(QStringLiteral("#14141B")));

    player.resize(700, 224);
    QApplication::processEvents();
    QVERIFY(player.findChild<QLabel *>(QStringLiteral("playerTitle"))->isVisible());
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
#include "tst_songlistview.moc"
