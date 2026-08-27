#pragma once

#include "core/MusicSource.h"

#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;

namespace ui {

class CommentsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CommentsDialog(core::MusicSource *source, qint64 songId, const QString &title,
                            QWidget *parent = nullptr);

private:
    void loadMore();

    core::MusicSource *m_source = nullptr;
    qint64 m_songId = 0;
    QListWidget *m_list = nullptr;
    QLabel *m_countLabel = nullptr;
    QPushButton *m_moreBtn = nullptr;
    int m_offset = 0;
    int m_total = 0;
};

} // namespace ui

