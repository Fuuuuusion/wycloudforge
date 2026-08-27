#include "core/TagReader.h"

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

void appendFrame(QByteArray &out, const QByteArray &id, const QByteArray &data)
{
    out.append(id);
    out.append(char((data.size() >> 24) & 0xFF));
    out.append(char((data.size() >> 16) & 0xFF));
    out.append(char((data.size() >> 8) & 0xFF));
    out.append(char(data.size() & 0xFF));
    out.append(char(0));
    out.append(char(0));
    out.append(data);
}

QByteArray makeMp3WithId3()
{
    QByteArray out("ID3");
    out.append(char(3)); // ID3v2.3
    out.append(char(0));
    out.append(char(0));

    QByteArray frames;
    appendFrame(frames, "TIT2", QByteArrayLiteral("\x03") + QStringLiteral("测试歌曲").toUtf8());
    appendFrame(frames, "TPE1", QByteArrayLiteral("\x03") + QStringLiteral("周杰伦").toUtf8());
    appendFrame(frames, "TALB", QByteArrayLiteral("\x03") + QStringLiteral("叶惠美").toUtf8());
    appendFrame(frames, "TLEN", QByteArrayLiteral("180000"));
    QByteArray apic;
    apic.append(char(0));
    apic.append("image/jpeg");
    apic.append(char(0));
    apic.append(char(3));
    apic.append(char(0));
    apic.append(QByteArrayLiteral("\xFF\xD8\xFF\xE0") + QByteArrayLiteral("fakejpeg"));
    appendFrame(frames, "APIC", apic);
    QByteArray uslt;
    uslt.append(char(3));
    uslt.append("chi");
    uslt.append(char(0));
    uslt.append(QStringLiteral("第一行\n第二行").toUtf8());
    appendFrame(frames, "USLT", uslt);

    const quint32 total = quint32(frames.size());
    out.append(char((total >> 21) & 0x7F));
    out.append(char((total >> 14) & 0x7F));
    out.append(char((total >> 7) & 0x7F));
    out.append(char(total & 0x7F));
    out.append(frames);

    // 一个最小可识别的 MPEG1 Layer3 帧头 + 填充
    out.append(QByteArrayLiteral("\xFF\xFB\x90\x64"));
    out.append(QByteArray(413, '\0'));
    return out;
}

QByteArray makeFlac()
{
    QByteArray out("fLaC");

    QByteArray streamInfo(34, '\0');
    streamInfo[10] = char(0x0A); // sample rate 44100
    streamInfo[11] = char(0xC4);
    streamInfo[12] = char(0x42); // 声道=2
    streamInfo[13] = char(0x10); // 位深=16
    streamInfo[15] = char(0x06); // total samples 441000 (10s)
    streamInfo[16] = char(0xBA);
    streamInfo[17] = char(0xB8);
    out.append(char(0x00)); // STREAMINFO,非最后一块
    out.append(char(0));
    out.append(char(0));
    out.append(char(34));
    out.append(streamInfo);

    QByteArray comments;
    comments.append(QByteArrayLiteral("\x00\x00\x00\x00")); // vendor len
    comments.append(QByteArrayLiteral("\x04\x00\x00\x00")); // 4 条
    auto addComment = [&comments](const QByteArray &key, const QString &value) {
        const QByteArray entry = key + '=' + value.toUtf8();
        comments.append(char(entry.size() & 0xFF));
        comments.append(char((entry.size() >> 8) & 0xFF));
        comments.append(char((entry.size() >> 16) & 0xFF));
        comments.append(char((entry.size() >> 24) & 0xFF));
        comments.append(entry);
    };
    addComment("TITLE", QStringLiteral("测试FLAC"));
    addComment("ARTIST", QStringLiteral("歌手"));
    addComment("ALBUM", QStringLiteral("专辑"));
    addComment("LYRICS", QStringLiteral("歌词行"));
    out.append(char(0x04)); // VORBIS_COMMENT,非最后一块
    out.append(char((comments.size() >> 16) & 0xFF));
    out.append(char((comments.size() >> 8) & 0xFF));
    out.append(char(comments.size() & 0xFF));
    out.append(comments);

    QByteArray picture;
    picture.append(QByteArrayLiteral("\x00\x00\x00\x03")); // 封面类型
    picture.append(QByteArrayLiteral("\x00\x00\x00\x09") + QByteArrayLiteral("image/png"));
    picture.append(QByteArrayLiteral("\x00\x00\x00\x00")); // 描述长度
    picture.append(QByteArrayLiteral("\x00\x00\x00\x00")); // 宽
    picture.append(QByteArrayLiteral("\x00\x00\x00\x00")); // 高
    picture.append(QByteArrayLiteral("\x00\x00\x00\x18")); // 深度
    picture.append(QByteArrayLiteral("\x00\x00\x00\x00")); // 颜色数
    picture.append(QByteArrayLiteral("\x00\x00\x00\x08") + QByteArrayLiteral("\x89PNG\r\n\x1A\n"));
    out.append(char(0x86)); // PICTURE,最后一块
    out.append(char((picture.size() >> 16) & 0xFF));
    out.append(char((picture.size() >> 8) & 0xFF));
    out.append(char(picture.size() & 0xFF));
    out.append(picture);
    return out;
}

} // namespace

class TagReaderTest : public QObject
{
    Q_OBJECT
private slots:
    void readsMp3Tags();
    void readsFlacTags();
    void fallsBackToFileName();
};

void TagReaderTest::readsMp3Tags()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("test.mp3"));
    QVERIFY(writeFile(path, makeMp3WithId3()));

    const TagInfo info = TagReader::read(path);
    QCOMPARE(info.title, QStringLiteral("测试歌曲"));
    QCOMPARE(info.artist, QStringLiteral("周杰伦"));
    QCOMPARE(info.album, QStringLiteral("叶惠美"));
    QVERIFY(info.hasCover());
    QVERIFY(info.lyricsData.contains("第一行"));
    Q_UNUSED(info.durationMs); // 合成音频帧时长可能为 0,不做断言
}

void TagReaderTest::readsFlacTags()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("test.flac"));
    QVERIFY(writeFile(path, makeFlac()));

    const TagInfo info = TagReader::read(path);
    QCOMPARE(info.title, QStringLiteral("测试FLAC"));
    QCOMPARE(info.artist, QStringLiteral("歌手"));
    QCOMPARE(info.album, QStringLiteral("专辑"));
    QVERIFY(info.hasCover());
    QVERIFY(info.lyricsData.contains("歌词行"));
    QVERIFY(info.durationMs > 0);
}

void TagReaderTest::fallsBackToFileName()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("无名曲.wav"));
    QVERIFY(writeFile(path, QByteArray(100, '\0')));

    const TagInfo info = TagReader::read(path);
    QCOMPARE(info.title, QStringLiteral("无名曲"));
}

QTEST_MAIN(TagReaderTest)
#include "tst_tagreader.moc"
