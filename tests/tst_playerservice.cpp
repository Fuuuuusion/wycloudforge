#include "core/PlayerService.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace core;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(bytes) == bytes.size();
}

void writeWav(const QString &path, int seconds)
{
    const int sampleRate = 22050;
    const int dataSize = sampleRate * 2 * seconds;
    QByteArray data(dataSize, '\0');
    QByteArray out;
    out.append("RIFF");
    out.append(char((36 + dataSize) & 0xFF));
    out.append(char(((36 + dataSize) >> 8) & 0xFF));
    out.append(char(((36 + dataSize) >> 16) & 0xFF));
    out.append(char(((36 + dataSize) >> 24) & 0xFF));
    out.append("WAVEfmt ");
    out.append(QByteArrayLiteral("\x10\x00\x00\x00"));
    out.append(QByteArrayLiteral("\x01\x00\x01\x00"));
    out.append(char(sampleRate & 0xFF));
    out.append(char((sampleRate >> 8) & 0xFF));
    out.append(char((sampleRate >> 16) & 0xFF));
    out.append(char((sampleRate >> 24) & 0xFF));
    const int byteRate = sampleRate * 2;
    out.append(char(byteRate & 0xFF));
    out.append(char((byteRate >> 8) & 0xFF));
    out.append(char((byteRate >> 16) & 0xFF));
    out.append(char((byteRate >> 24) & 0xFF));
    out.append(QByteArrayLiteral("\x02\x00\x10\x00"));
    out.append("data");
    out.append(char(dataSize & 0xFF));
    out.append(char((dataSize >> 8) & 0xFF));
    out.append(char((dataSize >> 16) & 0xFF));
    out.append(char((dataSize >> 24) & 0xFF));
    out.append(data);
    QVERIFY2(writeFile(path, out), "failed to write wav");
}

} // namespace

class PlayerServiceTest : public QObject
{
    Q_OBJECT
private slots:
    void playlistNavigation();
    void playPauseAndPosition();
};

void PlayerServiceTest::playlistNavigation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString p1 = dir.filePath(QStringLiteral("a.wav"));
    const QString p2 = dir.filePath(QStringLiteral("b.wav"));
    writeWav(p1, 2);
    writeWav(p2, 2);

    Song s1;
    s1.id = 1;
    s1.filePath = p1;
    s1.title = QStringLiteral("A");
    s1.durationMs = 2000;
    Song s2;
    s2.id = 2;
    s2.filePath = p2;
    s2.title = QStringLiteral("B");
    s2.durationMs = 2000;

    PlayerService player;
    QSignalSpy spy(&player, &PlayerService::songChanged);
    player.setPlaylist({ s1, s2 }, 0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(player.currentSong().title, QStringLiteral("A"));

    player.next();
    QCOMPARE(player.currentIndex(), 1);
    QCOMPARE(player.currentSong().title, QStringLiteral("B"));
    QCOMPARE(spy.count(), 2);

    player.prev();
    QCOMPARE(player.currentIndex(), 0);
    QCOMPARE(player.currentSong().title, QStringLiteral("A"));

    player.setMode(PlayerService::Shuffle);
    player.next();
    QVERIFY(player.currentIndex() >= 0 && player.currentIndex() <= 1);
}

void PlayerServiceTest::playPauseAndPosition()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString p1 = dir.filePath(QStringLiteral("a.wav"));
    writeWav(p1, 3);
    Song s1;
    s1.id = 1;
    s1.filePath = p1;
    s1.title = QStringLiteral("A");
    s1.durationMs = 3000;

    PlayerService player;
    player.setPlaylist({ s1 }, 0);
    player.play();

    const bool started = QTest::qWaitFor([&player] {
        return player.isPlaying();
    }, 4000);
    if (!started) {
        QSKIP("无可用音频输出设备,跳过播放状态验证");
    }
    QVERIFY(player.isPlaying());
    const bool advanced = QTest::qWaitFor([&player] {
        return player.position() >= 300;
    }, 3000);
    QVERIFY(advanced);
    player.pause();
    QVERIFY(!player.isPlaying());
    player.seek(1000);
    QVERIFY(player.position() >= 900);
}

QTEST_MAIN(PlayerServiceTest)
#include "tst_playerservice.moc"
