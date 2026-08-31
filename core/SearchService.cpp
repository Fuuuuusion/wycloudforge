#include "SearchService.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QMap>
#include <QMutexLocker>
#include <QRunnable>
#include <QStringList>

#include <algorithm>
#include <functional>

namespace core {
namespace {

QString normalizedText(const QString &text)
{
    return text.normalized(QString::NormalizationForm_KC).toCaseFolded().simplified();
}

int scoreField(const QString &field, const QString &query,
               int exactScore, int prefixScore, int containsScore)
{
    if (field.isEmpty() || query.isEmpty())
        return 0;
    if (field == query)
        return exactScore;
    if (field.startsWith(query))
        return prefixScore;
    if (field.contains(query))
        return containsScore;
    return 0;
}

bool artistTitleCombinationMatches(const QString &title, const QString &artist,
                                   const QString &query)
{
    if (title.isEmpty() || artist.isEmpty() || query.isEmpty())
        return false;
    const bool crossesFieldBoundary = !title.contains(query) && !artist.contains(query)
        && ((artist + title).contains(query) || (title + artist).contains(query)
            || (artist + QLatin1Char(' ') + title).contains(query)
            || (title + QLatin1Char(' ') + artist).contains(query));
    if (crossesFieldBoundary) {
        return true;
    }
    const QStringList tokens = query.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.size() < 2)
        return false;
    bool titleMatched = false;
    bool artistMatched = false;
    for (const QString &token : tokens) {
        const bool inTitle = title.contains(token);
        const bool inArtist = artist.contains(token);
        if (!inTitle && !inArtist)
            return false;
        titleMatched = titleMatched || inTitle;
        artistMatched = artistMatched || inArtist;
    }
    return titleMatched && artistMatched;
}

bool betterResult(const SearchService::Result &left, const SearchService::Result &right)
{
    if (left.score != right.score)
        return left.score > right.score;
    return left.index < right.index;
}

SearchService::Batch searchSongs(const QList<Song> &songs, const QString &query,
                                 int maxResults,
                                 const std::function<bool()> &cancelled = {})
{
    SearchService::Batch batch;
    const QString q = normalizedText(query);
    if (q.isEmpty())
        return batch;
    const int trimThreshold = maxResults > 0 ? qMax(maxResults * 2, maxResults + 1) : -1;
    for (int i = 0; i < songs.size(); ++i) {
        if ((i & 63) == 0 && cancelled && cancelled())
            return {};
        const Song &song = songs[i];
        const QString title = normalizedText(song.title);
        const QString artist = normalizedText(song.artist);
        const QString album = normalizedText(song.album);
        int score = scoreField(title, q, 1000, 900, 800);
        if (artistTitleCombinationMatches(title, artist, q))
            score = qMax(score, 750);
        score = qMax(score, scoreField(artist, q, 700, 650, 600));
        score = qMax(score, scoreField(album, q, 550, 500, 450));
        if (score <= 0)
            continue;
        ++batch.totalMatches;
        batch.results.append({ i, score });
        if (trimThreshold > 0 && batch.results.size() >= trimThreshold) {
            std::nth_element(batch.results.begin(), batch.results.begin() + maxResults,
                             batch.results.end(), betterResult);
            batch.results.resize(maxResults);
        }
    }
    std::sort(batch.results.begin(), batch.results.end(), betterResult);
    if (maxResults > 0 && batch.results.size() > maxResults)
        batch.results.resize(maxResults);
    return batch;
}

} // namespace

SearchService::SearchService(QObject *parent)
    : QObject(parent)
    , m_snapshot(std::make_shared<const QList<Song>>())
{
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(10000);
}

SearchService::~SearchService()
{
    m_stopping.store(true, std::memory_order_release);
    m_generation.fetch_add(1, std::memory_order_acq_rel);
    m_pool.clear();
    m_pool.waitForDone();
}

