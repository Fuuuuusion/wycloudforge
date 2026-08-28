#pragma once

#include "core/Song.h"

#include <QTableView>

class QMenu;
class QMouseEvent;

namespace ui {

class SongListModel;

class SongListView : public QTableView
{
    Q_OBJECT
public:
    explicit SongListView(QWidget *parent = nullptr);

    void setSongs(const QList<core::Song> &songs, qint64 playingId = -1);
    QList<core::Song> songs() const;
    void setPlayingId(qint64 playingId);
    void setRemovable(bool removable);
    void setHighlightQuery(const QString &query);
    void setPlaylistMenuItems(const QList<QPair<int, QString>> &items);

signals:
    void playRequested(int row);
    void heartRequested(int row);
    void addToPlaylistRequested(int row, int playlistId);
    void removeFromPlaylistRequested(int row);
    void deleteFromLibraryRequested(int row);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    SongListModel *m_model = nullptr;
    QMenu *m_menu = nullptr;
    QList<QPair<int, QString>> m_playlistItems;
    int m_contextRow = -1;
    bool m_removable = false;
};

} // namespace ui
