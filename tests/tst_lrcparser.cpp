#include "core/LrcParser.h"

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

QTEST_MAIN(LrcParserTest)
#include "tst_lrcparser.moc"
