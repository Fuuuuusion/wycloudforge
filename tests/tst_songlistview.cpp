#include "ui/SongListView.h"
#include "ui/SongListModel.h"
#include "ui/PlayerBar.h"
#include "ui/CoverCard.h"
#include "ui/RecommendPage.h"
#include "ui/AccountPanel.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

using namespace core;
using namespace ui;

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
    void rowHoverKeepsBackgroundClear();
    void rowHoverClearsWhenPointerLeavesViewport();
    void songListProvidesScrollableTopAndBottomSafeAreas();
    void sourceSwitchIsVisibleAndExclusive();
    void accountActionIsExplicitAndPreservesSignal();
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
    const QModelIndex favoriteIndex = view.model()->index(0, 5);
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
    const QRect actionRect = view.visualRect(view.model()->index(0, 6));
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
    const QModelIndex actionIndex = view.model()->index(0, 6);
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
    QVERIFY(previous);
    QVERIFY(next);
    QCOMPARE(previous->size(), QSize(30, 30));
    QCOMPARE(next->size(), QSize(30, 30));
    QCOMPARE(previous->toolTip(), QStringLiteral("上一首"));
    QCOMPARE(next->toolTip(), QStringLiteral("下一首"));

    bar.setFixedWidth(716);
    bar.show();
    QApplication::processEvents();
    QVERIFY(previous->geometry().left() >= 0);
    QVERIFY(previous->geometry().right() < bar.width());
    QVERIFY(next->geometry().left() >= 0);
    QVERIFY(next->geometry().right() < bar.width());
    QCOMPARE(previous->size(), QSize(30, 30));
    QCOMPARE(next->size(), QSize(30, 30));

    QSignalSpy previousSpy(&bar, &PlayerBar::prevClicked);
    QSignalSpy nextSpy(&bar, &PlayerBar::nextClicked);
    previous->click();
    next->click();
    QCOMPARE(previousSpy.count(), 1);
    QCOMPARE(nextSpy.count(), 1);
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
    QCOMPARE(middle.height(), 64);
    QCOMPARE(first.height() - middle.height(), 42);
    QCOMPARE(last.height() - middle.height(), 94);
    QCOMPARE(first.width(), middle.width());
    QCOMPARE(last.width(), middle.width());
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
    QCOMPARE(netease->size(), QSize(76, 30));
    QCOMPARE(qq->size(), QSize(76, 30));
    QVERIFY(netease->isChecked());
    QVERIFY(!qq->isChecked());
    const auto fillColor = [](QPushButton *button) {
        const QImage image = button->grab().toImage();
        return image.pixelColor(image.width() / 10, image.height() / 2);
    };
    QCOMPARE(fillColor(netease), QColor(QStringLiteral("#EC4141")));
    QCOMPARE(fillColor(qq), QColor(QStringLiteral("#1B1B24")));

    QSignalSpy activationSpy(&page, &RecommendPage::sourceActivationRequested);
    qq->click();
    QApplication::processEvents();
    QCOMPARE(activationSpy.count(), 1);
    QCOMPARE(activationSpy.takeFirst().at(0).toInt(), int(SourceId::QqMusic));
    QVERIFY(!netease->isChecked());
    QVERIFY(qq->isChecked());
    QCOMPARE(fillColor(netease), QColor(QStringLiteral("#1B1B24")));
    QCOMPARE(fillColor(qq), QColor(QStringLiteral("#EC4141")));
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
    QVERIFY(button->text() == QStringLiteral("登录")
            || button->text().startsWith(QStringLiteral("查看账号 · ")));

    QSignalSpy accountSpy(&panel, &AccountPanel::accountClicked);
    button->click();
    QCOMPARE(accountSpy.count(), 1);
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

    const QRect titleRect = view.visualRect(view.model()->index(0, 1));
    QVERIFY(titleRect.isValid());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, titleRect.center());
    QCOMPARE(playSpy.count(), 1);
    QCOMPARE(playSpy.takeFirst().at(0).toInt(), 0);

    const QRect heartRect = view.visualRect(view.model()->index(0, 5));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, heartRect.center());
    QCOMPARE(heartSpy.count(), 1);
    QCOMPARE(playSpy.count(), 0);

    const QRect downloadRect = view.visualRect(view.model()->index(0, 6));
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
