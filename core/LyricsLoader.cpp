#include "LyricsLoader.h"

#include "core/TagReader.h"

#include <QFile>
#include <QFileInfo>

namespace core {

QHash<QString, QList<LyricLine>> LyricsLoader::s_cache;

QString LyricsLoader::sidecarPathFor(const QString &musicPath)
{
    QFileInfo fi(musicPath);
    return fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".lrc");
}

QList<LyricLine> LyricsLoader::load(const Song &song)
{
    if (song.filePath.isEmpty())
        return {};
    const auto it = s_cache.constFind(song.filePath);
    if (it != s_cache.constEnd())
        return it.value();

    QList<LyricLine> result;
    const QString sidecar = sidecarPathFor(song.filePath);
    if (QFile::exists(sidecar)) {
        QFile f(sidecar);
        if (f.open(QIODevice::ReadOnly))
            result = LrcParser::parseBytes(f.readAll());
    } else {
        const TagInfo info = TagReader::read(song.filePath);
        if (info.hasLyrics())
            result = LrcParser::parseBytes(info.lyricsData);
    }
    s_cache.insert(song.filePath, result);
    return result;
}

bool LyricsLoader::saveSidecar(const Song &song, const QString &lrcText)
{
    const QString path = sidecarPathFor(song.filePath);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(lrcText.toUtf8());
    f.close();
    s_cache.insert(song.filePath, LrcParser::parse(lrcText));
    return true;
}

void LyricsLoader::invalidate(const QString &musicPath)
{
    s_cache.remove(musicPath);
}

} // namespace core

