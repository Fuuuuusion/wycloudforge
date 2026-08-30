#pragma once

#include "core/Song.h"

#include <QList>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QString>
#include <QThreadPool>

#include <atomic>
#include <memory>

namespace core {

struct ArtistGroup
{
    QString name;
    int count = 0;
    QString coverPath;
};

struct AlbumGroup
{
    QString name;
    QString artist;
    int count = 0;
    QString coverPath;
};

class SearchService : public QObject
{
    Q_OBJECT
public:
    struct Result
    {
        int index = -1;
        int score = 0;
    };

    struct Batch
    {
        QList<Result> results;
        int totalMatches = 0;
    };

    explicit SearchService(QObject *parent = nullptr);
    ~SearchService() override;

    void updateSnapshot(const QList<Song> &songs);
    quint64 searchAsync(const QString &query, int maxResults = 200);
    void cancelAsync();

    static QList<Result> search(const QList<Song> &songs, const QString &query,
                                int maxResults = -1);
    static Batch searchBatch(const QList<Song> &songs, const QString &query,
                             int maxResults = -1);
    static QList<QPair<int, int>> highlightRanges(const QString &text, const QString &query);

    static QList<ArtistGroup> artists(const QList<Song> &songs);
    static QList<AlbumGroup> albums(const QList<Song> &songs);

signals:
    void searchFinished(quint64 generation, const QString &query,
                        const QList<core::Song> &songs, int totalMatches);

private:
    QMutex m_snapshotMutex;
    std::shared_ptr<const QList<Song>> m_snapshot;
    QThreadPool m_pool;
    std::atomic<quint64> m_generation{ 0 };
    std::atomic_bool m_stopping{ false };
};

} // namespace core
