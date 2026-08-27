#include "DiscoverPage.h"

#include "core/SearchService.h"
#include "ui/CoverCard.h"
#include "ui/CoverProvider.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

namespace ui {
namespace {

QLabel *makeRowTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("class", "rowTitle");
    return label;
}

} // namespace

DiscoverPage::DiscoverPage(QWidget *parent)
    : QWidget(parent)
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    m_contentLayout = new QVBoxLayout(content);
    m_contentLayout->setContentsMargins(28, 24, 28, 40);
    m_contentLayout->setSpacing(0);

    auto *bannerRow = new QWidget(content);
    auto *bannerLayout = new QHBoxLayout(bannerRow);
    bannerLayout->setContentsMargins(0, 0, 0, 0);
    bannerLayout->setSpacing(16);

    auto makeBanner = [this](const QString &title, const QString &sub, const QString &btnText,
                             const QString &gradient, QPushButton *&btn) {
        auto *banner = new QFrame(this);
        banner->setFixedHeight(150);
        banner->setStyleSheet(QStringLiteral(
            "QFrame{border:none;border-radius:10px;background:%1;}"
            "QLabel{color:white;background:transparent;border:none;}"
            "QPushButton{border:none;border-radius:16px;background:rgba(255,255,255,0.22);"
            "color:white;padding:7px 18px;font-size:13px;}")
            .arg(gradient));
        auto *l = new QVBoxLayout(banner);
        l->setContentsMargins(26, 24, 26, 24);
        l->setSpacing(4);
        auto *t = new QLabel(title, banner);
        t->setStyleSheet(QStringLiteral("font-size:20px;font-weight:700;"));
        auto *s = new QLabel(sub, banner);
        s->setStyleSheet(QStringLiteral("font-size:12px;opacity:0.85;"));
        btn = new QPushButton(btnText, banner);
        btn->setCursor(Qt::PointingHandCursor);
        l->addStretch(1);
        l->addWidget(t);
        l->addWidget(s);
        l->addWidget(btn, 0, Qt::AlignLeft);
        return banner;
    };

    QPushButton *playRandomBtn = nullptr;
    QPushButton *importBtn = nullptr;
    bannerLayout->addWidget(makeBanner(QStringLiteral("发现属于你的音乐"), QStringLiteral("随机播放,不期而遇"),
                                       QStringLiteral("▶ 立即播放"),
                                       QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #EC4141,stop:1 #FF8A6B)"),
                                       playRandomBtn), 1);
    bannerLayout->addWidget(makeBanner(QStringLiteral("本地音乐库"), QStringLiteral("导入文件夹,自动整理歌手与专辑"),
                                       QStringLiteral("＋ 导入音乐"),
                                       QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #1B1B2A,stop:1 #3A3A5A)"),
                                       importBtn), 1);
    connect(playRandomBtn, &QPushButton::clicked, this, [this] {
        if (!m_songs.isEmpty())
            emit playRequested(m_songs, QRandomGenerator::global()->bounded(m_songs.size()));
    });
    connect(importBtn, &QPushButton::clicked, this, &DiscoverPage::importRequested);
    m_contentLayout->addWidget(bannerRow);

    m_contentLayout->addSpacing(8);
    m_contentLayout->addWidget(makeRowTitle(QStringLiteral("最近播放"), content));
    auto *recentBox = new QWidget(content);
    m_recentLayout = new QHBoxLayout(recentBox);
    m_recentLayout->setContentsMargins(0, 0, 0, 0);
    m_recentLayout->setSpacing(14);
    m_contentLayout->addWidget(recentBox);

    m_contentLayout->addWidget(makeRowTitle(QStringLiteral("我的歌单"), content));
    auto *gridBox = new QWidget(content);
    m_playlistGrid = new QGridLayout(gridBox);
    m_playlistGrid->setContentsMargins(0, 0, 0, 0);
    m_playlistGrid->setSpacing(16);
    m_playlistGrid->setColumnStretch(0, 1);
    m_contentLayout->addWidget(gridBox);

    m_contentLayout->addWidget(makeRowTitle(QStringLiteral("推荐歌手"), content));
    auto *artistBox = new QWidget(content);
    m_artistLayout = new QHBoxLayout(artistBox);
    m_artistLayout->setContentsMargins(0, 0, 0, 0);
    m_artistLayout->setSpacing(22);
    m_contentLayout->addWidget(artistBox);

    m_contentLayout->addStretch(1);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
    scroll->setWidget(content);
}

void DiscoverPage::setLibrary(const QList<core::Song> &songs, qint64 playingId)
{
    m_songs = songs;
    m_playingId = playingId;
    rebuild();
}

void DiscoverPage::setPlaylists(const QList<core::PlaylistController::PlaylistInfo> &playlists)
{
    m_playlists = playlists;
    rebuild();
}

void DiscoverPage::setRecent(const QList<core::Song> &recent)
{
    m_recent = recent;
    rebuild();
}

void DiscoverPage::rebuild()
{
    auto clear = [](QLayout *layout) {
        while (QLayoutItem *item = layout->takeAt(0)) {
            delete item->widget();
            delete item;
        }
    };
    clear(m_recentLayout);
    clear(m_playlistGrid);
    clear(m_artistLayout);

    const QList<core::Song> recent = m_recent.isEmpty() ? m_songs.mid(0, 8) : m_recent.mid(0, 8);
    for (int i = 0; i < recent.size(); ++i) {
        const core::Song &s = recent[i];
        auto *card = new CoverCard;
        card->setFixedCardSize(132, 116);
        card->setCover(CoverProvider::coverFor(s, 116, 6));
        card->setText(s.title, s.artist);
        connect(card, &CoverCard::clicked, this, [this, i, recent] {
            emit playRequested(recent, i);
        });
        m_recentLayout->addWidget(card);
    }
    m_recentLayout->addStretch(1);

    for (int i = 0; i < m_playlists.size(); ++i) {
        const auto &pl = m_playlists[i];
        core::Song sample;
        sample.title = pl.name;
        auto *card = new CoverCard;
        card->setCover(CoverProvider::placeholder(pl.name, 150, 8));
        card->setText(pl.name, QStringLiteral("%1 首").arg(pl.songCount));
        connect(card, &CoverCard::clicked, this, [this, id = pl.id] {
            emit playlistClicked(id);
        });
        m_playlistGrid->addWidget(card, i / 4, i % 4);
    }
    for (int i = m_playlists.size(); i < (m_playlists.size() + 3) / 4 * 4; ++i)
        m_playlistGrid->setColumnStretch(i % 4, 1);

    const auto artists = core::SearchService::artists(m_songs);
    for (const auto &a : artists.mid(0, 8)) {
        auto *card = new CoverCard;
        card->setFixedCardSize(96, 96);
        card->setCover(CoverProvider::placeholder(a.name, 96, 48));
        card->setRound(true);
        card->setText(a.name, QStringLiteral("%1 首").arg(a.count));
        connect(card, &CoverCard::clicked, this, [this, name = a.name] {
            const QList<core::Song> filtered = [&] {
                QList<core::Song> out;
                for (const core::Song &s : m_songs)
                    if (s.artist == name)
                        out.append(s);
                return out;
            }();
            if (!filtered.isEmpty())
                emit playRequested(filtered, 0);
        });
        m_artistLayout->addWidget(card);
    }
    m_artistLayout->addStretch(1);
}

} // namespace ui
