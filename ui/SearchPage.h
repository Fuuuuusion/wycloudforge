#pragma once

#include "core/MusicSource.h"
#include "core/Song.h"

#include <QWidget>

class QLabel;
class QStackedWidget;
class QVBoxLayout;
class QFileInfo;

namespace ui {

class SongListView;

} // namespace ui

namespace core {
class LibraryService;
}

namespace ui {

class SearchPage : public QWidget
{
    Q_OBJECT
public:
    explicit SearchPage(QWidget *parent = nullptr);

    void setSourceProvider(core::MusicSource *source, core::LibraryService *library);
    void performSearch(const QList<core::Song> &allSongs, const QString &query);
    QList<core::Song> currentSongs() const { return m_results; }

signals:
    void playRequested(const QList<core::Song> &songs, int index);
    void artistClicked(const QString &artist);
    void albumClicked(const QString &album, const QString &artist);
    void heartRequested(int row);
    void addToPlaylistRequested(int row, int playlistId);
    void removeFromPlaylistRequested(int row);
    void deleteFromLibraryRequested(int row);

private:
    void ensureCover(const core::Song &song);

    QLabel *m_title = nullptr;
    QStackedWidget *m_stack = nullptr;
    SongListView *m_songList = nullptr;
    QVBoxLayout *m_artistLayout = nullptr;
    QVBoxLayout *m_albumLayout = nullptr;
    QList<core::Song> m_results;
    QList<core::Song> m_onlineSongs;
    core::MusicSource *m_source = nullptr;
    core::LibraryService *m_lib = nullptr;
    QLabel *m_onlineHeader = nullptr;
    SongListView *m_onlineList = nullptr;
};

} // namespace ui
