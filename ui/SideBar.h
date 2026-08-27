#pragma once

#include <QWidget>

class QButtonGroup;
class QPushButton;
class QVBoxLayout;

namespace ui {

class SideBar : public QWidget
{
    Q_OBJECT
public:
    enum PageId { DiscoverPage = 0, LibraryPage = 1, PlayingPage = 3, OnlinePageId = 5 };

    struct PlaylistItem
    {
        int id = -1;
        QString name;
        bool favorite = false;
    };

    explicit SideBar(QWidget *parent = nullptr);

    void setPlaylists(const QList<PlaylistItem> &items, int activeId = -1);
    void setActivePage(int pageId);

signals:
    void pageRequested(int pageId);
    void playlistSelected(int playlistId);
    void createPlaylistRequested();

private:
    void addNavButton(const QString &text, const QString &icon, int pageId);
    void rebuildPlaylistButtons(const QList<PlaylistItem> &items, int activeId);

    QButtonGroup *m_navGroup = nullptr;
    QButtonGroup *m_playlistGroup = nullptr;
    QVBoxLayout *m_playlistLayout = nullptr;
    QWidget *m_playlistSection = nullptr;
    QList<int> m_playlistIds;
    int m_activePlaylist = -1;
};

} // namespace ui
