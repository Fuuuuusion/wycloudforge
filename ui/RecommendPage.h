#pragma once

#include "core/Song.h"

#include <QWidget>

class QHBoxLayout;
class QLabel;

namespace core {
class MusicSource;
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
    void refresh();

signals:
    void playRequested(const QList<core::Song> &songs, int index);
    void openPlaylistRequested(qint64 id, const QString &name);
    void loginRequested();

private:
    void loadCache();
    void buildDaily(const QJsonArray &songs);
    void updateDailySongCover(qint64 songId, const QString &path);
    void buildPlaylists(const QJsonArray &playlists);
    void saveCache(const QJsonArray &songs, const QJsonArray &playlists);
    void showEmpty();

    core::MusicSource *m_source = nullptr;
    core::LibraryService *m_lib = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QHBoxLayout *m_playlistRow = nullptr;
    SongListView *m_list = nullptr;
};

} // namespace ui
