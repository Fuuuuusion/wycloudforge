#include "ui/SongListView.h"
#include "ui/SongListModel.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QLabel>
#include <QPushButton>
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

QTEST_MAIN(SongListViewTest)
#include "tst_songlistview.moc"
