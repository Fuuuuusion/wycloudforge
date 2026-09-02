#include "SideBar.h"

#include "ui/ThemeManager.h"

#include "ui/CoverProvider.h"
#include "ui/SourceIcons.h"
#include "ui/SvgIcon.h"

#include <QButtonGroup>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace ui {
namespace {

const char kPlaylistStyle[] =
    "QPushButton{text-align:left;border:none;background:@pageBackground;color:@textSecondary;"
    "font-size:14px;padding:6px 10px;border-radius:6px;}"
    "QPushButton:hover{background:@surfaceAlt;color:@accentHover;}"
    "QPushButton:checked{background:@surfaceAlt;color:@accent;font-weight:600;}";

class NavButton : public QPushButton
{
public:
    NavButton(const QString &text, const QString &iconPath, QWidget *parent)
        : QPushButton(parent)
        , m_label(text)
        , m_iconPath(iconPath)
    {
        setCheckable(true);
        setFlat(true);
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover, true);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(84);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override
    {
        return { 240, 84 };
    }

protected:
    void enterEvent(QEnterEvent *event) override
    {
        m_hover = true;
        update();
        QPushButton::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hover = false;
        update();
        QPushButton::leaveEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool active = m_hover || isChecked();
        QFont font = painter.font();
        font.setFamily(QStringLiteral("Microsoft YaHei UI"));
        font.setPointSize(15);
        font.setWeight(active ? QFont::DemiBold : QFont::Normal);
        painter.setFont(font);
        const QFontMetrics metrics(font);
        const int iconSize = 24;
        const int gap = 10;
        const int totalWidth = iconSize + gap + metrics.horizontalAdvance(m_label);
        const int left = qMax(8, (width() - totalWidth) / 2);
        const int top = (height() - iconSize) / 2;

        QPixmap icon = makeSvgIcon(m_iconPath, iconSize).pixmap(iconSize, iconSize);
        if (active && !icon.isNull()) {
            QPixmap tinted(icon.size());
            tinted.fill(Qt::transparent);
            QPainter iconPainter(&tinted);
            iconPainter.drawPixmap(0, 0, icon);
            iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            iconPainter.fillRect(tinted.rect(), themeColor(ThemeColor::Accent));
            icon = tinted;
        }
        if (!icon.isNull())
            painter.drawPixmap(left, top, icon);

        const QRect textRect(left + iconSize + gap, 0, metrics.horizontalAdvance(m_label), height());
        if (active) {
            painter.setPen(QPen(themeColor(ThemeColor::Accent), 1));
        } else {
            painter.setPen(QPen(themeColor(ThemeColor::TextSecondary), 1));
        }
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_label);
    }

private:
    QString m_label;
    QString m_iconPath;
    bool m_hover = false;
};

class PlaylistButton final : public QPushButton
{
public:
    explicit PlaylistButton(core::SourceId source, QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        if (source == core::SourceId::Local)
            return;
        m_sourceBadge = new QLabel(this);
        m_sourceBadge->setObjectName(QStringLiteral("cloudPlaylistSourceBadge"));
        m_sourceBadge->setFixedSize(20, 20);
        m_sourceBadge->setPixmap(sourceIcon(source).pixmap(18, 18));
        m_sourceBadge->setAlignment(Qt::AlignCenter);
        m_sourceBadge->setToolTip(sourceDisplayName(source));
        m_sourceBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_sourceBadge->show();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QPushButton::resizeEvent(event);
        if (m_sourceBadge)
            m_sourceBadge->move(width() - m_sourceBadge->width() - 10,
                                (height() - m_sourceBadge->height()) / 2);
    }

private:
    QLabel *m_sourceBadge = nullptr;
};

} // namespace

