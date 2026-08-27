#include "SearchService.h"

#include <QHash>
#include <QMap>

namespace core {
namespace {

int scoreField(const QString &field, const QString &query, int prefixScore, int containsScore)
{
    if (field.isEmpty() || query.isEmpty())
        return 0;
    if (field.startsWith(query, Qt::CaseInsensitive))
        return prefixScore;
    if (field.contains(query, Qt::CaseInsensitive))
        return containsScore;
    return 0;
}

} // namespace

QList<SearchService::Result> SearchService::search(const QList<Song> &songs, const QString &query)
{
    QList<Result> results;
    const QString q = query.trimmed();
    if (q.isEmpty())
        return results;
    for (int i = 0; i < songs.size(); ++i) {
        const Song &s = songs[i];
        int score = scoreField(s.title, q, 100, 60);
        score += scoreField(s.artist, q, 90, 50);
        score += scoreField(s.album, q, 80, 40);
        if (score > 0)
            results.append({ i, score });
    }
    std::sort(results.begin(), results.end(), [](const Result &a, const Result &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.index < b.index;
    });
    return results;
}

QList<QPair<int, int>> SearchService::highlightRanges(const QString &text, const QString &query)
{
    QList<QPair<int, int>> ranges;
    const QString q = query.trimmed();
    if (q.isEmpty())
        return ranges;
    const int idx = text.indexOf(q, 0, Qt::CaseInsensitive);
    if (idx >= 0)
        ranges.append({ idx, idx + q.size() });
    return ranges;
}

QList<ArtistGroup> SearchService::artists(const QList<Song> &songs)
{
    QMap<QString, int> counts;
    for (const Song &s : songs) {
        if (!s.artist.isEmpty())
            ++counts[s.artist];
    }
    QList<ArtistGroup> groups;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        groups.append({ it.key(), it.value() });
    std::sort(groups.begin(), groups.end(), [](const ArtistGroup &a, const ArtistGroup &b) {
        return a.count > b.count;
    });
    return groups;
}

QList<AlbumGroup> SearchService::albums(const QList<Song> &songs)
{
    struct Key
    {
        QString name;
        QString artist;
        bool operator<(const Key &o) const { return name < o.name; }
    };
    QMap<Key, int> counts;
    for (const Song &s : songs) {
        if (!s.album.isEmpty())
            ++counts[{ s.album, s.artist }];
    }
    QList<AlbumGroup> groups;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        groups.append({ it.key().name, it.key().artist, it.value() });
    return groups;
}

} // namespace core

