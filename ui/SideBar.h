#pragma once

#include "core/Song.h"

#include <QWidget>

class QButtonGroup;
class QPushButton;
class QVBoxLayout;

namespace ui {

class SideBar : public QWidget
{
    Q_OBJECT
public:
    enum PageId { RecommendPage = 0, FavoritesPage = 1, LocalLibraryPage = 2, SelfPlaylistsPage = 3 };

    struct PlaylistItem
    {
        int id = -1;
        QString name;
        QString coverPath;
        QString description;
        bool favorite = false;
        bool cloud = false;
        core::SourceId source = core::SourceId::Local;
        QString remoteId;

        QString stableIdentity() const
        {
            return cloud ? QStringLiteral("%1:%2").arg(int(source)).arg(remoteId)
                         : QStringLiteral("local:%1").arg(id);
        }
    };

    explicit SideBar(QWidget *parent = nullptr);

    void setPlaylists(const QList<PlaylistItem> &items, int activeId = -1,
                      const QString &activeCloudIdentity = {});
    void setActivePage(int pageId);

signals:
    void pageRequested(int pageId);
    void playlistSelected(int playlistId);
    void cloudPlaylistSelected(int sourceId, const QString &remoteId, const QString &name);
    void createPlaylistRequested();

private:
    void addNavButton(const QString &text, const QString &icon, int pageId);
    void rebuildPlaylistButtons(const QList<PlaylistItem> &items, int activeId,
                                const QString &activeCloudIdentity);

    QButtonGroup *m_navGroup = nullptr;
    QButtonGroup *m_playlistGroup = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
    QVBoxLayout *m_playlistLayout = nullptr;
    QWidget *m_playlistSection = nullptr;
    QList<int> m_playlistIds;
    int m_activePlaylist = -1;
    QString m_activeCloudIdentity;
};

} // namespace ui