SideBar::SideBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("sidebar");
    setFixedWidth(240);
    setAttribute(Qt::WA_StyledBackground, true);
    setThemedStyleSheet(this, QStringLiteral("QWidget#sidebar{background:@pageBackground;border:none;}"));

    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);
    m_playlistGroup = new QButtonGroup(this);
    m_playlistGroup->setExclusive(true);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    auto *navigationScroll = new QScrollArea(this);
    navigationScroll->setObjectName(QStringLiteral("sidebarNavigationScroll"));
    navigationScroll->setWidgetResizable(true);
    navigationScroll->setFrameShape(QFrame::NoFrame);
    navigationScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setThemedStyleSheet(navigationScroll, QStringLiteral(
        "QScrollArea{background:@pageBackground;border:none;}"
        "QScrollArea>QWidget>QWidget{background:@pageBackground;}"));
    auto *content = new QWidget(navigationScroll);
    m_contentLayout = new QVBoxLayout(content);
    m_contentLayout->setContentsMargins(0, 0, 0, 10);
    m_contentLayout->setSpacing(1);
    navigationScroll->setWidget(content);
    rootLayout->addWidget(navigationScroll);

    addNavButton(QStringLiteral("推荐"), QStringLiteral(":/icons/icon-music.svg"), RecommendPage);
    addNavButton(QStringLiteral("收藏"), QStringLiteral(":/icons/icon-heart.svg"), FavoritesPage);
    addNavButton(QStringLiteral("本地歌单"), QStringLiteral(":/icons/icon-library.svg"), LocalLibraryPage);
    addNavButton(QStringLiteral("自建歌单"), QStringLiteral(":/icons/icon-folder.svg"), SelfPlaylistsPage);

    // 自建歌单区头部:标题 + 右侧"＋"
    auto *sectionHead = new QWidget(this);
    auto *headLayout = new QHBoxLayout(sectionHead);
    headLayout->setContentsMargins(12, 10, 12, 4);
    headLayout->setSpacing(4);
    auto *sectionTitle = new QLabel(QStringLiteral("自建歌单"), sectionHead);
    setThemedStyleSheet(sectionTitle, QStringLiteral(
        "color:@textSecondary;font-size:13px;background:transparent;"));
    headLayout->addWidget(sectionTitle);
    headLayout->addStretch(1);
    auto *plusBtn = new QPushButton(sectionHead);
    plusBtn->setObjectName("createPlaylistPlus");
    plusBtn->setIcon(makeSvgIcon(QStringLiteral(":/icons/icon-plus.svg"), 18));
    plusBtn->setIconSize(QSize(18, 18));
    plusBtn->setFixedSize(22, 22);
    plusBtn->setCursor(Qt::PointingHandCursor);
    plusBtn->setToolTip(QStringLiteral("创建歌单"));
    setThemedStyleSheet(plusBtn, QStringLiteral(
        "QPushButton{border:none;background:@pageBackground;border-radius:11px;}"
        "QPushButton:hover{background:@surfaceAlt;}"));
    connect(plusBtn, &QPushButton::clicked, this, &SideBar::createPlaylistRequested);
    headLayout->addWidget(plusBtn);
    m_contentLayout->addWidget(sectionHead);

    m_playlistSection = new QWidget(this);
    m_playlistLayout = new QVBoxLayout(m_playlistSection);
    m_playlistLayout->setContentsMargins(0, 0, 0, 0);
    m_playlistLayout->setSpacing(1);
    m_playlistLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    auto *playlistScroll = new QScrollArea(this);
    playlistScroll->setObjectName(QStringLiteral("sidebarPlaylistScroll"));
    playlistScroll->setWidgetResizable(true);
    playlistScroll->setFrameShape(QFrame::NoFrame);
    playlistScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setThemedStyleSheet(playlistScroll, QStringLiteral(
        "QScrollArea{background:@pageBackground;border:none;}"
        "QScrollArea>QWidget>QWidget{background:@pageBackground;}"));
    playlistScroll->setWidget(m_playlistSection);
    playlistScroll->setMinimumHeight(144);
    m_contentLayout->addWidget(playlistScroll, 1);
}

void SideBar::addNavButton(const QString &text, const QString &icon, int pageId)
{
    auto *btn = new NavButton(text, icon, this);
    m_navGroup->addButton(btn, pageId);
    m_contentLayout->addWidget(btn);
    connect(btn, &QPushButton::clicked, this, [this, pageId] { emit pageRequested(pageId); });
}

void SideBar::setPlaylists(const QList<PlaylistItem> &items, int activeId,
                           const QString &activeCloudIdentity)
{
    m_activePlaylist = activeId;
    m_activeCloudIdentity = activeCloudIdentity;
    rebuildPlaylistButtons(items, activeId, activeCloudIdentity);
}

void SideBar::rebuildPlaylistButtons(const QList<PlaylistItem> &items, int activeId,
                                     const QString &activeCloudIdentity)
{
    // 每次重建时连同旧的 stretch 一起清空。此前只删除按钮、却不断追加
    // addStretch()，多次刷新后新歌单按钮会被旧 stretch 推到可视区域之外。
    while (QLayoutItem *item = m_playlistLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_playlistIds.clear();

    int cloudButtonId = -2;
    for (const PlaylistItem &item : items) {
        auto *btn = new PlaylistButton(item.cloud ? item.source : core::SourceId::Local,
                                       m_playlistSection);
        btn->setProperty("class", "playlistBtn");
        QString style = QLatin1String(kPlaylistStyle);
        if (item.cloud)
            style.replace(QStringLiteral("padding:6px 10px"),
                          QStringLiteral("padding:6px 34px 6px 10px"));
        setThemedStyleSheet(btn, style);
        QPixmap cover;
        if (!item.coverPath.isEmpty() && QFileInfo::exists(item.coverPath))
            cover = QPixmap(item.coverPath);
        if (cover.isNull())
            cover = CoverProvider::placeholder(item.name, 48, 6);
        btn->setIcon(QIcon(cover.scaled(48, 48, Qt::KeepAspectRatioByExpanding,
                                        Qt::SmoothTransformation)));
        btn->setIconSize(QSize(48, 48));
        btn->setFixedHeight(64);
        btn->setText(item.name);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(item.description.isEmpty() ? item.name : item.name + QStringLiteral("\n") + item.description);
        const int groupId = item.cloud ? cloudButtonId-- : item.id;
        m_playlistGroup->addButton(btn, groupId);
        m_playlistIds.append(groupId);
        m_playlistLayout->addWidget(btn);
        if (item.cloud) {
            connect(btn, &QPushButton::clicked, this,
                    [this, source = item.source, remoteId = item.remoteId, name = item.name] {
                emit cloudPlaylistSelected(int(source), remoteId, name);
            });
            if (item.stableIdentity() == activeCloudIdentity)
                btn->setChecked(true);
        } else {
            connect(btn, &QPushButton::clicked, this,
                    [this, id = item.id] { emit playlistSelected(id); });
        }
    }

    if (activeCloudIdentity.isEmpty()) {
        if (auto *btn = m_playlistGroup->button(activeId))
            btn->setChecked(true);
    }
}

void SideBar::setActivePage(int pageId)
{
    if (auto *btn = m_navGroup->button(pageId))
        btn->setChecked(true);
}

} // namespace ui
