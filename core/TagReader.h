#pragma once

#include <QByteArray>
#include <QString>

namespace core {

struct TagInfo
{
    QString title;
    QString artist;
    QString album;
    qint64 durationMs = 0;
    QByteArray coverData;   // 内嵌封面原始数据
    QByteArray lyricsData;  // 内嵌歌词(UTF-8)

    bool hasCover() const { return !coverData.isEmpty(); }
    bool hasLyrics() const { return !lyricsData.isEmpty(); }
};

class TagReader
{
public:
    static TagInfo read(const QString &filePath);
};

} // namespace core

