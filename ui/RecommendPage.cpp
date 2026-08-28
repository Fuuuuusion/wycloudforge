#include "RecommendPage.h"

#include "core/LibraryService.h"
#include "core/MusicSource.h"
#include "core/SettingsService.h"
#include "ui/CoverCard.h"
#include "ui/CoverProvider.h"
#include "ui/SongListView.h"

#include <QFile>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

namespace ui {

RecommendPage::RecommendPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("推荐"), this);
    title->setProperty("class", "pageTitle");
    layout->addWidget(title);

    m_emptyLabel = new QLabel(this);
    m_emptyLabel->setProperty("class", "pageSub");
    m_emptyLabel->setVisible(false);
    layout->addWidget(m_emptyLabel);

    auto *plTitle = new QLabel(QStringLiteral("推荐歌单"), this);
    plTitle->setProperty("class", "sectionTitle");
    layout->addWidget(plTitle);

    auto *plHost = new QWidget(this);
    m_playlistRow = new QHBoxLayout(plHost);
    m_playlistRow->setContentsMargins(0, 0, 0, 0);
    m_playlistRow->setSpacing(16);
    m_playlistRow->addStretch(1);
    layout->addWidget(plHost);

    auto *dailyTitle = new QLabel(QStringLiteral("每日推荐歌曲"), this);
    dailyTitle->setProperty("class", "sectionTitle");
    layout->addWidget(dailyTitle);

    m_list = new SongListView(this);
    layout->addWidget(m_list, 1);
    connect(m_list, &SongListView::playRequested, this, [this](int row) {
        emit playRequested(m_list->songs(), row);
    });
}

void RecommendPage::setSourceProvider(core::MusicSource *source, core::LibraryService *library)
{
    m_source = source;
    m_lib = library;
}

void RecommendPage::refresh()
{
    const bool loggedIn = core::SettingsService::onlineUid() > 0;
    if (!loggedIn || !m_source) {
        loadCache();
        return;
    }
    m_source->recommendSongs([this](const QJsonArray &songs) {
        buildDaily(songs);
        m_source->topPlaylists(QString(), 0, [this, songs](const QJsonArray &pls) {
            buildPlaylists(pls);
            saveCache(songs, pls);
        }, [this, songs](const QString &) {
            saveCache(songs, QJsonArray());
        });
    }, [this](const QString &) {
        loadCache();
    });
}

void RecommendPage::buildDaily(const QJsonArray &arr)
{
    QList<core::Song> songs;
    for (const QJsonValue &v : arr) {
        core::Song s = m_source ? m_source->songFromJson(v.toObject()) : core::Song();
        if (m_lib && s.isOnline())
            s.id = m_lib->upsertOnlineSong(s);
        songs.append(s);
    }
    m_list->setSongs(songs);
    m_emptyLabel->setVisible(false);
    m_list->setVisible(true);
}

void RecommendPage::buildPlaylists(const QJsonArray &arr)
{
    while (QLayoutItem *item = m_playlistRow->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    int shown = 0;
    for (const QJsonValue &v : arr) {
        if (shown >= 6)
            break;
        const QJsonObject o = v.toObject();
        const qint64 id = o.value(QStringLiteral("id")).toVariant().toLongLong();
        const QString name = o.value(QStringLiteral("name")).toString();
        auto *card = new CoverCard(this);
        card->setFixedCardSize(132, 116);
        card->setCover(CoverProvider::placeholder(name.left(1), 116, 6));
        card->setText(name, QStringLiteral("歌单"));
        connect(card, &CoverCard::clicked, this, [this, id, name] { emit openPlaylistRequested(id, name); });
        m_playlistRow->insertWidget(m_playlistRow->count() - 1, card);
        ++shown;
        // 异步下载封面
        QString pic = o.value(QStringLiteral("picUrl")).toString();
        if (pic.isEmpty())
            pic = o.value(QStringLiteral("coverImgUrl")).toString();
        if (m_lib && !pic.isEmpty()) {
            const QString path = m_lib->playlistCoverCachePath(id);
            m_source->downloadToFile(QUrl(pic), path, [card, path](bool ok) {
                if (ok) {
                    QPixmap pm(path);
                    if (!pm.isNull())
                        card->setCover(pm.scaled(116, 116, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                }
            });
        }
    }
}

void RecommendPage::saveCache(const QJsonArray &songs, const QJsonArray &playlists)
{
    QJsonObject root;
    root.insert(QStringLiteral("songs"), songs);
    root.insert(QStringLiteral("playlists"), playlists);
    QFile f(core::SettingsService::recommendCachePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void RecommendPage::loadCache()
{
    QFile f(core::SettingsService::recommendCachePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        showEmpty();
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    buildDaily(root.value(QStringLiteral("songs")).toArray());
    buildPlaylists(root.value(QStringLiteral("playlists")).toArray());
    if (root.isEmpty())
        showEmpty();
}

void RecommendPage::showEmpty()
{
    m_emptyLabel->setText(QStringLiteral("登录后获取推荐内容"));
    m_emptyLabel->setVisible(true);
    m_list->setSongs(QList<core::Song>());
    m_list->setVisible(false);
    while (QLayoutItem *item = m_playlistRow->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

} // namespace ui
