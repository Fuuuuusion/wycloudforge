#include "DownloadService.h"

#include "core/LibraryService.h"
#include "core/MusicSource.h"
#include "core/MusicSourceRegistry.h"

#include <QFileInfo>
#include <QUrl>

#include <algorithm>

namespace core {

DownloadService::DownloadService(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<DownloadService::Task>();
}

DownloadService::Task *DownloadService::taskFor(qint64 taskId)
{
    auto it = m_tasks.find(taskId);
    return it == m_tasks.end() ? nullptr : &it.value();
}

QList<DownloadService::Task> DownloadService::tasks() const
{
    QList<Task> result;
    for (qint64 id : m_tasks.keys())
        result.append(m_tasks.value(id));
    std::sort(result.begin(), result.end(), [](const Task &a, const Task &b) { return a.id < b.id; });
    return result;
}

qint64 DownloadService::enqueue(const Song &song)
{
    if (!song.hasRemoteIdentity() || song.id <= 0)
        return -1;
    if (m_library && m_library->isSongDownloaded(song.id))
        return -1;
    for (const Task &task : m_tasks) {
        if (task.song.id == song.id && (task.state == Queued || task.state == Downloading))
            return task.id;
    }
    Task task;
    task.id = m_nextTaskId++;
    task.song = song;
    if (m_library) {
        const Song stored = m_library->songById(song.id);
        if (stored.id > 0)
            task.song = stored;
    }
    m_tasks.insert(task.id, task);
    m_queue.append(task.id);
    emit taskAdded(task);
    emit tasksChanged();
    startNext();
    return task.id;
}

void DownloadService::enqueue(const QList<Song> &songs)
{
    for (const Song &song : songs)
        enqueue(song);
}

void DownloadService::cancel(qint64 taskId)
{
    Task *task = taskFor(taskId);
    if (!task)
        return;
    if (taskId == m_activeTaskId) {
        m_cancelRequested = true;
        if (m_activeSource && m_downloadId)
            m_activeSource->cancelDownload(m_downloadId);
        return;
    }
    if (task->state != Queued)
        return;
    m_queue.removeAll(taskId);
    task->state = Canceled;
    task->error = QStringLiteral("已取消");
    emit taskUpdated(*task);
    emit tasksChanged();
}

void DownloadService::retry(qint64 taskId)
{
    Task *task = taskFor(taskId);
    if (!task || (task->state != Failed && task->state != Canceled))
        return;
    task->state = Queued;
    task->percent = 0;
    task->receivedBytes = 0;
    task->totalBytes = 0;
    task->error.clear();
    m_queue.append(taskId);
    emit taskUpdated(*task);
    emit tasksChanged();
    startNext();
}

void DownloadService::startNext()
{
    if (m_activeTaskId > 0 || m_queue.isEmpty())
        return;
    const qint64 taskId = m_queue.takeFirst();
    Task *task = taskFor(taskId);
    if (!task)
        return startNext();
    if (m_library && m_library->isSongDownloaded(task->song.id)) {
        m_tasks.remove(taskId);
        emit taskRemoved(taskId);
        emit tasksChanged();
        return startNext();
    }
    m_activeSource = sourceFor(task->song);
    if (!m_activeSource || !m_library) {
        failTask(taskId, QStringLiteral("下载服务未准备好"));
        return;
    }
    m_activeTaskId = taskId;
    m_cancelRequested = false;
    m_urlRetryCount = 0;
    task->state = Downloading;
    task->percent = 0;
    task->receivedBytes = 0;
    task->totalBytes = 0;
    emit taskUpdated(*task);
    resolveUrl(taskId);
}

void DownloadService::resolveUrl(qint64 taskId)
{
    Task *task = taskFor(taskId);
    if (!task || taskId != m_activeTaskId || !m_activeSource)
        return;
    m_activeSource->songUrls(QList<Song>{ task->song },
                       [this, taskId](const QJsonArray &array) {
        Task *task = taskFor(taskId);
        if (!task || taskId != m_activeTaskId)
            return;
        if (m_cancelRequested) {
            task->state = Canceled;
            task->error = QStringLiteral("已取消");
            m_activeTaskId = -1;
            emit taskUpdated(*task);
            emit tasksChanged();
            startNext();
            return;
        }
        QString url;
        QString addressError;
        if (!array.isEmpty())
            url = array.first().toObject().value(QStringLiteral("url")).toString();
        if (!array.isEmpty())
            addressError = array.first().toObject().value(QStringLiteral("error")).toString();
        if (url.isEmpty()) {
            if (m_urlRetryCount++ == 0) {
                resolveUrl(taskId);
                return;
            }
            failTask(taskId, addressError.isEmpty()
                ? QStringLiteral("歌曲没有可用的下载地址(可能受版权/VIP、地区或 DRM 限制)")
                : addressError);
            return;
        }
        beginTransfer(taskId, QUrl(url));
    }, [this, taskId](const QString &error) {
        if (taskId == m_activeTaskId)
            failTask(taskId, QStringLiteral("获取下载地址失败：%1").arg(error));
    });
}

void DownloadService::beginTransfer(qint64 taskId, const QUrl &url)
{
    Task *task = taskFor(taskId);
    if (!task || taskId != m_activeTaskId || !m_library || !m_activeSource)
        return;
    const QString path = m_library->downloadFilePathFor(task->song);
    if (path.isEmpty()) {
        failTask(taskId, QStringLiteral("无法生成下载文件名"));
        return;
    }
    m_downloadId = m_activeSource->downloadToFileWithProgress(
        url, path,
        [this, taskId](qint64 received, qint64 total) {
            Task *current = taskFor(taskId);
            if (!current || taskId != m_activeTaskId)
                return;
            const int percent = total > 0
                ? qBound(0, int((received * 100) / total), 100) : current->percent;
            if (percent == current->percent && received == current->receivedBytes
                && total == current->totalBytes)
                return;
            current->percent = percent;
            current->receivedBytes = qMax<qint64>(0, received);
            current->totalBytes = qMax<qint64>(0, total);
            emit taskUpdated(*current);
        },
        [this, taskId, path](const MusicSource::DownloadResult &result) {
            Task *current = taskFor(taskId);
            if (!current || taskId != m_activeTaskId)
                return;
            m_downloadId = 0;
            if (m_cancelRequested) {
                current->state = Canceled;
                current->error = QStringLiteral("已取消");
                current->percent = 0;
                m_activeTaskId = -1;
                emit taskUpdated(*current);
                emit tasksChanged();
                startNext();
                return;
            }
            if (!result.ok) {
                const bool retryable = result.error.contains(QStringLiteral("HTTP 401"))
                    || result.error.contains(QStringLiteral("HTTP 403"));
                if (retryable && m_urlRetryCount++ == 0) {
                    resolveUrl(taskId);
                    return;
                }
                failTask(taskId, result.error.isEmpty() ? QStringLiteral("下载失败") : result.error);
                return;
            }
            const Song completedSong = current->song;
            if (!m_library->setSongDownloaded(completedSong.id, path)) {
                failTask(taskId, QStringLiteral("下载完成但写入数据库失败"));
                return;
            }
            // 永久下载与在线歌曲身份分离保存；封面也必须落到应用封面缓存并
            // 写回在线记录，否则重启后下载文件会被扫描成“本地歌曲”且无封面。
            saveCover(completedSong);
            m_tasks.remove(taskId);
            m_activeTaskId = -1;
            m_activeSource = nullptr;
            emit taskRemoved(taskId);
            emit tasksChanged();
            startNext();
        });
}

void DownloadService::saveCover(const Song &song)
{
    if (!m_library || !song.isOnline() || song.id <= 0)
        return;
    MusicSource *source = sourceFor(song);
    if (!source)
        return;

    const Song stored = m_library->songById(song.id);
    const Song target = stored.id > 0 ? stored : song;
    if (!target.coverPath.isEmpty() && QFileInfo::exists(target.coverPath)
        && QFileInfo(target.coverPath).size() > 0)
        return;

    const QString coverUrl = target.coverUrl.isEmpty() ? song.coverUrl : target.coverUrl;
    if (coverUrl.isEmpty())
        return;
    const QString path = m_library->songCoverCachePath(target);
    if (path.isEmpty())
        return;
    if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
        m_library->setSongCoverPath(target.id, path);
        return;
    }

    const qint64 id = target.id;
    source->downloadToFile(QUrl(coverUrl), path, [this, id, path](bool ok) {
        if (ok && m_library)
            m_library->setSongCoverPath(id, path);
    });
}

void DownloadService::failTask(qint64 taskId, const QString &message)
{
    Task *task = taskFor(taskId);
    if (!task || taskId != m_activeTaskId)
        return;
    m_downloadId = 0;
    task->state = Failed;
    task->error = message;
    m_activeTaskId = -1;
    m_activeSource = nullptr;
    emit taskUpdated(*task);
    emit tasksChanged();
    startNext();
}

MusicSource *DownloadService::sourceFor(const Song &song) const
{
    if (m_registry) {
        if (MusicSource *source = m_registry->sourceFor(song))
            return source;
    }
    return m_source;
}

} // namespace core
