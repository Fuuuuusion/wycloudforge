#include "core/LrcParser.h"
#include "core/LyricsLoader.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace core;

class LrcParserTest : public QObject
{
    Q_OBJECT
private slots:
    void parseBasicLines();
    void parseMultipleTags();
    void parseOffset();
    void decodeUtf8Bom();
    void decodeUtf16();
    void decodeGbk();
    void malformedLinesIgnored();
    void roundTrip();
    void loadDownloadedOnlineSidecar();
    void explicitLyricPathTakesPriority();
    void missingOnlineSidecarIsSafe();
};

void LrcParserTest::parseBasicLines()
{
    const QString text = QStringLiteral("[ti:测试]\n[00:01.50]第一句\n[00:05.00]第二句\n");
    qint64 offset = 0;
    const auto lines = LrcParser::parse(text, &offset);
    QCOMPARE(offset, qint64(0));
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines[0].timeMs, qint64(1500));
    QCOMPARE(lines[0].text, QStringLiteral("第一句"));
    QCOMPARE(lines[1].timeMs, qint64(5000));
    QCOMPARE(lines[1].text, QStringLiteral("第二句"));
}

void LrcParserTest::parseMultipleTags()
{
    const auto lines = LrcParser::parse(QStringLiteral("[00:01.00][00:02.00]同一句\n"));
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines[0].timeMs, qint64(1000));
    QCOMPARE(lines[1].timeMs, qint64(2000));
    QCOMPARE(lines[0].text, QStringLiteral("同一句"));
}

void LrcParserTest::parseOffset()
{
    const QString text = QStringLiteral("[offset:500]\n[00:01.00]歌词\n");
    qint64 offset = 0;
    const auto lines = LrcParser::parse(text, &offset);
    QCOMPARE(offset, qint64(500));
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines[0].timeMs, qint64(1500));
}

void LrcParserTest::decodeUtf8Bom()
{
    QByteArray raw;
    raw.append("\xEF\xBB\xBF");
    raw.append("[00:01.00]中文歌词\n");
    QString out;
    QVERIFY(LrcParser::decodeText(raw, out));
    QVERIFY(out.contains(QStringLiteral("中文歌词")));
}

void LrcParserTest::decodeUtf16()
{
    const QString expected = QStringLiteral("[00:01.00]歌词\n");
    QByteArray raw = QByteArrayLiteral("\xFF\xFE");
    const std::u16string u16 = expected.toStdU16String();
    raw.append(reinterpret_cast<const char *>(u16.data()), int(u16.size() * 2));
    QString out;
    QVERIFY(LrcParser::decodeText(raw, out));
    QCOMPARE(out, expected);
}

void LrcParserTest::decodeGbk()
{
    // "[00:01.00]你好" 的 GBK 编码
    QByteArray raw;
    raw.append("[00:01.00]");
    raw.append(char(0xC4));
    raw.append(char(0xE3));
    raw.append(char(0xBA));
    raw.append(char(0xC3));
    raw.append('\n');
    QString out;
    QVERIFY(LrcParser::decodeText(raw, out));
    QVERIFY(out.contains(QStringLiteral("你好")));
}

void LrcParserTest::malformedLinesIgnored()
{
    const auto lines = LrcParser::parse(QStringLiteral("没有时间标签\n[00:0x.00]坏时间\n[abc]坏标签\n[00:03.00]正常\n"));
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines[0].timeMs, qint64(3000));
}

void LrcParserTest::roundTrip()
{
    const auto lines = LrcParser::parse(QStringLiteral("[00:01.50]第一句\n[00:05.00]第二句\n"));
    const QString lrc = LrcParser::toLrc(lines);
    const auto reparsed = LrcParser::parse(lrc);
    QCOMPARE(reparsed.size(), 2);
    QCOMPARE(reparsed[0].timeMs, qint64(1500));
    QCOMPARE(reparsed[1].text, QStringLiteral("第二句"));
}

void LrcParserTest::loadDownloadedOnlineSidecar()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString audioPath = dir.filePath(QStringLiteral("downloaded.mp3"));
    QFile audio(audioPath);
    QVERIFY(audio.open(QIODevice::WriteOnly));
    QVERIFY(audio.write("audio") > 0);
    audio.close();

    const QString lyricPath = LyricsLoader::sidecarPathFor(audioPath);
    QFile lyric(lyricPath);
    QVERIFY(lyric.open(QIODevice::WriteOnly));
    QVERIFY(lyric.write("[00:01.00]\xe4\xb8\x8b\xe8\xbd\xbd\xe6\xad\x8c\xe8\xaf\x8d\n") > 0);
    lyric.close();

    Song song;
    song.source = 1;
    song.onlineId = 1001;
    song.filePath = QStringLiteral("netease://1001");
    song.downloadPath = audioPath;
    QCOMPARE(QDir::cleanPath(LyricsLoader::existingSidecarPathFor(song)), QDir::cleanPath(lyricPath));
    const auto lines = LyricsLoader::load(song);
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines.first().text, QStringLiteral("下载歌词"));
}

void LrcParserTest::explicitLyricPathTakesPriority()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString audioPath = dir.filePath(QStringLiteral("cached.mp3"));
    QFile audio(audioPath);
    QVERIFY(audio.open(QIODevice::WriteOnly));
    QVERIFY(audio.write("audio") > 0);
    audio.close();

    QFile adjacent(LyricsLoader::sidecarPathFor(audioPath));
    QVERIFY(adjacent.open(QIODevice::WriteOnly));
    QVERIFY(adjacent.write("[00:01.00]adjacent\n") > 0);
    adjacent.close();
    const QString explicitPath = dir.filePath(QStringLiteral("preferred.lrc"));
    QFile explicitLyric(explicitPath);
    QVERIFY(explicitLyric.open(QIODevice::WriteOnly));
    QVERIFY(explicitLyric.write("[00:02.00]preferred\n") > 0);
    explicitLyric.close();

    Song song;
    song.source = 1;
    song.onlineId = 1002;
    song.filePath = QStringLiteral("netease://1002");
    song.cachePath = audioPath;
    song.lyricPath = explicitPath;
    const auto lines = LyricsLoader::load(song);
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines.first().timeMs, qint64(2000));
    QCOMPARE(lines.first().text, QStringLiteral("preferred"));
}

void LrcParserTest::missingOnlineSidecarIsSafe()
{
    Song song;
    song.source = 1;
    song.onlineId = 1003;
    song.filePath = QStringLiteral("netease://1003");
    song.downloadPath = QStringLiteral("Z:/missing/downloaded.mp3");
    QVERIFY(LyricsLoader::existingSidecarPathFor(song).isEmpty());
    QVERIFY(LyricsLoader::load(song).isEmpty());
    QVERIFY(LyricsLoader::writableSidecarPathFor(song).isEmpty());
}

QTEST_MAIN(LrcParserTest)
#include "tst_lrcparser.moc"
