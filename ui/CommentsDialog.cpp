#include "CommentsDialog.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {

CommentsDialog::CommentsDialog(core::MusicSource *source, qint64 songId, const QString &title, QWidget *parent)
    : QDialog(parent)
    , m_source(source)
    , m_songId(songId)
{
    setWindowTitle(QStringLiteral("评论 · %1").arg(title));
    resize(560, 520);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(10);

    m_countLabel = new QLabel(this);
    m_countLabel->setStyleSheet(QStringLiteral("color:#6E6E7A;font-size:12px;"));
    layout->addWidget(m_countLabel);

    m_list = new QListWidget(this);
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget{background:rgba(255,255,255,0.04);border:none;border-radius:10px;}"
        "QListWidget::item{padding:10px;border-radius:6px;}"));
    m_list->setWordWrap(true);
    layout->addWidget(m_list, 1);

    auto *row = new QHBoxLayout;
    m_moreBtn = new QPushButton(QStringLiteral("加载更多"), this);
    row->addStretch(1);
    row->addWidget(m_moreBtn);
    layout->addLayout(row);
    connect(m_moreBtn, &QPushButton::clicked, this, &CommentsDialog::loadMore);
    loadMore();
}

void CommentsDialog::loadMore()
{
    if (!m_source)
        return;
    m_source->comments(m_songId, m_offset, 20, [this](const QJsonObject &obj) {
        m_total = obj.value(QStringLiteral("total")).toInt();
        m_countLabel->setText(QStringLiteral("共 %1 条评论").arg(m_total));
        const QJsonArray comments = obj.value(QStringLiteral("comments")).toArray();
        for (const QJsonValue &v : comments) {
            const QJsonObject c = v.toObject();
            const QJsonObject user = c.value(QStringLiteral("user")).toObject();
            const QString name = user.value(QStringLiteral("nickname")).toString();
            const QString content = c.value(QStringLiteral("content")).toString();
            const qint64 timeMs = c.value(QStringLiteral("time")).toVariant().toLongLong();
            const int liked = c.value(QStringLiteral("likedCount")).toInt();
            const QString timeText = QDateTime::fromMSecsSinceEpoch(timeMs).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
            auto *item = new QListWidgetItem(QStringLiteral("%1 · %2 · 赞 %3\n%4").arg(name, timeText).arg(liked).arg(content));
            m_list->addItem(item);
        }
        m_offset += comments.size();
        m_moreBtn->setEnabled(m_offset < m_total);
    }, [this](const QString &msg) {
        m_countLabel->setText(QStringLiteral("评论加载失败:%1").arg(msg));
    });
}

} // namespace ui

