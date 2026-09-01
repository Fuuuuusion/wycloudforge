#pragma once

#include "core/SearchAggregator.h"
#include "core/Song.h"

#include <QTableView>
#include <QSet>

class QMenu;
class QMouseEvent;
class QResizeEvent;
class QShowEvent;
class QHideEvent;
class QAction;
class QLabel;
class QPushButton;
class QToolButton;
class QTimer;

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
    void setSearchResultGroups(const QList<core::SearchResultGroup> &groups,
                               qint64 playingId = -1);
    bool updateSong(const core::Song &song);
    QList<core::Song> songs() const;
    void setPlayingId(qint64 playingId);
    void setPlaybackActive(bool active);
    bool playbackActive() const { return m_playbackActive; }
    void setRemovable(bool removable);
    void setHighlightQuery(const QString &query);
    void setPlaylistMenuItems(const QList<QPair<int, QString>> &items);
    void setFavoriteIds(const QSet<qint64> &ids);
    void setDownloadingIdentities(const QSet<QString> &identities);
    void refreshLibraryState(core::LibraryService *library);
    void setDownloadActionMode(DownloadActionMode mode);
    QList<core::Song> selectedSongs() const;
    bool batchMode() const { return m_batchMode; }
    int hoveredRow() const;

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
    void sourceActivated(int row, const core::Song &song);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void setBatchMode(bool enabled);
    void updateBatchButtons();
    void updateBatchLayout();
    void rebuildBatchPlaylistMenu();
    void toggleRowSelection(int row);
    void resetPointerState();
    void updateSafeAreaRows();
    bool pointHitsSongContent(const QModelIndex &index, const QPoint &point) const;
    void verifyPointerStillOverViewport();
    void showSourcePicker(int row, const QRect &cellRect);
    void closeSourcePicker();
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
    QSet<QString> m_downloadingIdentities;
    QSet<qint64> m_favoriteIds;
    int m_contextRow = -1;
    int m_pendingPlayRow = -1;
    QTimer *m_pointerGuardTimer = nullptr;
    QWidget *m_sourcePopup = nullptr;
    int m_sourcePopupRow = -1;
    bool m_removable = false;
    bool m_batchMode = false;
    bool m_favoriteStateInitialized = false;
    bool m_playbackActive = false;
    DownloadActionMode m_downloadMode = DownloadAction;
};

} // namespace ui
