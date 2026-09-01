#pragma once

#include "core/Song.h"

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
    void refresh();
    QList<core::Song> currentSongs() const;
    void setPlaylistMenuItems(const QList<QPair<int, QString>> &items);

signals:
    void playRequested(const QList<core::Song> &songs, int index);
    void openPlaylistRequested(int sourceId, const QString &remoteId, const QString &name);
    void loginRequested();
    void heartRequested(int row);
    void addToPlaylistRequested(int row, int playlistId);
    void sourceActivationRequested(int sourceId);

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
    int m_requestGeneration = 0;
    bool m_neteaseAvailable = false;
    bool m_qqAvailable = false;
};

} // namespace ui
