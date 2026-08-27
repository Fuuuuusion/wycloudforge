#include "core/LibraryService.h"
#include "core/PlaylistController.h"

#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace core;

class PlaylistControllerTest : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void createRenameDelete();
    void addRemoveMoveSongs();
    void favorites();
    void recentPlays();

private:
    QTemporaryDir *m_dir = nullptr;
    LibraryService *m_library = nullptr;
    PlaylistController *m_controller = nullptr;
};

void PlaylistControllerTest::init()
{
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    LibraryService::setDatabasePathOverride(m_dir->filePath(QStringLiteral("t.db")));
    m_library = new LibraryService;
    QVERIFY(m_library->openDatabase());
    m_controller = new PlaylistController;
    m_controller->setDatabase(m_library->database());
}

void PlaylistControllerTest::cleanup()
{
    delete m_controller;
    delete m_library;
    delete m_dir;
    LibraryService::setDatabasePathOverride(QString());
}

void PlaylistControllerTest::createRenameDelete()
{
    QCOMPARE(m_controller->playlists().size(), 1); // 我喜欢的音乐
    const int id = m_controller->createPlaylist(QStringLiteral("测试歌单"));
    QVERIFY(id > 1);
    QCOMPARE(m_controller->playlists().size(), 2);
    QVERIFY(m_controller->renamePlaylist(id, QStringLiteral("新名字")));
    QVERIFY(m_controller->playlists().last().name == QStringLiteral("新名字"));
    QVERIFY(m_controller->deletePlaylist(id));
    QCOMPARE(m_controller->playlists().size(), 1);
}

void PlaylistControllerTest::addRemoveMoveSongs()
{
    QSqlQuery q(m_library->database());
    for (int i = 0; i < 3; ++i) {
        q.prepare(QStringLiteral(
            "INSERT INTO songs(path,title,artist,album,duration_ms) VALUES(?,?,?,?,?)"));
        q.addBindValue(QStringLiteral("/tmp/song%1.mp3").arg(i));
        q.addBindValue(QStringLiteral("歌%1").arg(i + 1));
        q.addBindValue(QStringLiteral("歌手"));
        q.addBindValue(QStringLiteral("专辑"));
        q.addBindValue(1000 * (i + 1));
        QVERIFY(q.exec());
    }

    const int pl = m_controller->createPlaylist(QStringLiteral("队列"));
    QVERIFY(m_controller->addSong(pl, 1));
    QVERIFY(m_controller->addSong(pl, 2));
    QVERIFY(m_controller->addSong(pl, 3));
    auto songs = m_controller->songsOf(pl);
    QCOMPARE(songs.size(), 3);
    QCOMPARE(songs[0].title, QStringLiteral("歌1"));

    QVERIFY(m_controller->moveSong(pl, 0, 2));
    songs = m_controller->songsOf(pl);
    QCOMPARE(songs[0].title, QStringLiteral("歌2"));
    QCOMPARE(songs[2].title, QStringLiteral("歌1"));

    QVERIFY(m_controller->removeSong(pl, songs[0].id));
    QCOMPARE(m_controller->songsOf(pl).size(), 2);
}

void PlaylistControllerTest::favorites()
{
    QSqlQuery q(m_library->database());
    q.prepare(QStringLiteral("INSERT INTO songs(path,title) VALUES(?,?)"));
    q.addBindValue(QStringLiteral("/tmp/fav.mp3"));
    q.addBindValue(QStringLiteral("收藏歌"));
    QVERIFY(q.exec());
    const qint64 songId = q.lastInsertId().toLongLong();

    QVERIFY(!m_controller->isFavorite(songId));
    m_controller->setFavorite(songId, true);
    QVERIFY(m_controller->isFavorite(songId));
    QCOMPARE(m_controller->songsOf(m_controller->favoritePlaylistId()).size(), 1);
    m_controller->setFavorite(songId, false);
    QVERIFY(!m_controller->isFavorite(songId));
}

void PlaylistControllerTest::recentPlays()
{
    QSqlQuery q(m_library->database());
    q.prepare(QStringLiteral("INSERT INTO songs(path,title) VALUES(?,?)"));
    q.addBindValue(QStringLiteral("/tmp/r1.mp3"));
    q.addBindValue(QStringLiteral("最近一"));
    QVERIFY(q.exec());
    const qint64 id1 = q.lastInsertId().toLongLong();
    q.prepare(QStringLiteral("INSERT INTO songs(path,title) VALUES(?,?)"));
    q.addBindValue(QStringLiteral("/tmp/r2.mp3"));
    q.addBindValue(QStringLiteral("最近二"));
    QVERIFY(q.exec());
    const qint64 id2 = q.lastInsertId().toLongLong();

    m_controller->recordPlay(id1);
    m_controller->recordPlay(id2);
    const auto recent = m_controller->recentSongs(10);
    QCOMPARE(recent.size(), 2);
    QCOMPARE(recent[0].title, QStringLiteral("最近二"));
}

QTEST_MAIN(PlaylistControllerTest)
#include "tst_playlistcontroller.moc"