void SearchService::updateSnapshot(const QList<Song> &songs)
{
    auto snapshot = std::make_shared<const QList<Song>>(songs);
    {
        QMutexLocker locker(&m_snapshotMutex);
        m_snapshot = std::move(snapshot);
    }
    cancelAsync();
}

quint64 SearchService::searchAsync(const QString &query, int maxResults)
{
    const QString normalizedQuery = query.trimmed();
    const quint64 generation = m_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    m_pool.clear();
    std::shared_ptr<const QList<Song>> snapshot;
    {
        QMutexLocker locker(&m_snapshotMutex);
        snapshot = m_snapshot;
    }
    auto *task = QRunnable::create([this, snapshot = std::move(snapshot), normalizedQuery,
                                    maxResults, generation] {
        const Batch batch = searchSongs(*snapshot, normalizedQuery, maxResults, [this, generation] {
            return m_stopping.load(std::memory_order_acquire)
                || m_generation.load(std::memory_order_acquire) != generation;
        });
        if (m_stopping.load(std::memory_order_acquire)
            || m_generation.load(std::memory_order_acquire) != generation) {
            return;
        }
        QList<Song> matches;
        matches.reserve(batch.results.size());
        for (const Result &result : batch.results)
            matches.append(snapshot->at(result.index));
        QMetaObject::invokeMethod(this,
                                  [this, generation, normalizedQuery,
                                   matches = std::move(matches), total = batch.totalMatches] {
            if (m_stopping.load(std::memory_order_acquire)
                || m_generation.load(std::memory_order_acquire) != generation) {
                return;
            }
            emit searchFinished(generation, normalizedQuery, matches, total);
        }, Qt::QueuedConnection);
    });
    m_pool.start(task);
    return generation;
}

void SearchService::cancelAsync()
{
    m_generation.fetch_add(1, std::memory_order_acq_rel);
    m_pool.clear();
}

QList<SearchService::Result> SearchService::search(const QList<Song> &songs,
                                                   const QString &query,
                                                   int maxResults)
{
    return searchBatch(songs, query, maxResults).results;
}

SearchService::Batch SearchService::searchBatch(const QList<Song> &songs,
                                                const QString &query,
                                                int maxResults)
{
    return searchSongs(songs, query, maxResults);
}

QList<QPair<int, int>> SearchService::highlightRanges(const QString &text, const QString &query)
{
    QList<QPair<int, int>> ranges;
    const QString q = query.trimmed();
    if (q.isEmpty())
        return ranges;
    int from = 0;
    while (from <= text.size() - q.size()) {
        const int index = text.indexOf(q, from, Qt::CaseInsensitive);
        if (index < 0)
            break;
        ranges.append({ index, index + q.size() });
        from = index + q.size();
    }
    return ranges;
}

QList<ArtistGroup> SearchService::artists(const QList<Song> &songs)
{
    QMap<QString, int> counts;
    QMap<QString, QString> covers;
    for (const Song &s : songs) {
        if (!s.artist.isEmpty()) {
            ++counts[s.artist];
            if (!covers.contains(s.artist) && !s.coverPath.isEmpty() && QFileInfo::exists(s.coverPath))
                covers.insert(s.artist, s.coverPath);
        }
    }
    QList<ArtistGroup> groups;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        groups.append({ it.key(), it.value(), covers.value(it.key()) });
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
        bool operator<(const Key &o) const
        {
            return name == o.name ? artist < o.artist : name < o.name;
        }
    };
    QMap<Key, int> counts;
    QMap<Key, QString> covers;
    for (const Song &s : songs) {
        if (!s.album.isEmpty()) {
            ++counts[{ s.album, s.artist }];
            const Key key{ s.album, s.artist };
            if (!covers.contains(key) && !s.coverPath.isEmpty() && QFileInfo::exists(s.coverPath))
                covers.insert(key, s.coverPath);
        }
    }
    QList<AlbumGroup> groups;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        groups.append({ it.key().name, it.key().artist, it.value(), covers.value(it.key()) });
    return groups;
}

} // namespace core
