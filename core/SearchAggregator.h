#pragma once

#include "core/SearchTypes.h"

#include <QList>
#include <QString>

namespace core {

enum class SearchSortMode : int
{
    Comprehensive = 0,
    Popularity,
    RecentPlayed,
    LocalFirst
};

struct SearchResultVariant
{
    SearchResultItem item;
    int relevanceScore = 0;
    double heatPercentile = 0.0;
    int localPriority = 0;
};

struct SearchResultGroup
{
    QString identity;
    QList<SearchResultVariant> variants;
    int preferredIndex = -1;
    int relevanceScore = 0;
    double heatPercentile = 0.0;
    int sourceConsensus = 0;
    int localPriority = 0;
    qint64 lastPlayedMs = 0;
    qint64 playCount = 0;

    SearchResultItem preferredItem() const;
    Song preferredSong() const;
    bool hasSource(SourceId source) const;
};

struct SearchAggregateOptions
{
    QString query;
    SearchSortMode sortMode = SearchSortMode::Comprehensive;
    SourceId preferredSource = SourceId::Local;
    qint64 durationToleranceMs = 3000;
};

class SearchAggregator
{
public:
    static QList<SearchResultGroup> aggregate(const QList<SearchResultItem> &items,
                                              const SearchAggregateOptions &options);
    static QList<SearchResultGroup> aggregateSongsPreservingOrder(
        const QList<Song> &songs, qint64 durationToleranceMs = 3000);
    static int relevanceScore(const SearchResultItem &item, const QString &query);
    static bool sameRecording(const SearchResultItem &left,
                              const SearchResultItem &right,
                              qint64 durationToleranceMs = 3000);
    static QString normalizedMatchText(const QString &text);
};

} // namespace core

Q_DECLARE_METATYPE(core::SearchSortMode)
