#pragma once

#include "core/MusicSource.h"
#include "core/Song.h"

#include <QWidget>

class QGridLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace ui {

class SongListView;

} // namespace ui

namespace core {
class LibraryService;
}

namespace ui {

class OnlinePage : public QWidget
{
    Q_OBJECT
public:
    explicit OnlinePage(QWidget *parent = nullptr);

    void setSourceProvider(core::MusicSource *source, core::LibraryService *library);
    void setLoginInfo(const QString &nickname);
    void refresh();

signals:
    void playRequested(const QList<core::Song> &songs, int index);
    void openPlaylistRequested(qint64 id, const QString &name);
    void openAlbumRequested(qint64 id);
    void openArtistRequested(qint64 id);
    void loginRequested();
    void logoutRequested();

private:
    void buildRecommendTab();
    void buildRankTab();
    void buildPlaylistSquareTab();
    void buildMyPlaylistsTab();
    void buildFmTab();
    void loadSongs(SongListView *view, const QJsonArray &arr);
    void ensureCover(const core::Song &song);
    void refreshPlaylistSquare();

    core::MusicSource *m_source = nullptr;
    core::LibraryService *m_lib = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_loginBtn = nullptr;
    QStackedWidget *m_stack = nullptr;
    SongListView *m_dailyList = nullptr;
    SongListView *m_rankList = nullptr;
    SongListView *m_squareList = nullptr;
    SongListView *m_mineList = nullptr;
    SongListView *m_fmList = nullptr;
    QGridLayout *m_squareGrid = nullptr;
    QGridLayout *m_mineGrid = nullptr;
    QListWidget *m_topListWidget = nullptr;
    QString m_squareCat = QStringLiteral("华语");
};

} // namespace ui
