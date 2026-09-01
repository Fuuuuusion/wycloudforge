#include "SearchAggregator.h"

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace core {
namespace {

QString normalizedPhrase(const QString &text)
{
    return text.normalized(QString::NormalizationForm_KC).toCaseFolded().simplified();
}

QString primaryArtist(const QString &artist)
{
    static const QRegularExpression separator(QStringLiteral(
        R"((?:\s*(?:/|、|,|，|;|；|&|＆|×)\s*)|(?:\s+feat\.?\s+)|(?:\s+ft\.?\s+)|(?:\s+with\s+))"),
        QRegularExpression::CaseInsensitiveOption);
    const QStringList parts = normalizedPhrase(artist).split(separator, Qt::SkipEmptyParts);
    return SearchAggregator::normalizedMatchText(parts.isEmpty() ? artist : parts.constFirst());
}

QString versionSignature(const QString &title)
{
    static const QList<QPair<QString, QRegularExpression>> markers = {
        { QStringLiteral("live"), QRegularExpression(QStringLiteral(R"((\blive\b|现场|演唱会|巡演))"), QRegularExpression::CaseInsensitiveOption) },
        { QStringLiteral("remix"), QRegularExpression(QStringLiteral(R"((\bremix\b|混音))"), QRegularExpression::CaseInsensitiveOption) },
        { QStringLiteral("cover"), QRegularExpression(QStringLiteral(R"((\bcover\b|翻唱))"), QRegularExpression::CaseInsensitiveOption) },
        { QStringLiteral("instrumental"), QRegularExpression(QStringLiteral(R"((\binstrumental\b|伴奏|卡拉ok|karaoke|纯音乐))"), QRegularExpression::CaseInsensitiveOption) },
        { QStringLiteral("acoustic"), QRegularExpression(QStringLiteral(R"((\bacoustic\b|不插电|unplugged))"), QRegularExpression::CaseInsensitiveOption) },
        { QStringLiteral("demo"), QRegularExpression(QStringLiteral(R"((\bdemo\b|小样))"), QRegularExpression::CaseInsensitiveOption) },
        { QStringLiteral("remaster"), QRegularExpression(QStringLiteral(R"((remaster(?:ed)?|重制))"), QRegularExpression::CaseInsensitiveOption) },
        { QStringLiteral("sped"), QRegularExpression(QStringLiteral(R"((sped\s*up|加速版|slowed|慢速版))"), QRegularExpression::CaseInsensitiveOption) },
        { QStringLiteral("language"), QRegularExpression(QStringLiteral(R"(((?:english|japanese|korean|chinese)\s*(?:ver(?:sion)?\.?)|国语版|粤语版|中文版|英文版|日语版|韩语版))"), QRegularExpression::CaseInsensitiveOption) }
    };
    QStringList result;
    const QString normalized = normalizedPhrase(title);
    for (const auto &marker : markers) {
        if (marker.second.match(normalized).hasMatch())
            result.append(marker.first);
    }
    return result.join(QLatin1Char('+'));
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

bool combinationMatches(const QString &title, const QString &artist, const QString &query)
{
    if (title.isEmpty() || artist.isEmpty() || query.isEmpty())
        return false;
    const bool crossesBoundary = !title.contains(query) && !artist.contains(query)
        && ((artist + title).contains(query) || (title + artist).contains(query)
            || (artist + QLatin1Char(' ') + title).contains(query)
            || (title + QLatin1Char(' ') + artist).contains(query));
    if (crossesBoundary)
        return true;
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

int localPriority(const SearchResultItem &item)
{
    const Song &song = item.song;
    if (song.isDownloaded())
        return 400;
    if (song.isCached())
        return 300;
    if (!song.isOnline() && !song.missing && !song.filePath.isEmpty()) {
        const QFileInfo file(song.filePath);
        if (file.isFile() && file.size() > 0)
            return 200;
    }
    return item.playable ? 100 : 0;
}

bool groupHasSource(const SearchResultGroup &group, SourceId source)
{
    for (const SearchResultVariant &variant : group.variants) {
        if (variant.item.source == source)
            return true;
    }
    return false;
}

bool canJoinGroup(const SearchResultVariant &variant, const SearchResultGroup &group,
                  qint64 durationToleranceMs)
{
    for (const SearchResultVariant &existing : group.variants) {
        if (!SearchAggregator::sameRecording(variant.item, existing.item,
                                              durationToleranceMs)) {
            return false;
        }
    }
    return !group.variants.isEmpty();
}

qint64 itemDuration(const SearchResultItem &item)
{
    return item.durationMs > 0 ? item.durationMs : item.song.durationMs;
}

qint64 largestDurationDelta(const SearchResultVariant &variant,
                            const SearchResultGroup &group)
{
    const qint64 duration = itemDuration(variant.item);
    qint64 largest = 0;
    for (const SearchResultVariant &existing : group.variants)
        largest = qMax(largest, qAbs(duration - itemDuration(existing.item)));
    return largest;
}

QString groupTieIdentity(const SearchResultGroup &group)
{
    return group.variants.isEmpty()
        ? QString() : group.variants.constFirst().item.stableIdentity();
}

bool betterPreferredVariant(const SearchResultVariant &left,
                            const SearchResultVariant &right,
                            SourceId preferredSource)
{
    if (left.localPriority != right.localPriority)
        return left.localPriority > right.localPriority;
    if (preferredSource != SourceId::Local) {
        const bool leftPreferred = left.item.source == preferredSource;
        const bool rightPreferred = right.item.source == preferredSource;
        if (leftPreferred != rightPreferred)
            return leftPreferred;
    }
    if (left.item.playable != right.item.playable)
        return left.item.playable;
    if (left.heatPercentile != right.heatPercentile)
        return left.heatPercentile > right.heatPercentile;
    if (left.item.sourceRank != right.item.sourceRank)
        return left.item.sourceRank < right.item.sourceRank;
    return left.item.stableIdentity() < right.item.stableIdentity();
}

void finalizeGroup(SearchResultGroup *group, SourceId preferredSource)
{
    QSet<int> sources;
    QStringList identities;
    group->preferredIndex = -1;
    for (int i = 0; i < group->variants.size(); ++i) {
        const SearchResultVariant &variant = group->variants.at(i);
        sources.insert(int(variant.item.source));
        identities.append(variant.item.stableIdentity());
        group->relevanceScore = qMax(group->relevanceScore, variant.relevanceScore);
        group->heatPercentile = qMax(group->heatPercentile, variant.heatPercentile);
        group->localPriority = qMax(group->localPriority, variant.localPriority);
        group->lastPlayedMs = qMax(group->lastPlayedMs, variant.item.song.lastPlayedMs);
        group->playCount = qMax(group->playCount, variant.item.song.playCount);
        if (group->preferredIndex < 0
            || betterPreferredVariant(variant, group->variants.at(group->preferredIndex),
                                      preferredSource)) {
            group->preferredIndex = i;
        }
    }
    identities.sort(Qt::CaseSensitive);
    group->identity = QStringLiteral("group:%1").arg(identities.join(QLatin1Char('|')));
    group->sourceConsensus = sources.size();
}

bool betterGroup(const SearchResultGroup &left, const SearchResultGroup &right,
                 SearchSortMode mode)
{
    if (mode == SearchSortMode::Popularity) {
        const bool leftRelevant = left.relevanceScore >= 450;
        const bool rightRelevant = right.relevanceScore >= 450;
        if (leftRelevant != rightRelevant)
            return leftRelevant;
        if (left.heatPercentile != right.heatPercentile)
            return left.heatPercentile > right.heatPercentile;
        if (left.sourceConsensus != right.sourceConsensus)
            return left.sourceConsensus > right.sourceConsensus;
        if (left.relevanceScore != right.relevanceScore)
            return left.relevanceScore > right.relevanceScore;
    } else if (mode == SearchSortMode::RecentPlayed) {
        if (left.lastPlayedMs != right.lastPlayedMs)
            return left.lastPlayedMs > right.lastPlayedMs;
        if (left.playCount != right.playCount)
            return left.playCount > right.playCount;
        if (left.relevanceScore != right.relevanceScore)
            return left.relevanceScore > right.relevanceScore;
        if (left.heatPercentile != right.heatPercentile)
            return left.heatPercentile > right.heatPercentile;
    } else if (mode == SearchSortMode::LocalFirst) {
        if (left.localPriority != right.localPriority)
            return left.localPriority > right.localPriority;
        if (left.relevanceScore != right.relevanceScore)
            return left.relevanceScore > right.relevanceScore;
        if (left.heatPercentile != right.heatPercentile)
            return left.heatPercentile > right.heatPercentile;
        if (left.sourceConsensus != right.sourceConsensus)
            return left.sourceConsensus > right.sourceConsensus;
    } else {
        if (left.relevanceScore != right.relevanceScore)
            return left.relevanceScore > right.relevanceScore;
        if (left.heatPercentile != right.heatPercentile)
            return left.heatPercentile > right.heatPercentile;
        if (left.sourceConsensus != right.sourceConsensus)
            return left.sourceConsensus > right.sourceConsensus;
        if (left.localPriority != right.localPriority)
            return left.localPriority > right.localPriority;
        if (left.lastPlayedMs != right.lastPlayedMs)
            return left.lastPlayedMs > right.lastPlayedMs;
        if (left.playCount != right.playCount)
            return left.playCount > right.playCount;
    }
    return left.identity < right.identity;
}

} // namespace

SearchResultItem SearchResultGroup::preferredItem() const
{
    return preferredIndex >= 0 && preferredIndex < variants.size()
        ? variants.at(preferredIndex).item : SearchResultItem{};
}

Song SearchResultGroup::preferredSong() const
{
    return preferredItem().song;
}

bool SearchResultGroup::hasSource(SourceId source) const
{
    return groupHasSource(*this, source);
}

QString SearchAggregator::normalizedMatchText(const QString &text)
{
    const QString normalized = normalizedPhrase(text);
    QString result;
    result.reserve(normalized.size());
    for (const QChar character : normalized) {
        if (character.isLetterOrNumber())
            result.append(character);
    }
    return result;
}

int SearchAggregator::relevanceScore(const SearchResultItem &item, const QString &query)
{
    const QString phraseQuery = normalizedPhrase(query);
    if (phraseQuery.isEmpty())
        return 0;
    const QString title = normalizedPhrase(item.title.isEmpty() ? item.song.title : item.title);
    const QString artist = normalizedPhrase(item.artist.isEmpty() ? item.song.artist : item.artist);
    const QString album = normalizedPhrase(item.album.isEmpty() ? item.song.album : item.album);
    if (item.type == SearchItemType::Artist)
        return qMax(100, scoreField(title, phraseQuery, 700, 650, 600));
    if (item.type == SearchItemType::Album)
        return qMax(100, scoreField(title, phraseQuery, 550, 500, 450));
    if (item.type == SearchItemType::Playlist)
        return qMax(100, scoreField(title, phraseQuery, 500, 450, 400));

    int score = scoreField(title, phraseQuery, 1000, 900, 800);
    if (combinationMatches(title, artist, phraseQuery))
        score = qMax(score, 750);
    score = qMax(score, scoreField(artist, phraseQuery, 700, 650, 600));
    score = qMax(score, scoreField(album, phraseQuery, 550, 500, 450));
    return qMax(100, score);
}

bool SearchAggregator::sameRecording(const SearchResultItem &left,
                                     const SearchResultItem &right,
                                     qint64 durationToleranceMs)
{
    if (left.type != SearchItemType::Song || right.type != SearchItemType::Song) {
        return false;
    }
    const QString leftTitle = normalizedMatchText(
        left.title.isEmpty() ? left.song.title : left.title);
    const QString rightTitle = normalizedMatchText(
        right.title.isEmpty() ? right.song.title : right.title);
    if (leftTitle.isEmpty() || leftTitle != rightTitle)
        return false;
    const QString leftArtist = primaryArtist(
        left.artist.isEmpty() ? left.song.artist : left.artist);
    const QString rightArtist = primaryArtist(
        right.artist.isEmpty() ? right.song.artist : right.artist);
    if (leftArtist.isEmpty() || leftArtist != rightArtist)
        return false;
    const QString leftAlbum = normalizedMatchText(
        left.album.isEmpty() ? left.song.album : left.album);
    const QString rightAlbum = normalizedMatchText(
        right.album.isEmpty() ? right.song.album : right.album);
    if (leftAlbum.isEmpty() || leftAlbum != rightAlbum)
        return false;
    const QString leftVersion = versionSignature(
        left.title.isEmpty() ? left.song.title : left.title);
    const QString rightVersion = versionSignature(
        right.title.isEmpty() ? right.song.title : right.title);
    if (leftVersion != rightVersion)
        return false;
    const qint64 leftDuration = left.durationMs > 0 ? left.durationMs : left.song.durationMs;
    const qint64 rightDuration = right.durationMs > 0 ? right.durationMs : right.song.durationMs;
    return leftDuration > 0 && rightDuration > 0
        && qAbs(leftDuration - rightDuration) <= qMax<qint64>(0, durationToleranceMs);
}

QList<SearchResultGroup> SearchAggregator::aggregate(
    const QList<SearchResultItem> &items, const SearchAggregateOptions &options)
{
    QList<SearchResultVariant> variants;
    variants.reserve(items.size());
    QSet<QString> seenIdentities;
    for (const SearchResultItem &item : items) {
        const QString identity = item.stableIdentity();
        if (identity.isEmpty() || seenIdentities.contains(identity))
            continue;
        seenIdentities.insert(identity);
        SearchResultVariant variant;
        variant.item = item;
        variant.relevanceScore = relevanceScore(item, options.query);
        variant.localPriority = ::core::localPriority(item);
        variants.append(variant);
    }

    QHash<int, QList<int>> indexesBySource;
    for (int i = 0; i < variants.size(); ++i) {
        const int key = int(variants.at(i).item.source) * 16
            + int(variants.at(i).item.type);
        indexesBySource[key].append(i);
    }
    for (auto it = indexesBySource.begin(); it != indexesBySource.end(); ++it) {
        QList<int> indexes = it.value();
        std::sort(indexes.begin(), indexes.end(), [&variants](int leftIndex, int rightIndex) {
            const SearchResultItem &left = variants.at(leftIndex).item;
            const SearchResultItem &right = variants.at(rightIndex).item;
            const bool leftHasPopularity = left.popularity >= 0.0;
            const bool rightHasPopularity = right.popularity >= 0.0;
            if (leftHasPopularity != rightHasPopularity)
                return leftHasPopularity;
            if (leftHasPopularity && left.popularity != right.popularity)
                return left.popularity > right.popularity;
            if (left.sourceRank != right.sourceRank)
                return left.sourceRank < right.sourceRank;
            return left.stableIdentity() < right.stableIdentity();
        });
        const int count = indexes.size();
        for (int position = 0; position < count; ++position) {
            variants[indexes.at(position)].heatPercentile = count <= 1
                ? 1.0 : 1.0 - double(position) / double(count - 1);
        }
    }

    std::sort(variants.begin(), variants.end(), [](const SearchResultVariant &left,
                                                    const SearchResultVariant &right) {
        if (left.item.source != right.item.source)
            return int(left.item.source) < int(right.item.source);
        return left.item.stableIdentity() < right.item.stableIdentity();
    });

    QList<SearchResultGroup> groups;
    groups.reserve(variants.size());
    for (const SearchResultVariant &variant : std::as_const(variants)) {
        QList<int> candidates;
        if (variant.item.type == SearchItemType::Song) {
            for (int i = 0; i < groups.size(); ++i) {
                if (canJoinGroup(variant, groups.at(i), options.durationToleranceMs)) {
                    candidates.append(i);
                }
            }
        }
        if (!candidates.isEmpty()) {
            int bestCandidate = candidates.constFirst();
            for (int i = 1; i < candidates.size(); ++i) {
                const int candidate = candidates.at(i);
                const qint64 candidateDelta = largestDurationDelta(variant, groups.at(candidate));
                const qint64 bestDelta = largestDurationDelta(variant, groups.at(bestCandidate));
                if (candidateDelta < bestDelta
                    || (candidateDelta == bestDelta
                        && groupTieIdentity(groups.at(candidate))
                            < groupTieIdentity(groups.at(bestCandidate)))) {
                    bestCandidate = candidate;
                }
            }
            groups[bestCandidate].variants.append(variant);
        } else {
            SearchResultGroup group;
            group.variants.append(variant);
            groups.append(group);
        }
    }

    for (SearchResultGroup &group : groups)
        finalizeGroup(&group, options.preferredSource);
    std::sort(groups.begin(), groups.end(), [&options](const SearchResultGroup &left,
                                                       const SearchResultGroup &right) {
        return betterGroup(left, right, options.sortMode);
    });
    return groups;
}

} // namespace core
