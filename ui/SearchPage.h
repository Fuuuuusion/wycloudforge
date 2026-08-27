#pragma once

#include "core/Song.h"

#include <QWidget>

class QLabel;
class QStackedWidget;
class QVBoxLayout;

namespace ui {

class SongListView;

class SearchPage : public QWidget
{
    Q_OBJECT
public:
    explicit SearchPage(QWidget *parent = nullptr);

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
    QLabel *m_title = nullptr;
    QStackedWidget *m_stack = nullptr;
    SongListView *m_songList = nullptr;
    QVBoxLayout *m_artistLayout = nullptr;
    QVBoxLayout *m_albumLayout = nullptr;
    QList<core::Song> m_results;
};

} // namespace ui

