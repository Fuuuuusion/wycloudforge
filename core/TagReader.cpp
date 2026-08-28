#include "TagReader.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef NETECLONE_HAVE_TAGLIB
#include <taglib/attachedpictureframe.h>
#include <taglib/audioproperties.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/mp4file.h>
#include <taglib/mp4item.h>
#include <taglib/mp4tag.h>
#include <taglib/mpegfile.h>
#include <taglib/tag.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/xiphcomment.h>
#endif

namespace core {
namespace {

QString fromTagLibString(const TagLib::String &s)
{
    return QString::fromUtf8(s.to8Bit(true));
}

QString fallbackTitle(const QString &filePath)
{
    return QFileInfo(filePath).completeBaseName();
}

bool hasCjk(const QString &text)
{
    for (const QChar ch : text) {
        const ushort u = ch.unicode();
        if ((u >= 0x3400 && u <= 0x4DBF) || (u >= 0x4E00 && u <= 0x9FFF)
            || (u >= 0xF900 && u <= 0xFAFF))
            return true;
    }
    return false;
}

bool isValidUtf8Bytes(const QByteArray &data)
{
    int i = 0;
    while (i < data.size()) {
        const uchar c = uchar(data[i]);
        int extra = 0;
        if (c < 0x80) { ++i; continue; }
        if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else return false;
        if (i + extra >= data.size())
            return false;
        for (int k = 1; k <= extra; ++k)
            if ((uchar(data[i + k]) & 0xC0) != 0x80)
                return false;
        i += extra + 1;
    }
    return true;
}

QByteArray trimLegacyBytes(QByteArray bytes)
{
    const int nul = bytes.indexOf('\0');
    if (nul >= 0)
        bytes.truncate(nul);
    while (!bytes.isEmpty() && (bytes.endsWith(' ') || bytes.endsWith('\0')))
        bytes.chop(1);
    return bytes;
}

QString decodeLegacyBytes(const QByteArray &raw)
{
    const QByteArray bytes = trimLegacyBytes(raw);
    if (bytes.isEmpty())
        return {};

    if (isValidUtf8Bytes(bytes)) {
        const QString utf8 = QString::fromUtf8(bytes);
        bool ascii = true;
        for (const char ch : bytes)
            ascii = ascii && uchar(ch) < 0x80;
        if (ascii || hasCjk(utf8))
            return utf8;
    }

#ifdef Q_OS_WIN
    const int len = MultiByteToWideChar(936, MB_ERR_INVALID_CHARS,
                                        bytes.constData(), bytes.size(), nullptr, 0);
    if (len > 0) {
        std::wstring wide(len, L'\0');
        MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, bytes.constData(), bytes.size(),
                            wide.data(), len);
        const QString cp936 = QString::fromWCharArray(wide.data(), len);
        if (hasCjk(cp936))
            return cp936;
    }
#endif
    return QString::fromLatin1(bytes);
}

void applyId3v1(const QString &filePath, TagInfo &info)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 128 || !file.seek(file.size() - 128))
        return;
    const QByteArray tag = file.read(128);
    if (tag.size() != 128 || tag.left(3) != QByteArrayLiteral("TAG"))
        return;

    const QString title = decodeLegacyBytes(tag.mid(3, 30));
    const QString artist = decodeLegacyBytes(tag.mid(33, 30));
    const QString album = decodeLegacyBytes(tag.mid(63, 30));
    if (info.title.isEmpty())
        info.title = title;
    if (!artist.isEmpty() && (!hasCjk(info.artist) || info.artist.isEmpty()))
        info.artist = artist;
    if (info.album.isEmpty())
        info.album = album;
}

void applyFilenameFallback(const QString &filePath, TagInfo &info)
{
    const QString base = fallbackTitle(filePath);
    if (info.title.isEmpty()) {
        static const QRegularExpression separator(QStringLiteral("^(.+?)\\s*[-－–—]\\s*(.+)$"));
        const auto match = separator.match(base);
        if (match.hasMatch()) {
            if (info.artist.isEmpty())
                info.artist = match.captured(1).trimmed();
            info.title = match.captured(2).trimmed();
        }
    }
    if (info.title.isEmpty())
        info.title = base;
}

#ifndef NETECLONE_HAVE_TAGLIB
// ---------- 内置最小解析器(无 TagLib 时的回退) ----------

