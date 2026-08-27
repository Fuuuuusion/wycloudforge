#pragma once

#include "core/Song.h"

#include <QList>
#include <QPair>
#include <QString>

namespace core {

struct ArtistGroup
{
    QString name;
    int count = 0;
};

struct AlbumGroup
{
    QString name;
    QString artist;
    int count = 0;
};

class SearchService
{
public:
    struct Result
    {
        int index = -1;
        int score = 0;
    };

    static QList<Result> search(const QList<Song> &songs, const QString &query);
    static QList<QPair<int, int>> highlightRanges(const QString &text, const QString &query);

    static QList<ArtistGroup> artists(const QList<Song> &songs);
    static QList<AlbumGroup> albums(const QList<Song> &songs);
};

} // namespace core

