#include "ui/SongListView.h"
#include "ui/SongListModel.h"
#include "ui/PlayerBar.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QFile>
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

QTEST_MAIN(SongListViewTest)
#include "tst_songlistview.moc"