QString decodeId3Text(const QByteArray &data, int &pos, int encoding, int length)
{
    if (encoding == 0 || encoding == 3) {
        int end = data.indexOf('\0', pos);
        if (end < 0 || end >= pos + length)
            end = pos + length;
        QString s = encoding == 3 ? QString::fromUtf8(data.mid(pos, end - pos))
                                  : QString::fromLatin1(data.mid(pos, end - pos));
        pos = end + 1;
        return s;
    }
    // UTF-16 (BOM) / UTF-16BE
    QByteArray raw = data.mid(pos, length);
    int term = -1;
    for (int i = 0; i + 1 < raw.size(); ++i) {
        if (raw[i] == '\0' && raw[i + 1] == '\0') { term = i; break; }
    }
    if (term >= 0)
        raw = raw.left(term);
    QString s;
    if (encoding == 2)
        s = QString::fromUtf16(reinterpret_cast<const char16_t *>(raw.constData()), raw.size() / 2);
    else
        s = QString::fromUtf16(reinterpret_cast<const char16_t *>(raw.constData() + (raw.startsWith("\xFF\xFE") ? 2 : 0)),
                               (raw.size() - (raw.startsWith("\xFF\xFE") ? 2 : 0)) / 2);
    pos += length;
    return s;
}

quint32 syncsafeToInt(const char *p)
{
    return (quint32(p[0] & 0x7F) << 21) | (quint32(p[1] & 0x7F) << 14)
        | (quint32(p[2] & 0x7F) << 7) | quint32(p[3] & 0x7F);
}

TagInfo readMp3(const QByteArray &bytes)
{
    TagInfo info;
    if (!bytes.startsWith("ID3"))
        return info;
    const int major = bytes[3];
    const int flags = bytes[5];
    const int headerSize = syncsafeToInt(bytes.constData() + 6);
    int pos = 10;
    if ((flags & 0x40) && major == 4 && pos + 4 <= headerSize + 10) {
        pos += syncsafeToInt(bytes.constData() + pos) + 4;
    } else if ((flags & 0x40) && major == 3 && pos + 4 <= headerSize + 10) {
        quint32 ext = (quint32(uchar(bytes[pos])) << 24) | (quint32(uchar(bytes[pos + 1])) << 16)
            | (quint32(uchar(bytes[pos + 2])) << 8) | quint32(uchar(bytes[pos + 3]));
        pos += int(ext) + 4;
    }
    const int frameEnd = qMin(headerSize + 10, bytes.size());
    while (pos + 10 <= frameEnd) {
        const QByteArray id = bytes.mid(pos, 4);
        if (id.isEmpty() || id[0] == '\0')
            break;
        int size = 0;
        if (major == 4)
            size = int(syncsafeToInt(bytes.constData() + pos + 4));
        else
            size = (quint32(uchar(bytes[pos + 4])) << 24) | (quint32(uchar(bytes[pos + 5])) << 16)
                | (quint32(uchar(bytes[pos + 6])) << 8) | quint32(uchar(bytes[pos + 7]));
        pos += 10;
        if (size < 0 || pos + size > frameEnd) {
            pos += size; // 容错:跳过异常帧
            continue;
        }
        const QByteArray data = bytes.mid(pos, size);
        if (id == "TIT2") { int p = 0; info.title = decodeId3Text(data, p, data[0], size); }
        else if (id == "TPE1") { int p = 0; info.artist = decodeId3Text(data, p, data[0], size); }
        else if (id == "TALB") { int p = 0; info.album = decodeId3Text(data, p, data[0], size); }
        else if (id == "TLEN") { info.durationMs = QString::fromLatin1(data).toLongLong(); }
        else if (id == "USLT") {
            int p = 1; // 跳过编码字节
            p += 3;    // 语言
            int enc = data[0];
            int skip = (enc == 0 || enc == 3) ? data.indexOf('\0', p) : -1;
            if (skip < 0) {
                for (int i = p; i + 1 < data.size(); ++i)
                    if (data[i] == '\0' && data[i + 1] == '\0') { skip = i; break; }
            }
            if (skip >= 0) {
                p = skip + ((enc == 0 || enc == 3) ? 1 : 2);
                info.lyricsData = (enc == 0) ? data.mid(p).toUtf8()
                                             : (enc == 3 ? data.mid(p) : QString::fromUtf16(
                                                                               reinterpret_cast<const char16_t *>(data.constData() + p),
                                                                               (data.size() - p) / 2).toUtf8());
            }
        } else if (id == "APIC") {
            int enc = data[0];
            int p = 1;
            int mimeEnd = data.indexOf('\0', p);
            if (mimeEnd < 0) { pos += size; continue; }
            QString mime = QString::fromLatin1(data.mid(p, mimeEnd - p));
            p = mimeEnd + 1 + 1; // 跳过图片类型字节
            int descSkip = (enc == 0 || enc == 3) ? 1 : 2;
            int descEnd = -1;
            if (enc == 0 || enc == 3) {
                descEnd = data.indexOf('\0', p);
            } else {
                for (int i = p; i + 1 < data.size(); ++i)
                    if (data[i] == '\0' && data[i + 1] == '\0') { descEnd = i; break; }
            }
            if (descEnd >= 0)
                p = descEnd + descSkip;
            if (p < data.size())
                info.coverData = data.mid(p);
        }
        pos += size;
    }
    return info;
}

