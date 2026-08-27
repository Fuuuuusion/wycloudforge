#include "LrcParser.h"

#include <QRegularExpression>
#include <QVarLengthArray>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace core {
namespace {

bool isValidUtf8(const QByteArray &data)
{
    int i = 0;
    const int n = data.size();
    while (i < n) {
        const uchar c = uchar(data[i]);
        int extra = 0;
        if (c < 0x80) { ++i; continue; }
        if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else return false;
        if (i + extra >= n)
            return false;
        for (int k = 1; k <= extra; ++k) {
            if ((uchar(data[i + k]) & 0xC0) != 0x80)
                return false;
        }
        i += extra + 1;
    }
    return true;
}

} // namespace

bool LrcParser::decodeText(const QByteArray &raw, QString &out)
{
    if (raw.startsWith("\xEF\xBB\xBF")) {
        out = QString::fromUtf8(raw.mid(3));
        return true;
    }
    if (raw.startsWith("\xFF\xFE")) {
        out = QString::fromUtf16(reinterpret_cast<const char16_t *>(raw.constData() + 2), (raw.size() - 2) / 2);
        return true;
    }
    if (raw.startsWith("\xFE\xFF")) {
        QByteArray be = raw.mid(2);
        for (int i = 0; i + 1 < be.size(); i += 2)
            std::swap(be[i], be[i + 1]);
        out = QString::fromUtf16(reinterpret_cast<const char16_t *>(be.constData()), be.size() / 2);
        return true;
    }
    if (isValidUtf8(raw)) {
        out = QString::fromUtf8(raw);
        return true;
    }
#ifdef Q_OS_WIN
    const int len = MultiByteToWideChar(936, 0, raw.constData(), raw.size(), nullptr, 0);
    if (len > 0) {
        QVarLengthArray<wchar_t, 1024> buf(len);
        MultiByteToWideChar(936, 0, raw.constData(), raw.size(), buf.data(), len);
        out = QString::fromWCharArray(buf.constData(), len);
        return true;
    }
#endif
    out = QString::fromLatin1(raw);
    return true;
}

QList<LyricLine> LrcParser::parse(const QString &text, qint64 *offsetMs)
{
    QList<LyricLine> lines;
    qint64 offset = 0;
    const QStringList rawLines = text.split(QLatin1Char('\n'));
    static const QRegularExpression tagRe(QStringLiteral("\\[(\\d+):(\\d{1,2})(?:\\.(\\d{1,3}))?\\]"));
    static const QRegularExpression offsetRe(QStringLiteral("\\[offset:([+-]?\\d+)\\]"));

    for (QString line : rawLines) {
        line = line.trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            const auto offsetMatch = offsetRe.match(line);
            if (offsetMatch.hasMatch()) {
                offset = offsetMatch.captured(1).toLongLong();
                continue;
            }
            if (line.startsWith(QStringLiteral("[ti:")) || line.startsWith(QStringLiteral("[ar:"))
                || line.startsWith(QStringLiteral("[al:")) || line.startsWith(QStringLiteral("[by:"))
                || line.startsWith(QStringLiteral("[re:")) || line.startsWith(QStringLiteral("[ve:")))
                continue;
        }

        int lastTagEnd = -1;
        QList<qint64> times;
        auto it = tagRe.globalMatch(line);
        while (it.hasNext()) {
            const auto m = it.next();
            const int minutes = m.captured(1).toInt();
            const int seconds = m.captured(2).toInt();
            QString frac = m.captured(3);
            if (frac.size() < 3)
                frac += QString(3 - frac.size(), QLatin1Char('0'));
            const qint64 ms = qint64(minutes) * 60000 + qint64(seconds) * 1000 + frac.left(3).toInt();
            times.append(ms);
            lastTagEnd = m.capturedEnd();
        }
        if (times.isEmpty())
            continue;
        const QString textPart = line.mid(lastTagEnd).trimmed();
        for (qint64 t : times) {
            LyricLine l;
            l.timeMs = t;
            l.text = textPart;
            lines.append(l);
        }
    }

    if (offsetMs)
        *offsetMs = offset;
    for (LyricLine &l : lines)
        l.timeMs += offset;

    std::stable_sort(lines.begin(), lines.end(), [](const LyricLine &a, const LyricLine &b) {
        return a.timeMs < b.timeMs;
    });
    return lines;
}

QList<LyricLine> LrcParser::parseBytes(const QByteArray &raw)
{
    QString text;
    decodeText(raw, text);
    return parse(text);
}

QString LrcParser::formatTime(qint64 ms)
{
    if (ms < 0)
        ms = 0;
    const qint64 totalMs = ms % 1000;
    const qint64 totalSec = ms / 1000;
    return QStringLiteral("%1:%2.%3")
        .arg(totalSec / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSec % 60, 2, 10, QLatin1Char('0'))
        .arg(totalMs, 2, 10, QLatin1Char('0'));
}

QString LrcParser::toLrc(const QList<LyricLine> &lines)
{
    QString out;
    for (const LyricLine &l : lines)
        out += QStringLiteral("[%1]%2\n").arg(formatTime(l.timeMs), l.text);
    return out;
}

} // namespace core
