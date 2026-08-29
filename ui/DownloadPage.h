#pragma once

#include "core/DownloadService.h"

#include <QWidget>

class QVBoxLayout;

namespace ui {

class DownloadPage : public QWidget
{
    Q_OBJECT
public:
    explicit DownloadPage(QWidget *parent = nullptr);

    void setTasks(const QList<core::DownloadService::Task> &tasks);

signals:
    void backRequested();
    void cancelRequested(qint64 taskId);
    void retryRequested(qint64 taskId);

private:
    QVBoxLayout *m_taskLayout = nullptr;
};

} // namespace ui
