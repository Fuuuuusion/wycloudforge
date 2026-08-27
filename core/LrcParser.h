#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace core {

struct LyricLine
{
    qint64 timeMs = 0;
    QString text;
};

class LrcParser
{
public:
    // 编码自动识别:UTF-8 BOM / UTF-16 LE/BE / 严格 UTF-8 / GBK(Win32)
    static bool decodeText(const QByteArray &raw, QString &out);

    static QList<LyricLine> parse(const QString &text, qint64 *offsetMs = nullptr);
    static QList<LyricLine> parseBytes(const QByteArray &raw);

    static QString formatTime(qint64 ms); // mm:ss.xx
    static QString toLrc(const QList<LyricLine> &lines);
};

} // namespace core

