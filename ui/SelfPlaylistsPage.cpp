#include "SelfPlaylistsPage.h"

#include "ui/CoverCard.h"
#include "ui/CoverProvider.h"
#include "ui/SourceIcons.h"

#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace ui {
namespace {
class NewPlaylistCard : public CoverCard
{
    Q_OBJECT
public:
    explicit NewPlaylistCard(QWidget *parent = nullptr)
        : CoverCard(parent)
    {
        setFixedCardSize(132, 116);
        setFullCoverCard(true);
        setNewPlaylistCard(true);
        setText(QStringLiteral("新建歌单"), QString());
    }
};
}

SelfPlaylistsPage::SelfPlaylistsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("自建歌单"), this);
    title->setProperty("class", "pageTitle");
    layout->addWidget(title);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *scrollHost = new QWidget;
    m_grid = new QGridLayout(scrollHost);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(16);
    m_grid->setColumnStretch(0, 1);
    scroll->setWidget(scrollHost);
    layout->addWidget(scroll, 1);
}

void SelfPlaylistsPage::setPlaylists(const QList<core::PlaylistController::PlaylistInfo> &playlists)
{
    m_playlists = playlists;
    rebuild();
}

void SelfPlaylistsPage::setCloudPlaylists(const QList<core::OnlinePlaylist> &playlists)
{
    m_cloudPlaylists = playlists;
    rebuild();
}

void SelfPlaylistsPage::rebuild()
{
    while (QLayoutItem *item = m_grid->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    int col = 0;
    int row = 0;
    auto addCard = [&](CoverCard *card) {
        m_grid->addWidget(card, row, col);
        ++col;
        if (col >= 5) {
            col = 0;
            ++row;
        }
    };

    for (const auto &pl : m_playlists) {
        if (pl.id == 1)
            continue;
        auto *card = new CoverCard(this);
        card->setFixedCardSize(132, 116);
        card->setFullCoverCard(true);
        QPixmap cover;
        if (!pl.coverPath.isEmpty() && QFileInfo::exists(pl.coverPath))
            cover = QPixmap(pl.coverPath);
        if (cover.isNull())
            cover = CoverProvider::placeholder(pl.name, 116, 6);
        card->setCover(cover);
        card->setText(pl.name, pl.description);
        connect(card, &CoverCard::clicked, this, [this, id = pl.id] { emit openPlaylistRequested(id); });
        addCard(card);
    }

    for (const core::OnlinePlaylist &playlist : m_cloudPlaylists) {
        auto *card = new CoverCard(this);
        card->setFixedCardSize(132, 116);
        card->setFullCoverCard(true);
        QPixmap cover;
        if (!playlist.coverPath.isEmpty() && QFileInfo::exists(playlist.coverPath))
            cover = QPixmap(playlist.coverPath);
        if (cover.isNull())
            cover = CoverProvider::placeholder(playlist.name, 116, 6);
        card->setCover(cover);
        card->setText(playlist.name, sourceDisplayName(playlist.source));
        card->setToolTip(QStringLiteral("%1\n%2")
                             .arg(playlist.name, sourceDisplayName(playlist.source)));
        connect(card, &CoverCard::clicked, this,
                [this, source = playlist.source, remoteId = playlist.remoteId,
                 name = playlist.name] {
            emit openCloudPlaylistRequested(int(source), remoteId, name);
        });
        addCard(card);
    }

    auto *newCard = new NewPlaylistCard(this);
    connect(newCard, &CoverCard::clicked, this, &SelfPlaylistsPage::createPlaylistRequested);
    addCard(newCard);

    for (int i = 0; i < 5; ++i)
        m_grid->setColumnStretch(i, 1);
}

} // namespace ui

#include "SelfPlaylistsPage.moc"
