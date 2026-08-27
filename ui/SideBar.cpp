#include "SideBar.h"

#include "ui/CoverProvider.h"
#include "ui/SvgIcon.h"

#include <QButtonGroup>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {
namespace {

const char kNavStyle[] =
    "QPushButton{text-align:left;border:none;background:transparent;color:#9A9AA5;"
    "padding:8px 10px;border-radius:6px;}"
    "QPushButton:hover{background:rgba(255,255,255,0.08);color:#E8E8E8;}"
    "QPushButton:checked{background:rgba(236,65,65,0.16);color:#EC4141;font-weight:600;}";

const char kPlaylistStyle[] =
    "QPushButton{text-align:left;border:none;background:transparent;color:#9A9AA5;"
    "padding:7px 10px;border-radius:6px;}"
    "QPushButton:hover{background:rgba(255,255,255,0.08);color:#E8E8E8;}"
    "QPushButton:checked{background:rgba(236,65,65,0.16);color:#EC4141;font-weight:600;}";

} // namespace

SideBar::SideBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("sidebar");
    setFixedWidth(200);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("QWidget#sidebar{background:transparent;border:none;}"));

    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);
    m_playlistGroup = new QButtonGroup(this);
    m_playlistGroup->setExclusive(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 12, 0, 10);
    layout->setSpacing(1);

    addNavButton(QStringLiteral("推荐"), QStringLiteral(":/icons/icon-music.svg"), RecommendPage);
    addNavButton(QStringLiteral("收藏"), QStringLiteral(":/icons/icon-heart.svg"), FavoritesPage);
    addNavButton(QStringLiteral("本地歌单"), QStringLiteral(":/icons/icon-library.svg"), LocalLibraryPage);
    addNavButton(QStringLiteral("自建歌单"), QStringLiteral(":/icons/icon-folder.svg"), SelfPlaylistsPage);

    // 自建歌单区头部:标题 + 右侧"＋"
    auto *sectionHead = new QWidget(this);
    auto *headLayout = new QHBoxLayout(sectionHead);
    headLayout->setContentsMargins(20, 10, 12, 4);
    headLayout->setSpacing(4);
    auto *sectionTitle = new QLabel(QStringLiteral("自建歌单"), sectionHead);
    sectionTitle->setProperty("class", "sectionTitle");
    headLayout->addWidget(sectionTitle, 1);
    auto *plusBtn = new QPushButton(sectionHead);
    plusBtn->setObjectName("createPlaylistPlus");
    plusBtn->setIcon(makeSvgIcon(QStringLiteral(":/icons/icon-plus.svg"), 18));
    plusBtn->setIconSize(QSize(18, 18));
    plusBtn->setFixedSize(22, 22);
    plusBtn->setCursor(Qt::PointingHandCursor);
    plusBtn->setToolTip(QStringLiteral("创建歌单"));
    plusBtn->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;background:transparent;border-radius:11px;}"
        "QPushButton:hover{background:rgba(255,255,255,0.10);}"));
    connect(plusBtn, &QPushButton::clicked, this, &SideBar::createPlaylistRequested);
    headLayout->addWidget(plusBtn);
    layout->addWidget(sectionHead);

    m_playlistSection = new QWidget(this);
    m_playlistLayout = new QVBoxLayout(m_playlistSection);
    m_playlistLayout->setContentsMargins(0, 0, 0, 0);
    m_playlistLayout->setSpacing(1);
    layout->addWidget(m_playlistSection);
    layout->addStretch(1);
}

void SideBar::addNavButton(const QString &text, const QString &icon, int pageId)
{
    auto *btn = new QPushButton(text, this);
    btn->setProperty("class", "navBtn");
    btn->setStyleSheet(QLatin1String(kNavStyle));
    btn->setIcon(QIcon(icon));
    btn->setIconSize(QSize(18, 18));
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    m_navGroup->addButton(btn, pageId);
    static_cast<QVBoxLayout *>(layout())->addWidget(btn);
    connect(btn, &QPushButton::clicked, this, [this, pageId] { emit pageRequested(pageId); });
}

void SideBar::setPlaylists(const QList<PlaylistItem> &items, int activeId)
{
    m_activePlaylist = activeId;
    rebuildPlaylistButtons(items, activeId);
}

void SideBar::rebuildPlaylistButtons(const QList<PlaylistItem> &items, int activeId)
{
    qDeleteAll(m_playlistSection->findChildren<QPushButton *>());
    m_playlistIds.clear();

    for (const PlaylistItem &item : items) {
        auto *btn = new QPushButton(m_playlistSection);
        btn->setProperty("class", "playlistBtn");
        btn->setStyleSheet(QLatin1String(kPlaylistStyle));
        QPixmap cover;
        if (!item.coverPath.isEmpty() && QFileInfo::exists(item.coverPath))
            cover = QPixmap(item.coverPath);
        if (cover.isNull())
            cover = CoverProvider::placeholder(item.name, 28, 6);
        btn->setIcon(QIcon(cover.scaled(28, 28, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)));
        btn->setIconSize(QSize(28, 28));
        btn->setText(item.name);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(item.description.isEmpty() ? item.name : item.name + QStringLiteral("\n") + item.description);
        m_playlistGroup->addButton(btn, item.id);
        m_playlistIds.append(item.id);
        m_playlistLayout->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, [this, id = item.id] { emit playlistSelected(id); });
    }

    if (auto *btn = m_playlistGroup->button(activeId))
        btn->setChecked(true);
    m_playlistLayout->addStretch(1);
}

void SideBar::setActivePage(int pageId)
{
    if (auto *btn = m_navGroup->button(pageId))
        btn->setChecked(true);
}

} // namespace ui
