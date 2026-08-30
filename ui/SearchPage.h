#pragma once

#include "core/MusicSource.h"
#include "core/Song.h"

#include <QWidget>

#include <QHash>
#include <QSet>

class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class QFileInfo;

namespace ui {

class SongListView;

} // namespace ui

namespace core {
class LibraryService;
class MusicSourceRegistry;
class SearchService;
}

namespace ui {

class SearchPage : public QWidget
{
    Q_OBJECT
public:
    explicit SearchPage(QWidget *parent = nullptr);

    void setSourceProvider(core::MusicSource *source, core::LibraryService *library);
    void setSourceRegistry(core::MusicSourceRegistry *registry);
    void setOnlineSourceEnabled(core::SourceId sourceId, bool enabled);
    void setLocalSongs(const QList<core::Song> &songs);
    void performSearch(const QString &query);
    void refreshLocalResults();
    void refreshOnlineCovers();
    QList<core::Song> currentSongs() const;
    void setPlaylistMenuItems(const QList<QPair<int, QString>> &items);

signals:
    void playRequested(const QList<core::Song> &songs, int index);
    void artistClicked(const QString &artist);
    void albumClicked(const QString &album, const QString &artist);
    void heartRequested(int row);
    void addToPlaylistRequested(int row, int playlistId);
    void removeFromPlaylistRequested(int row);
    void deleteFromLibraryRequested(int row);

private:
    void loadOnlinePage(int offset);
    void startLocalSearch();
    void renderLocalGroups();
    void cancelActiveSearch();
    void updateOnlineHeader();
    void ensureCover(const core::Song &song);
    void setOnlineCover(const QString &stableIdentity, const QString &path);

    QLabel *m_title = nullptr;
    QStackedWidget *m_stack = nullptr;
    SongListView *m_songList = nullptr;
    QVBoxLayout *m_artistLayout = nullptr;
    QVBoxLayout *m_albumLayout = nullptr;
    QList<core::Song> m_results;
    QList<core::Song> m_onlineSongs;
    QString m_query;
    int m_onlineOffset = 0;
    int m_onlinePageSize = 30;
    quint64 m_searchGeneration = 0;
    quint64 m_localRequestGeneration = 0;
    bool m_onlineLoading = false;
    int m_sourceTimeoutMs = 15000;
    QHash<int, core::SearchSourceState> m_sourceStates;
    QSet<QString> m_albumCoverLookups;
    QSet<QString> m_coverDownloads;
    QSet<int> m_enabledSourceIds;
    core::MusicSource *m_source = nullptr;
    core::MusicSourceRegistry *m_registry = nullptr;
    core::LibraryService *m_lib = nullptr;
    core::SearchService *m_localSearch = nullptr;
    QLabel *m_onlineHeader = nullptr;
    SongListView *m_onlineList = nullptr;
    QPushButton *m_onlineMore = nullptr;
};

} // namespace ui
