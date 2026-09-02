#pragma once

#include "core/DownloadService.h"
#include "core/Song.h"

#include <QWidget>

class QVBoxLayout;

namespace ui {

class DownloadPage : public QWidget
{
    Q_OBJECT
public:
    explicit DownloadPage(QWidget *parent = nullptr);

    void setTasks(const QList<core::DownloadService::Task> &tasks);
    void setDownloadedSongs(const QList<core::Song> &songs);

signals:
    void backRequested();
    void cancelRequested(qint64 taskId);
    void retryRequested(qint64 taskId);
    void deleteRequested(qint64 songId);

private:
    void rebuildTasks();
    void rebuildDownloads();

    QVBoxLayout *m_taskLayout = nullptr;
    QVBoxLayout *m_downloadLayout = nullptr;
    QList<core::DownloadService::Task> m_tasks;
    QList<core::Song> m_downloadedSongs;
};

} // namespace ui
