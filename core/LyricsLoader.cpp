#include "LyricsLoader.h"

#include "core/TagReader.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>

namespace {

QString normalizedAbsolutePath(const QString &path)
{
    if (path.isEmpty())
        return {};
    return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
}

bool isUsableFile(const QString &path)
{
    const QFileInfo info(path);
    return info.isFile() && info.size() > 0;
}

QString lyricCacheKey(const QString &path)
{
    return QStringLiteral("lrc:") + normalizedAbsolutePath(path);
}

QString audioCacheKey(const QString &path)
{
    return QStringLiteral("audio:") + normalizedAbsolutePath(path);
}

} // namespace

namespace core {

QHash<QString, QList<LyricLine>> LyricsLoader::s_cache;

QString LyricsLoader::sidecarPathFor(const QString &musicPath)
{
    if (musicPath.isEmpty())
        return {};
    QFileInfo fi(musicPath);
    return fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".lrc");
}

QString LyricsLoader::existingSidecarPathFor(const Song &song)
{
    QStringList candidates;
    if (!song.lyricPath.isEmpty())
        candidates.append(song.lyricPath);
    if (!song.downloadPath.isEmpty())
        candidates.append(sidecarPathFor(song.downloadPath));
    if (!song.cachePath.isEmpty())
        candidates.append(sidecarPathFor(song.cachePath));
    if (!song.isOnline() && !song.filePath.isEmpty())
        candidates.append(sidecarPathFor(song.filePath));

    for (const QString &candidate : candidates) {
        if (isUsableFile(candidate))
            return normalizedAbsolutePath(candidate);
    }
    return {};
}

QString LyricsLoader::writableSidecarPathFor(const Song &song)
{
    if (!song.lyricPath.isEmpty())
        return normalizedAbsolutePath(song.lyricPath);
    if (song.isDownloaded())
        return normalizedAbsolutePath(sidecarPathFor(song.downloadPath));
    if (song.isCached())
        return normalizedAbsolutePath(sidecarPathFor(song.cachePath));
    if (!song.isOnline() && !song.filePath.isEmpty())
        return normalizedAbsolutePath(sidecarPathFor(song.filePath));
    return {};
}

QList<LyricLine> LyricsLoader::load(const Song &song)
{
    const QString sidecar = existingSidecarPathFor(song);
    const QString cacheKey = !sidecar.isEmpty() ? lyricCacheKey(sidecar)
                                                : audioCacheKey(song.filePath);
    if (cacheKey.endsWith(QLatin1Char(':')))
        return {};
    const auto it = s_cache.constFind(cacheKey);
    if (it != s_cache.constEnd())
        return it.value();

    QList<LyricLine> result;
    if (!sidecar.isEmpty()) {
        QFile f(sidecar);
        if (f.open(QIODevice::ReadOnly))
            result = LrcParser::parseBytes(f.readAll());
    } else if (!song.isOnline() && isUsableFile(song.filePath)) {
        const TagInfo info = TagReader::read(song.filePath);
        if (info.hasLyrics())
            result = LrcParser::parseBytes(info.lyricsData);
    }
    s_cache.insert(cacheKey, result);
    return result;
}

bool LyricsLoader::saveSidecar(const Song &song, const QString &lrcText)
{
    const QString path = writableSidecarPathFor(song);
    if (path.isEmpty())
        return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(lrcText.toUtf8());
    f.close();
    s_cache.insert(lyricCacheKey(path), LrcParser::parse(lrcText));
    return true;
}

void LyricsLoader::invalidate(const QString &musicPath)
{
    s_cache.remove(audioCacheKey(musicPath));
    s_cache.remove(lyricCacheKey(musicPath));
    s_cache.remove(lyricCacheKey(sidecarPathFor(musicPath)));
}

} // namespace core
