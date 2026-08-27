#include "SideBar.h"

#include "ui/SvgIcon.h"

#include <QButtonGroup>
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

    addNavButton(QStringLiteral("发现音乐"), QStringLiteral(":/icons/icon-music.svg"), DiscoverPage);
    addNavButton(QStringLiteral("音乐库"), QStringLiteral(":/icons/icon-library.svg"), LibraryPage);
    addNavButton(QStringLiteral("正在播放"), QStringLiteral(":/icons/icon-lyrics.svg"), PlayingPage);
    addNavButton(QStringLiteral("在线音乐"), QStringLiteral(":/icons/icon-cloud.svg"), OnlinePageId);

    auto *sectionTitle = new QLabel(QStringLiteral("我的歌单"), this);
    sectionTitle->setProperty("class", "sectionTitle");
    sectionTitle->setContentsMargins(20, 10, 0, 4);
    layout->addWidget(sectionTitle);

    m_playlistSection = new QWidget(this);
    m_playlistLayout = new QVBoxLayout(m_playlistSection);
    m_playlistLayout->setContentsMargins(0, 0, 0, 0);
    m_playlistLayout->setSpacing(1);
    layout->addWidget(m_playlistSection);
    layout->addStretch(1);

    auto *createBtn = new QPushButton(QStringLiteral("创建歌单"), this);
    createBtn->setObjectName("createPlaylistBtn");
    createBtn->setIcon(makeSvgIcon(QStringLiteral(":/icons/icon-plus.svg")));
    createBtn->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;background:rgba(255,255,255,0.05);color:#9A9AA5;"
        "padding:7px 10px;border-radius:6px;}"
        "QPushButton:hover{background:rgba(236,65,65,0.16);color:#EC4141;}"));
    createBtn->setCursor(Qt::PointingHandCursor);
    connect(createBtn, &QPushButton::clicked, this, &SideBar::createPlaylistRequested);
    layout->addWidget(createBtn, 0, Qt::AlignHCenter);
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
    static_cast<QVBoxLayout *>(layout())->insertWidget(layout()->count() - 1, btn);
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
        btn->setIcon(QIcon(item.favorite ? QStringLiteral(":/icons/icon-heart-fill.svg")
                                         : QStringLiteral(":/icons/icon-music.svg")));
        btn->setIconSize(QSize(16, 16));
        btn->setText(item.name);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(item.name);
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
