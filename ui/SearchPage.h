#pragma once

#include "core/MusicSource.h"
#include "core/SearchAggregator.h"
#include "core/Song.h"

#include <QWidget>

#include <QHash>
#include <QSet>
#include <QStringList>

class QLabel;
class QPushButton;
class QComboBox;
class QListWidget;
class QStackedWidget;
class QTimer;
class QVBoxLayout;
class QFileInfo;
class QHideEvent;

namespace ui {

class SongListView;

} // namespace ui

namespace core {
class LibraryService;
class MusicSourceRegistry;
class SearchCache;
class SearchService;
}

namespace ui {

class SearchPage : public QWidget
{
    Q_OBJECT
public:
    explicit SearchPage(QWidget *parent = nullptr);
    ~SearchPage() override;

    void setSourceProvider(core::MusicSource *source, core::LibraryService *library);
    void setSourceRegistry(core::MusicSourceRegistry *registry);
    void setOnlineSourceEnabled(core::SourceId sourceId, bool enabled);
    void setLocalSongs(const QList<core::Song> &songs);
    void performSearch(const QString &query);
    void previewSearchText(const QString &text);
    void showSearchAssistant();
    void moveSearchAssistantSelection(int direction);
    QString resolvedSearchText(const QString &fallback) const;
    void refreshLocalResults();
    void refreshOnlineCovers();
    void resetAfterCacheClear();
    void setSearchScope(core::SearchScope scope);
    void setSearchCategory(core::SearchCategory category);
    void setSortMode(core::SearchSortMode mode);
    void setPreferredSource(core::SourceId source);
    core::SearchScope searchScope() const { return m_scope; }
    core::SearchCategory searchCategory() const { return m_category; }
    QList<core::Song> currentSongs() const;
    QList<core::SearchResultGroup> onlineResultGroups() const;
    QList<core::SearchResultItem> onlineResultItems() const { return m_onlineItems; }
    core::SearchSourceState sourceState(core::SourceId source) const;
    QStringList assistantQueries() const;
    void setPlaylistMenuItems(const QList<QPair<int, QString>> &items);

signals:
    void playRequested(const QList<core::Song> &songs, int index);
    void artistClicked(const QString &artist);
    void albumClicked(const QString &album, const QString &artist);
    void onlineResultActivated(int source, int type, const QString &remoteId,
                               const QString &title);
    void searchTextChosen(const QString &text);
    void defaultSearchTextReady(const QString &text);
    void heartRequested(int row);
    void addToPlaylistRequested(int row, int playlistId);
    void removeFromPlaylistRequested(int row);
    void deleteFromLibraryRequested(int row);

protected:
    void hideEvent(QHideEvent *event) override;

private:
    void beginSearch(const QString &query, bool recordHistory);
    void loadOnlinePage(int offset, core::SourceId onlySource = core::SourceId::Local);
    QList<core::MusicSource *> activeOnlineSources(
        core::SourceId onlySource = core::SourceId::Local) const;
    void mergeOnlineResponse(const core::SearchResponse &response);
    void removeOnlineItems(const QSet<QString> &identities);
    void startLocalSearch();
    void renderLocalGroups();
    void renderGenericResults();
    void rebuildAggregatedOnlineResults();
    void requestNextOnlinePages();
    void updateOnlineLoadingState();
    void showCurrentResultPage();
    void requestDiscovery();
    void requestHotSearch(core::MusicSource *source, quint64 generation);
    void retryHotSearch(core::SourceId sourceId);
    void requestSuggestions();
    void renderAssistant();
    void cancelActiveSearch();
    void updateOnlineHeader();
    void ensureCover(const core::Song &song);
    void startCoverDownloads();
    void startAlbumCoverLookups();
    void setOnlineCover(const QString &stableIdentity, const QString &path);

    struct AlbumCoverRequest
    {
        core::SourceId source = core::SourceId::Local;
        QString albumId;
        quint64 generation = 0;
    };

    QLabel *m_title = nullptr;
    QWidget *m_legacyTabs = nullptr;
    QComboBox *m_scopeCombo = nullptr;
    QComboBox *m_categoryCombo = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QStackedWidget *m_stack = nullptr;
    SongListView *m_songList = nullptr;
    QVBoxLayout *m_artistLayout = nullptr;
    QVBoxLayout *m_albumLayout = nullptr;
    QVBoxLayout *m_genericLayout = nullptr;
    QList<core::Song> m_results;
    QList<core::Song> m_localSnapshot;
    QList<core::Song> m_onlineSongs;
    QList<core::Song> m_genericSongs;
    QList<core::SearchResultItem> m_onlineItems;
    QList<core::SearchResultGroup> m_onlineGroups;
    QString m_query;
    QString m_assistantInput;
    int m_onlineOffset = 0;
    int m_onlinePageSize = 30;
    quint64 m_searchGeneration = 0;
    quint64 m_localRequestGeneration = 0;
    quint64 m_assistantGeneration = 0;
    core::SearchScope m_scope = core::SearchScope::All;
    core::SearchCategory m_category = core::SearchCategory::Songs;
    core::SearchSortMode m_sortMode = core::SearchSortMode::Comprehensive;
    core::SourceId m_preferredSource = core::SourceId::Local;
    bool m_onlineLoading = false;
    bool m_searchAssistantVisible = false;
    int m_sourceTimeoutMs = 15000;
    QHash<int, core::SearchSourceState> m_sourceStates;
    QSet<QString> m_albumCoverLookups;
    QSet<QString> m_coverDownloads;
    QList<core::Song> m_coverDownloadQueue;
    QList<AlbumCoverRequest> m_albumCoverQueue;
    int m_coverDownloadsActive = 0;
    int m_albumCoverLookupsActive = 0;
    QSet<int> m_enabledSourceIds;
    core::MusicSource *m_source = nullptr;
    core::MusicSourceRegistry *m_registry = nullptr;
    core::LibraryService *m_lib = nullptr;
    core::SearchService *m_localSearch = nullptr;
    core::SearchCache *m_searchCache = nullptr;
    QTimer *m_suggestionTimer = nullptr;
    QHash<int, QList<core::SearchSuggestion>> m_suggestions;
    QHash<int, QList<core::HotSearchTerm>> m_hotTerms;
    QHash<int, QString> m_hotErrors;
    QLabel *m_onlineHeader = nullptr;
    SongListView *m_onlineList = nullptr;
    QLabel *m_genericSongsHeader = nullptr;
    SongListView *m_genericSongList = nullptr;
    QPushButton *m_retryNetease = nullptr;
    QPushButton *m_retryQq = nullptr;
    QPushButton *m_hotRetryNetease = nullptr;
    QPushButton *m_hotRetryQq = nullptr;
    QWidget *m_discoveryPanel = nullptr;
    QListWidget *m_historyList = nullptr;
    QListWidget *m_neteaseHotList = nullptr;
    QListWidget *m_qqHotList = nullptr;
    QListWidget *m_assistantList = nullptr;
    QLabel *m_assistantHeader = nullptr;
    QPushButton *m_clearHistory = nullptr;
};

} // namespace ui
