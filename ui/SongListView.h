#pragma once

#include "core/Song.h"

#include <QTableView>
#include <QSet>

class QMenu;
class QMouseEvent;
class QResizeEvent;
class QAction;
class QLabel;
class QPushButton;
class QToolButton;

namespace core {
class LibraryService;
}

namespace ui {

class SongListModel;

class SongListView : public QTableView
{
    Q_OBJECT
public:
    enum DownloadActionMode { DownloadAction = 0, DeleteDownloadAction = 1, NoDownloadAction = 2 };

    explicit SongListView(QWidget *parent = nullptr);

    void setSongs(const QList<core::Song> &songs, qint64 playingId = -1);
    QList<core::Song> songs() const;
    void setPlayingId(qint64 playingId);
    void setRemovable(bool removable);
    void setHighlightQuery(const QString &query);
    void setPlaylistMenuItems(const QList<QPair<int, QString>> &items);
    void setFavoriteIds(const QSet<qint64> &ids);
    void refreshLibraryState(core::LibraryService *library);
    void setDownloadActionMode(DownloadActionMode mode);
    QList<core::Song> selectedSongs() const;
    bool batchMode() const { return m_batchMode; }

signals:
    void playRequested(int row);
    void heartRequested(int row);
    void downloadRequested(int row);
    void deleteDownloadRequested(int row);
    void addToPlaylistRequested(int row, int playlistId);
    void removeFromPlaylistRequested(int row);
    void deleteFromLibraryRequested(int row);
    void batchAddToPlaylistRequested(const QList<core::Song> &songs, int playlistId);
    void batchCreatePlaylistRequested(const QList<core::Song> &songs);
    void batchFavoriteRequested(const QList<core::Song> &songs, bool favorite);
    void batchDownloadRequested(const QList<core::Song> &songs);
    void batchDeleteRequested(const QList<core::Song> &songs);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void setBatchMode(bool enabled);
    void updateBatchButtons();
    void updateBatchLayout();
    void rebuildBatchPlaylistMenu();
    void toggleRowSelection(int row);
    SongListModel *m_model = nullptr;
    QMenu *m_menu = nullptr;
    QWidget *m_batchBar = nullptr;
    QPushButton *m_batchToggle = nullptr;
    QLabel *m_selectionSummary = nullptr;
    QPushButton *m_selectAll = nullptr;
    QPushButton *m_clearSelection = nullptr;
    QPushButton *m_favoriteSelected = nullptr;
    QPushButton *m_unfavoriteSelected = nullptr;
    QPushButton *m_addSelected = nullptr;
    QPushButton *m_downloadSelected = nullptr;
    QPushButton *m_deleteSelected = nullptr;
    QPushButton *m_exitBatch = nullptr;
    QToolButton *m_moreSelected = nullptr;
    QMenu *m_batchPlaylistMenu = nullptr;
    QMenu *m_batchMoreMenu = nullptr;
    QAction *m_moreClear = nullptr;
    QAction *m_moreFavorite = nullptr;
    QAction *m_moreUnfavorite = nullptr;
    QAction *m_moreDownload = nullptr;
    QAction *m_moreDelete = nullptr;
    QList<QPair<int, QString>> m_playlistItems;
    QSet<QString> m_selectedIdentities;
    QSet<qint64> m_favoriteIds;
    int m_contextRow = -1;
    bool m_removable = false;
    bool m_batchMode = false;
    DownloadActionMode m_downloadMode = DownloadAction;
};

} // namespace ui