TagInfo readFlac(const QByteArray &bytes)
{
    TagInfo info;
    if (!bytes.startsWith("fLaC") || bytes.size() < 4 + 4 + 34)
        return info;
    int pos = 4;
    bool last = false;
    while (!last && pos + 4 <= bytes.size()) {
        const uchar header = uchar(bytes[pos]);
        last = header & 0x80;
        const int type = header & 0x7F;
        const int len = (quint32(uchar(bytes[pos + 1])) << 16) | (quint32(uchar(bytes[pos + 2])) << 8) | quint32(uchar(bytes[pos + 3]));
        pos += 4;
        if (pos + len > bytes.size())
            break;
        const QByteArray block = bytes.mid(pos, len);
        if (type == 0 && block.size() >= 34) { // STREAMINFO
            const quint32 rate = (quint32(uchar(block[10])) << 12) | (quint32(uchar(block[11])) << 4)
                | (quint32(uchar(block[12])) >> 4);
            quint64 total = (quint64(uchar(block[13]) & 0x0F) << 32)
                | (quint64(uchar(block[14])) << 24) | (quint64(uchar(block[15])) << 16)
                | (quint64(uchar(block[16])) << 8) | quint64(uchar(block[17]));
            if (rate > 0)
                info.durationMs = qint64(total * 1000 / rate);
        } else if (type == 4) { // VORBIS_COMMENT
            int p = 4;
            if (p + 4 > block.size()) { pos += len; continue; }
            quint32 vendorLen = (quint32(uchar(block[p])) | (quint32(uchar(block[p + 1])) << 8)
                | (quint32(uchar(block[p + 2])) << 16) | (quint32(uchar(block[p + 3])) << 24));
            p += 4 + vendorLen;
            if (p + 4 > block.size()) { pos += len; continue; }
            quint32 count = (quint32(uchar(block[p])) | (quint32(uchar(block[p + 1])) << 8)
                | (quint32(uchar(block[p + 2])) << 16) | (quint32(uchar(block[p + 3])) << 24));
            p += 4;
            for (quint32 i = 0; i < count && p + 4 <= block.size(); ++i) {
                quint32 clen = (quint32(uchar(block[p])) | (quint32(uchar(block[p + 1])) << 8)
                    | (quint32(uchar(block[p + 2])) << 16) | (quint32(uchar(block[p + 3])) << 24));
                p += 4;
                if (p + clen > block.size())
                    break;
                const QByteArray entry = block.mid(p, clen);
                p += clen;
                const int eq = entry.indexOf('=');
                if (eq < 0)
                    continue;
                const QString key = QString::fromUtf8(entry.left(eq)).toUpper();
                const QByteArray value = entry.mid(eq + 1);
                if (key == "TITLE") info.title = QString::fromUtf8(value);
                else if (key == "ARTIST") info.artist = QString::fromUtf8(value);
                else if (key == "ALBUM") info.album = QString::fromUtf8(value);
                else if (key == "LYRICS" || key == "UNSYNCEDLYRICS") info.lyricsData = value;
            }
        } else if (type == 6 && block.size() >= 8) { // PICTURE
            quint32 mimeLen = (quint32(uchar(block[4])) << 24) | (quint32(uchar(block[5])) << 16)
                | (quint32(uchar(block[6])) << 8) | quint32(uchar(block[7]));
            int p = 8 + int(mimeLen) + 4; // 类型 + mime + 描述长度
            if (p + 4 <= block.size()) {
                quint32 descLen = (quint32(uchar(block[p - 4])) << 24) | (quint32(uchar(block[p - 3])) << 16)
                    | (quint32(uchar(block[p - 2])) << 8) | quint32(uchar(block[p - 1]));
                p += int(descLen) + 16; // 描述 + 宽/高/深度/颜色
                if (p + 4 <= block.size()) {
                    quint32 dataLen = (quint32(uchar(block[p])) << 24) | (quint32(uchar(block[p + 1])) << 16)
                        | (quint32(uchar(block[p + 2])) << 8) | quint32(uchar(block[p + 3]));
                    p += 4;
                    if (p + int(dataLen) <= block.size())
                        info.coverData = block.mid(p, int(dataLen));
                }
            }
        }
        pos += len;
    }
    return info;
}
#endif

} // namespace

