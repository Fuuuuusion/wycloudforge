#include "DownloadPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {
namespace {

QPushButton *makeButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;background:#1B1B24;color:#C8C8D0;"
        "padding:6px 14px;border-radius:15px;font-size:12px;}"
        "QPushButton:hover{background:#3A2024;color:#EC4141;}"));
    return button;
}

}

DownloadPage::DownloadPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 130);
    layout->setSpacing(14);

    auto *header = new QHBoxLayout;
    auto *back = makeButton(QStringLiteral("返回"), this);
    auto *title = new QLabel(QStringLiteral("下载管理"), this);
    title->setProperty("class", "pageTitle");
    header->addWidget(back);
    header->addWidget(title);
    header->addStretch(1);
    layout->addLayout(header);
    connect(back, &QPushButton::clicked, this, &DownloadPage::backRequested);

    auto *hint = new QLabel(QStringLiteral("下载完成后会自动从进行中列表移除；失败任务可以重试。"), this);
    hint->setStyleSheet(QStringLiteral("color:#6E6E7A;font-size:12px;"));
    layout->addWidget(hint);

    auto *content = new QWidget(this);
    m_taskLayout = new QVBoxLayout(content);
    m_taskLayout->setContentsMargins(0, 0, 0, 0);
    m_taskLayout->setSpacing(8);
    m_taskLayout->addStretch(1);
    layout->addWidget(content, 1);
}

void DownloadPage::setTasks(const QList<core::DownloadService::Task> &tasks)
{
    while (QLayoutItem *item = m_taskLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (tasks.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("暂无正在下载的歌曲"), this);
        empty->setAlignment(Qt::AlignCenter);
        empty->setStyleSheet(QStringLiteral("color:#6E6E7A;font-size:14px;padding:30px;"));
        m_taskLayout->addWidget(empty);
    }
    for (const auto &task : tasks) {
        auto *row = new QWidget(this);
        row->setStyleSheet(QStringLiteral(
            "QWidget{background:#16161E;border-radius:10px;}"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(14, 10, 10, 10);
        rowLayout->setSpacing(10);
        auto *name = new QLabel(QStringLiteral("%1\n%2").arg(task.song.title, task.song.artist), row);
        name->setStyleSheet(QStringLiteral("color:#E8E8E8;font-size:13px;"));
        name->setMinimumWidth(260);
        rowLayout->addWidget(name, 1);
        QString status;
        if (task.state == core::DownloadService::Downloading)
            status = QStringLiteral("下载中 %1%").arg(task.percent);
        else if (task.state == core::DownloadService::Queued)
            status = QStringLiteral("等待中");
        else if (task.state == core::DownloadService::Canceled)
            status = QStringLiteral("已取消");
        else
            status = QStringLiteral("失败：%1").arg(task.error);
        auto *state = new QLabel(status, row);
        state->setStyleSheet(QStringLiteral("color:#9A9AA5;font-size:12px;"));
        state->setMinimumWidth(170);
        rowLayout->addWidget(state);
        if (task.state == core::DownloadService::Failed || task.state == core::DownloadService::Canceled) {
            auto *retry = makeButton(QStringLiteral("重试"), row);
            rowLayout->addWidget(retry);
            connect(retry, &QPushButton::clicked, this, [this, id = task.id] { emit retryRequested(id); });
        } else {
            auto *cancel = makeButton(QStringLiteral("取消"), row);
            rowLayout->addWidget(cancel);
            connect(cancel, &QPushButton::clicked, this, [this, id = task.id] { emit cancelRequested(id); });
        }
        m_taskLayout->addWidget(row);
    }
    m_taskLayout->addStretch(1);
}

} // namespace ui
