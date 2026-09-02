#pragma once

#include "core/Song.h"
#include "core/MusicSource.h"

#include <QList>
#include <QHash>
#include <QObject>

class QUrl;

namespace core {

class LibraryService;

class DownloadService : public QObject
{
    Q_OBJECT
public:
    enum State { Queued = 0, Downloading = 1, Failed = 2, Canceled = 3 };
    Q_ENUM(State)

    struct Task
    {
        qint64 id = -1;
        Song song;
        State state = Queued;
        int percent = 0;
        qint64 receivedBytes = 0;
        qint64 totalBytes = 0;
        QString error;
    };

    explicit DownloadService(QObject *parent = nullptr);

    void setSourceProvider(MusicSource *source) { m_source = source; }
    void setSourceRegistry(class MusicSourceRegistry *registry) { m_registry = registry; }
    void setLibrary(LibraryService *library) { m_library = library; }

    qint64 enqueue(const Song &song);
    void enqueue(const QList<Song> &songs);
    void cancel(qint64 taskId);
    void retry(qint64 taskId);
    QList<Task> tasks() const;
    bool hasActiveTasks() const { return m_activeTaskId > 0 || !m_queue.isEmpty(); }

signals:
    void taskAdded(const core::DownloadService::Task &task);
    void taskUpdated(const core::DownloadService::Task &task);
    void taskRemoved(qint64 taskId);
    void tasksChanged();

private:
    void startNext();
    void resolveUrl(qint64 taskId);
    void beginTransfer(qint64 taskId, const QUrl &url);
    void saveCover(const Song &song);
    void failTask(qint64 taskId, const QString &message);
    Task *taskFor(qint64 taskId);
    MusicSource *sourceFor(const Song &song) const;

    MusicSource *m_source = nullptr;
    class MusicSourceRegistry *m_registry = nullptr;
    MusicSource *m_activeSource = nullptr;
    LibraryService *m_library = nullptr;
    QHash<qint64, Task> m_tasks;
    QList<qint64> m_queue;
    qint64 m_nextTaskId = 1;
    qint64 m_activeTaskId = -1;
    MusicSource::DownloadId m_downloadId = 0;
    bool m_cancelRequested = false;
    int m_urlRetryCount = 0;
};

} // namespace core

Q_DECLARE_METATYPE(core::DownloadService::Task)