TagInfo TagReader::read(const QString &filePath)
{
    TagInfo info;

    // 网易云加密容器(.mgg/.mflac)不是 TagLib 可解析的音频,直接按文件名兜底,避免解析崩溃
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QLatin1String("mgg") || suffix == QLatin1String("mflac")) {
        applyFilenameFallback(filePath, info);
        return info;
    }

#ifdef NETECLONE_HAVE_TAGLIB
#ifdef _WIN32
    // TagLib 在 Windows 上用 Unicode(wchar_t)路径才能打开含中文/非ASCII的文件名，
    // 否则窄字符 fopen 无法解析中文路径，导致 FileRef 为空、元数据读不出来。
    const std::wstring widePath = filePath.toStdWString();
    const TagLib::FileName fileName(widePath.c_str());
#else
    const std::string narrowPath = filePath.toUtf8().toStdString();
    const TagLib::FileName fileName(narrowPath.c_str());
#endif
    TagLib::FileRef ref(fileName, true, TagLib::AudioProperties::Fast);
    if (!ref.isNull()) {
        if (auto *tag = ref.tag()) {
            info.title = fromTagLibString(tag->title());
            info.artist = fromTagLibString(tag->artist());
            info.album = fromTagLibString(tag->album());
        }
        if (auto *props = ref.audioProperties())
            info.durationMs = props->lengthInMilliseconds();

        if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(ref.file())) {
            if (auto *id3 = mpeg->ID3v2Tag()) {
                const auto pics = id3->frameList("APIC");
                if (!pics.isEmpty()) {
                    if (auto *pic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(pics.front()))
                        info.coverData = QByteArray(pic->picture().data(), int(pic->picture().size()));
                }
                const auto lyrs = id3->frameList("USLT");
                if (!lyrs.isEmpty()) {
                    if (auto *lyr = dynamic_cast<TagLib::ID3v2::UnsynchronizedLyricsFrame *>(lyrs.front()))
                        info.lyricsData = fromTagLibString(lyr->text()).toUtf8();
                }
            }
        } else if (auto *flac = dynamic_cast<TagLib::FLAC::File *>(ref.file())) {
            const auto pics = flac->pictureList();
            if (!pics.isEmpty())
                info.coverData = QByteArray(pics.front()->data().data(), int(pics.front()->data().size()));
            if (auto *xc = flac->xiphComment()) {
                const auto &map = xc->fieldListMap();
                if (map.contains("LYRICS"))
                    info.lyricsData = fromTagLibString(map["LYRICS"].front()).toUtf8();
                else if (map.contains("UNSYNCEDLYRICS"))
                    info.lyricsData = fromTagLibString(map["UNSYNCEDLYRICS"].front()).toUtf8();
            }
        } else if (auto *mp4 = dynamic_cast<TagLib::MP4::File *>(ref.file())) {
            if (auto *tag = mp4->tag()) {
                if (tag->contains("covr")) {
                    const auto covers = tag->item("covr").toCoverArtList();
                    if (!covers.isEmpty())
                        info.coverData = QByteArray(covers.front().data().data(), int(covers.front().data().size()));
                }
            }
        }
    }
#else
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return info;
    const QByteArray head = file.read(qMin<qint64>(file.size(), 4 * 1024 * 1024));
    if (head.startsWith("ID3"))
        info = readMp3(head);
    else if (head.startsWith("fLaC"))
        info = readFlac(head);
#endif

    // 很多旧 MP3 只有 ID3v1，中文字段按 GBK 写入；TagLib 会把它们当作
    // Latin-1 字符返回，因此在通用标签读取后用原始字节补一次正确解码。
    applyId3v1(filePath, info);
    applyFilenameFallback(filePath, info);
    return info;
}

} // namespace core
