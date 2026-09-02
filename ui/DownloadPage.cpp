#include "DownloadPage.h"

#include "ui/ThemeManager.h"

#include <QDateTime>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace ui {
namespace {

QString formatBytes(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("待获取");
    static const char *units[] = { "B", "KB", "MB", "GB" };
    double value = double(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2")
        .arg(value, 0, 'f', unit == 0 ? 0 : 1)
        .arg(QLatin1String(units[unit]));
}

QString formatTransfer(qint64 received, qint64 total)
{
    if (received <= 0 && total <= 0)
        return QStringLiteral("待获取");
    if (total > 0)
        return QStringLiteral("%1 / %2").arg(formatBytes(received), formatBytes(total));
    return formatBytes(received);
}

QPushButton *makeButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(30);
    setThemedStyleSheet(button, QStringLiteral(
        "QPushButton{border:none;background:@surfaceAlt;color:@textSecondary;"
        "padding:5px 12px;border-radius:15px;font-size:12px;}"
        "QPushButton:hover{background:@accentSoft;color:@accent;}"));
    return button;
}

void clearLayout(QVBoxLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

class DownloadActivityIndicator final : public QWidget
{
public:
    explicit DownloadActivityIndicator(bool active, QWidget *parent = nullptr)
        : QWidget(parent), m_active(active)
    {
        setFixedSize(28, 28);
        if (m_active) {
            m_timer.setInterval(40);
            connect(&m_timer, &QTimer::timeout, this, [this] { update(); });
            m_timer.start();
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor color = themeColor(m_active ? ThemeColor::Accent
                                                 : ThemeColor::TextSecondary);
        painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (m_active) {
            const qreal phase = (QDateTime::currentMSecsSinceEpoch() % 900) / 900.0;
            const qreal y = -3.0 + phase * 24.0;
            painter.save();
            painter.setClipRect(QRectF(4, 3, 20, 17));
            painter.drawLine(QPointF(14, y - 5), QPointF(14, y + 4));
            painter.drawLine(QPointF(10.5, y + 0.5), QPointF(14, y + 4));
            painter.drawLine(QPointF(17.5, y + 0.5), QPointF(14, y + 4));
            painter.restore();
        } else {
            painter.drawLine(QPointF(14, 6), QPointF(14, 16));
            painter.drawLine(QPointF(10.5, 12.5), QPointF(14, 16));
            painter.drawLine(QPointF(17.5, 12.5), QPointF(14, 16));
        }
        painter.drawLine(QPointF(7, 21), QPointF(21, 21));
    }

private:
    bool m_active = false;
    QTimer m_timer;
};

QVBoxLayout *makeColumn(const QString &title, QWidget *parent, QHBoxLayout *host)
{
    auto *panel = new QWidget(parent);
    panel->setObjectName(QStringLiteral("downloadColumn"));
    setThemedStyleSheet(panel, QStringLiteral(
        "QWidget#downloadColumn{background:@pageBackground;border:none;border-radius:10px;}"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);
    auto *heading = new QLabel(title, panel);
    setThemedStyleSheet(heading, QStringLiteral("color:@textPrimary;font-size:15px;font-weight:600;"));
    layout->addWidget(heading);

    auto *scroll = new QScrollArea(panel);
    scroll->setObjectName(title == QStringLiteral("下载任务")
                              ? QStringLiteral("downloadTaskScroll")
                              : QStringLiteral("downloadedSongScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(scroll);
    auto *rows = new QVBoxLayout(content);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(8);
    rows->setAlignment(Qt::AlignTop);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    host->addWidget(panel, 1);
    return rows;
}

QLabel *makeElidedLabel(const QString &text, const QString &style, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    setThemedStyleSheet(label, style);
    label->setToolTip(text);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return label;
}

} // namespace

DownloadPage::DownloadPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 28);
    layout->setSpacing(14);

    auto *header = new QHBoxLayout;
    auto *back = makeButton(QStringLiteral("返回"), this);
    back->setFixedSize(68, 32);
    auto *title = new QLabel(QStringLiteral("下载管理"), this);
    title->setProperty("class", "pageTitle");
    header->addWidget(back);
    header->addSpacing(12);
    header->addWidget(title);
    header->addStretch(1);
    layout->addLayout(header);
    connect(back, &QPushButton::clicked, this, &DownloadPage::backRequested);

    auto *hint = new QLabel(QStringLiteral("任务按顺序逐项下载；完成后会即时进入右侧已下载列表。"), this);
    setThemedStyleSheet(hint, QStringLiteral("color:@textTertiary;font-size:12px;"));
    layout->addWidget(hint);

    auto *columns = new QHBoxLayout;
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(14);
    m_taskLayout = makeColumn(QStringLiteral("下载任务"), this, columns);
    m_downloadLayout = makeColumn(QStringLiteral("已下载"), this, columns);
    layout->addLayout(columns, 1);
}

void DownloadPage::setTasks(const QList<core::DownloadService::Task> &tasks)
{
    m_tasks = tasks;
    rebuildTasks();
}

void DownloadPage::setDownloadedSongs(const QList<core::Song> &songs)
{
    m_downloadedSongs = songs;
    rebuildDownloads();
}

void DownloadPage::rebuildTasks()
{
    clearLayout(m_taskLayout);
    if (m_tasks.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("暂无下载任务"), this);
        empty->setAlignment(Qt::AlignCenter);
        setThemedStyleSheet(empty, QStringLiteral("color:@textTertiary;font-size:13px;padding:28px;"));
        m_taskLayout->addWidget(empty);
        return;
    }

    for (const auto &task : std::as_const(m_tasks)) {
        auto *row = new QWidget(this);
        row->setObjectName(QStringLiteral("downloadTaskRow"));
        row->setFixedHeight(76);
        setThemedStyleSheet(row, QStringLiteral("QWidget{background:@surface;border-radius:9px;}"));
        auto *grid = new QGridLayout(row);
        grid->setContentsMargins(10, 8, 10, 8);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(3);
        grid->addWidget(new DownloadActivityIndicator(
                            task.state == core::DownloadService::Downloading, row),
                        0, 0, 2, 1, Qt::AlignCenter);
        const QString main = QStringLiteral("%1 - %2")
                                 .arg(task.song.title.isEmpty() ? QStringLiteral("未知歌曲")
                                                                : task.song.title,
                                      task.song.artist.isEmpty() ? QStringLiteral("未知歌手")
                                                                 : task.song.artist);
        grid->addWidget(makeElidedLabel(main,
            QStringLiteral("color:@textPrimary;font-size:13px;font-weight:600;"), row), 0, 1);
        grid->addWidget(makeElidedLabel(
            task.song.album.isEmpty() ? QStringLiteral("未知专辑") : task.song.album,
            QStringLiteral("color:@textTertiary;font-size:11px;"), row), 1, 1);

        auto *size = new QLabel(formatTransfer(task.receivedBytes, task.totalBytes), row);
        size->setFixedWidth(128);
        size->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        setThemedStyleSheet(size, QStringLiteral("color:@textSecondary;font-size:11px;"));
        grid->addWidget(size, 0, 2, 2, 1);

        QString stateText;
        if (task.state == core::DownloadService::Downloading)
            stateText = QStringLiteral("下载中 %1%").arg(task.percent);
        else if (task.state == core::DownloadService::Queued)
            stateText = QStringLiteral("等待下载");
        else if (task.state == core::DownloadService::Canceled)
            stateText = QStringLiteral("已取消");
        else
            stateText = QStringLiteral("失败");
        auto *state = new QLabel(stateText, row);
        state->setFixedWidth(82);
        state->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        setThemedStyleSheet(state, QStringLiteral("color:@focus;font-size:11px;"));
        state->setToolTip(task.error);
        grid->addWidget(state, 0, 3);

        auto *progress = new QProgressBar(row);
        progress->setFixedSize(82, 5);
        progress->setRange(0, 100);
        progress->setValue(task.percent);
        progress->setTextVisible(false);
        setThemedStyleSheet(progress, QStringLiteral(
            "QProgressBar{border:none;background:@surfacePressed;border-radius:2px;}"
            "QProgressBar::chunk{background:@accent;border-radius:2px;}"));
        grid->addWidget(progress, 1, 3);

        auto *action = makeButton(
            task.state == core::DownloadService::Failed
                    || task.state == core::DownloadService::Canceled
                ? QStringLiteral("重试") : QStringLiteral("取消"), row);
        action->setFixedWidth(54);
        grid->addWidget(action, 0, 4, 2, 1, Qt::AlignCenter);
        if (task.state == core::DownloadService::Failed
            || task.state == core::DownloadService::Canceled) {
            connect(action, &QPushButton::clicked, this,
                    [this, id = task.id] { emit retryRequested(id); });
        } else {
            connect(action, &QPushButton::clicked, this,
                    [this, id = task.id] { emit cancelRequested(id); });
        }
        grid->setColumnStretch(1, 1);
        m_taskLayout->addWidget(row);
    }
}

void DownloadPage::rebuildDownloads()
{
    clearLayout(m_downloadLayout);
    if (m_downloadedSongs.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("暂无永久下载"), this);
        empty->setAlignment(Qt::AlignCenter);
        setThemedStyleSheet(empty, QStringLiteral("color:@textTertiary;font-size:13px;padding:28px;"));
        m_downloadLayout->addWidget(empty);
        return;
    }

    for (const core::Song &song : std::as_const(m_downloadedSongs)) {
        auto *row = new QWidget(this);
        row->setObjectName(QStringLiteral("downloadedSongRow"));
        row->setFixedHeight(76);
        setThemedStyleSheet(row, QStringLiteral("QWidget{background:@surface;border-radius:9px;}"));
        auto *grid = new QGridLayout(row);
        grid->setContentsMargins(10, 8, 10, 8);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(3);
        grid->addWidget(new DownloadActivityIndicator(false, row), 0, 0, 2, 1, Qt::AlignCenter);
        const QString main = QStringLiteral("%1 - %2")
                                 .arg(song.title.isEmpty() ? QStringLiteral("未知歌曲") : song.title,
                                      song.artist.isEmpty() ? QStringLiteral("未知歌手") : song.artist);
        grid->addWidget(makeElidedLabel(main,
            QStringLiteral("color:@textPrimary;font-size:13px;font-weight:600;"), row), 0, 1);
        grid->addWidget(makeElidedLabel(
            song.album.isEmpty() ? QStringLiteral("未知专辑") : song.album,
            QStringLiteral("color:@textTertiary;font-size:11px;"), row), 1, 1);
        auto *size = new QLabel(formatBytes(QFileInfo(song.downloadPath).size()), row);
        size->setFixedWidth(78);
        size->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        setThemedStyleSheet(size, QStringLiteral("color:@textSecondary;font-size:11px;"));
        grid->addWidget(size, 0, 2, 2, 1);
        auto *state = new QLabel(QStringLiteral("已下载"), row);
        state->setFixedWidth(58);
        state->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        setThemedStyleSheet(state, QStringLiteral("color:@success;font-size:11px;"));
        grid->addWidget(state, 0, 3, 2, 1);
        auto *remove = makeButton(QStringLiteral("删除"), row);
        remove->setFixedWidth(54);
        grid->addWidget(remove, 0, 4, 2, 1, Qt::AlignCenter);
        connect(remove, &QPushButton::clicked, this,
                [this, id = song.id] { emit deleteRequested(id); });
        grid->setColumnStretch(1, 1);
        m_downloadLayout->addWidget(row);
    }
}

} // namespace ui
