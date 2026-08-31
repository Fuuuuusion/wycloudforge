#include "ui/SongListView.h"
#include "ui/SongListModel.h"

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

QTEST_MAIN(SongListViewTest)
#include "tst_songlistview.moc"
