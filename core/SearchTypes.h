#pragma once

#include "core/Song.h"

#include <QList>
#include <QString>

namespace core {

enum class SearchCategory : int
{
    All = 0,
    Songs,
    Artists,
    Albums,
    Playlists,
    Lyrics
};

enum class SearchScope : int
{
    All = 0,
    Local,
    Netease,
    QqMusic
};

enum class SearchItemType : int
{
    Song = 0,
    Artist,
    Album,
    Playlist,
    Lyric
};

enum class SearchLoadState : int
{
    Idle = 0,
    Loading,
    Ready,
    Failed,
    TimedOut,
    Cancelled
};

struct SearchRequest
{
    QString keywords;
    SearchCategory category = SearchCategory::Songs;
    SearchScope scope = SearchScope::All;
    int limit = 30;
    int offset = 0;
    quint64 generation = 0;

    bool isValid() const
    {
        return !keywords.trimmed().isEmpty() && limit > 0 && offset >= 0;
    }
};

struct SearchResultItem
{
    SearchItemType type = SearchItemType::Song;
    SourceId source = SourceId::Local;
    QString remoteId;
    QString title;
    QString subtitle;
    QString artist;
    QString album;
    QString coverUrl;
    qint64 durationMs = 0;
    int sourceRank = -1;
    double popularity = -1.0;
    bool playable = true;
    QString availabilityError;
    Song song;

    QString stableIdentity() const
    {
        if (type == SearchItemType::Song && song.hasRemoteIdentity())
            return song.stableIdentity();
        return QStringLiteral("%1:%2:%3")
            .arg(int(source)).arg(int(type)).arg(remoteId.trimmed());
    }
};

struct SearchResponse
{
    SourceId source = SourceId::Local;
    SearchCategory category = SearchCategory::Songs;
    QList<SearchResultItem> items;
    int offset = 0;
    bool hasMore = false;
    quint64 generation = 0;
};

struct SearchSourceState
{
    SourceId source = SourceId::Local;
    SearchLoadState state = SearchLoadState::Idle;
    quint64 generation = 0;
    int offset = 0;
    bool hasMore = false;
    QString error;
};

} // namespace core

Q_DECLARE_METATYPE(core::SearchCategory)
Q_DECLARE_METATYPE(core::SearchScope)
Q_DECLARE_METATYPE(core::SearchItemType)
Q_DECLARE_METATYPE(core::SearchLoadState)

