#include "RecommendPage.h"

#include "core/LibraryService.h"
#include "core/MusicSource.h"
#include "core/SettingsService.h"
#include "ui/CoverCard.h"
#include "ui/CoverProvider.h"
#include "ui/SongListView.h"

#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
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
    connect(m_list, &SongListView::heartRequested, this, &RecommendPage::heartRequested);
    connect(m_list, &SongListView::addToPlaylistRequested,
            this, &RecommendPage::addToPlaylistRequested);
}

void RecommendPage::setSourceProvider(core::MusicSource *source, core::LibraryService *library)
{
    m_source = source;
    m_lib = library;
}

void RecommendPage::refresh()
{
    // 启动时先展示上次成功获取的内容，避免登录校验和 API 自启动期间整页空白。
    // 在线请求完成后会用最新结果无缝替换缓存。
    loadCache();
    const bool loggedIn = core::SettingsService::onlineUid() > 0;
    if (!loggedIn || !m_source)
        return;
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

QList<core::Song> RecommendPage::currentSongs() const
{
    return m_list->songs();
}

void RecommendPage::setPlaylistMenuItems(const QList<QPair<int, QString>> &items)
{
    m_list->setPlaylistMenuItems(items);
}

void RecommendPage::buildDaily(const QJsonArray &arr)
{
    QList<core::Song> songs;
    for (const QJsonValue &v : arr) {
        core::Song s = m_source ? m_source->songFromJson(v.toObject()) : core::Song();
        if (m_lib && s.isOnline()) {
            s.id = m_lib->upsertOnlineSong(s);
            if (s.id > 0) {
                const core::Song stored = m_lib->songById(s.id);
                s.coverPath = stored.coverPath;
                s.cachePath = stored.cachePath;
                s.lyricPath = stored.lyricPath;
            }
        }
        songs.append(s);
    }
    m_list->setSongs(songs);
    m_emptyLabel->setVisible(false);
    m_list->setVisible(true);

    for (const core::Song &song : songs) {
        if (!song.isOnline() || song.id <= 0 || song.coverUrl.isEmpty() || !m_source || !m_lib)
            continue;
        const core::Song stored = m_lib->songById(song.id);
        if (!stored.coverPath.isEmpty() && QFileInfo::exists(stored.coverPath))
            continue;
        const QString path = m_lib->songCoverCachePath(song);
        if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
            m_lib->setSongCoverPath(song.id, path);
            updateDailySongCover(song.id, path);
            continue;
        }
        const qint64 songId = song.id;
        m_source->downloadToFile(QUrl(song.coverUrl), path, [this, songId, path](bool ok) {
            if (!ok || !m_lib)
                return;
            m_lib->setSongCoverPath(songId, path);
            updateDailySongCover(songId, path);
        });
    }
}

void RecommendPage::updateDailySongCover(qint64 songId, const QString &path)
{
    if (!m_list || path.isEmpty() || !QFileInfo::exists(path))
        return;
    auto songs = m_list->songs();
    bool changed = false;
    for (core::Song &song : songs) {
        if (song.id == songId && song.coverPath != path) {
            song.coverPath = path;
            changed = true;
        }
    }
    if (changed)
        m_list->setSongs(songs);
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
            if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
                QPixmap pm(path);
                if (!pm.isNull())
                    card->setCover(pm.scaled(116, 116, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            } else {
                const QPointer<CoverCard> guard(card);
                m_source->downloadToFile(QUrl(pic), path, [guard, path](bool ok) {
                    if (!ok || !guard)
                        return;
                    QPixmap pm(path);
                    if (!pm.isNull())
                        guard->setCover(pm.scaled(116, 116, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                });
            }
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
