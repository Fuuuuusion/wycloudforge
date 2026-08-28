#include "SideBar.h"

#include "ui/CoverProvider.h"
#include "ui/SvgIcon.h"

#include <QButtonGroup>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {
namespace {

const char kPlaylistStyle[] =
    "QPushButton{text-align:left;border:none;background:transparent;color:#9A9AA5;"
    "font-size:14px;padding:6px 10px;border-radius:6px;}"
    "QPushButton:hover{background:transparent;color:#FF5A5A;}"
    "QPushButton:checked{background:transparent;color:#EC4141;font-weight:600;}";

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
        setMinimumHeight(44);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override
    {
        return { 200, 44 };
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
        const int iconSize = 20;
        const int gap = 8;
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
            iconPainter.fillRect(tinted.rect(), QColor(0xEC, 0x41, 0x41));
            icon = tinted;
        }
        if (!icon.isNull())
            painter.drawPixmap(left, top, icon);

        const QRect textRect(left + iconSize + gap, 0, metrics.horizontalAdvance(m_label), height());
        if (active) {
            QLinearGradient gradient(textRect.topLeft(), textRect.topRight());
            gradient.setColorAt(0.0, QColor(0xEC, 0x41, 0x41));
            gradient.setColorAt(1.0, QColor(0xFF, 0x9A, 0x76));
            painter.setPen(QPen(QBrush(gradient), 1));
        } else {
            painter.setPen(QPen(QColor(0x9A, 0x9A, 0xA5), 1));
        }
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_label);
    }

private:
    QString m_label;
    QString m_iconPath;
    bool m_hover = false;
};

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
    headLayout->setContentsMargins(12, 10, 12, 4);
    headLayout->setSpacing(4);
    headLayout->addStretch(1);
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
    auto *btn = new NavButton(text, icon, this);
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
