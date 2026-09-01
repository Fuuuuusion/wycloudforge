#pragma once

#include "core/Song.h"

#include <QHash>
#include <QJsonArray>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QPushButton;
class QScrollArea;

namespace core {
class MusicSource;
class MusicSourceRegistry;
class LibraryService;
}

namespace ui {

class SongListView;

class RecommendPage : public QWidget
{
    Q_OBJECT
public:
    explicit RecommendPage(QWidget *parent = nullptr);

    void setSourceProvider(core::MusicSource *source, core::LibraryService *library);
    void setSourceRegistry(core::MusicSourceRegistry *registry);
    void setActiveSource(core::SourceId sourceId);
    void setSourceAvailable(core::SourceId sourceId, bool available);
    void refresh(bool forceNetwork = false);
    void resetAfterCacheClear();
    core::SourceId activeSourceId() const;
    QList<core::Song> currentSongs() const;
    void setPlaylistMenuItems(const QList<QPair<int, QString>> &items);

signals:
    void playRequested(const QList<core::Song> &songs, int index);
    void openPlaylistRequested(int sourceId, const QString &remoteId, const QString &name);
    void loginRequested();
    void heartRequested(int row);
    void addToPlaylistRequested(int row, int playlistId);
    void sourceActivationRequested(int sourceId);
    void refreshStateChanged(bool busy, const QString &message);

private:
    QString cachePath(core::SourceId sourceId) const;
    void loadCache(core::MusicSource *source);
    void buildDaily(const QJsonArray &songs, core::MusicSource *source);
    void buildPlaylists(const QJsonArray &playlists, core::MusicSource *source);
    void saveCache(const QJsonArray &songs, const QJsonArray &playlists, core::MusicSource *source);
    void showEmpty();
    void updateSourceButtons();

    core::MusicSource *m_source = nullptr;
    core::MusicSourceRegistry *m_registry = nullptr;
    core::LibraryService *m_lib = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QHBoxLayout *m_playlistRow = nullptr;
    QWidget *m_playlistHost = nullptr;
    QScrollArea *m_playlistScroll = nullptr;
    QPushButton *m_neteaseButton = nullptr;
    QPushButton *m_qqButton = nullptr;
    SongListView *m_list = nullptr;
    QHash<int, QJsonArray> m_songPayloads;
    QHash<int, QJsonArray> m_playlistPayloads;
    QHash<int, int> m_playlistOffsets;
    int m_requestGeneration = 0;
    bool m_neteaseAvailable = false;
    bool m_qqAvailable = false;
};

} // namespace ui
