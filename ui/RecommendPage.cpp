#include "RecommendPage.h"

#include "ui/ThemeManager.h"

#include "core/LibraryService.h"
#include "core/MusicSource.h"
#include "core/MusicSourceRegistry.h"
#include "core/SettingsService.h"
#include "ui/CoverCard.h"
#include "ui/CoverProvider.h"
#include "ui/SongListView.h"
#include "ui/SourceIcons.h"

#include <QButtonGroup>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>
#include <memory>

namespace ui {
namespace {

QStringList songPayloadIdentity(const QJsonArray &payload, core::MusicSource *source)
{
    QStringList identities;
    identities.reserve(payload.size());
    for (const QJsonValue &value : payload) {
        const core::Song song = source ? source->songFromJson(value.toObject()) : core::Song{};
        identities.append(song.hasRemoteIdentity() ? song.stableIdentity()
                                                   : song.title + QLatin1Char('|') + song.artist);
    }
    return identities;
}

QStringList playlistPayloadIdentity(const QJsonArray &payload)
{
    QStringList identities;
    identities.reserve(payload.size());
    for (const QJsonValue &value : payload) {
        const QJsonObject object = value.toObject();
        QString remoteId = object.value(QStringLiteral("remoteId")).toVariant().toString();
        if (remoteId.isEmpty())
            remoteId = object.value(QStringLiteral("id")).toVariant().toString();
        identities.append(remoteId.isEmpty() ? object.value(QStringLiteral("name")).toString()
                                             : remoteId);
    }
    return identities;
}

struct RefreshBatch
{
    int pending = 2;
    int successes = 0;
    bool changed = false;
    QStringList errors;
};

} // namespace

RecommendPage::RecommendPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    auto *topScroll = new QScrollArea(this);
    topScroll->setObjectName(QStringLiteral("recommendTopScroll"));
    topScroll->setWidgetResizable(true);
    topScroll->setFrameShape(QFrame::NoFrame);
    topScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    topScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    topScroll->setMinimumHeight(92);
    topScroll->setMaximumHeight(280);
    topScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setThemedStyleSheet(topScroll, QStringLiteral(
        "QScrollArea#recommendTopScroll{background:transparent;border:none;}"
        "QScrollArea#recommendTopScroll>QWidget>QWidget{background:transparent;}"));
    auto *topHost = new QWidget(topScroll);
    auto *topLayout = new QVBoxLayout(topHost);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(12);

    auto *titleRow = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("推荐"), this);
    title->setProperty("class", "pageTitle");
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    m_neteaseButton = new QPushButton(this);
    m_qqButton = new QPushButton(this);
    const auto configureSourceButton = [](QPushButton *button, const QString &objectName,
                                          const QString &accessibleName) {
        button->setObjectName(objectName);
        button->setProperty("class", "sourceSwitch");
        setThemedStyleSheet(button, QString::fromLatin1(
            "QPushButton { border: none; border-radius: 8px; padding: 0; "
            "background-color: transparent; }"
            "QPushButton:hover { background-color: @surfaceAlt; }"
            "QPushButton:checked { background-color: @accentSoft; border: 1px solid @accent; }"
            "QPushButton:pressed { background-color: @surfacePressed; }"));
        button->setCheckable(true);
        button->setFixedSize(40, 40);
        button->setIconSize(QSize(26, 26));
        button->setCursor(Qt::PointingHandCursor);
        button->setAccessibleName(accessibleName);
        button->setToolTip(QStringLiteral("切换到%1推荐").arg(accessibleName));
    };
    configureSourceButton(m_neteaseButton, QStringLiteral("neteaseSourceSwitch"),
                          QStringLiteral("网易云音乐"));
    configureSourceButton(m_qqButton, QStringLiteral("qqSourceSwitch"),
                          QStringLiteral("QQ 音乐"));
    m_neteaseButton->setChecked(true);
    auto *sourceGroup = new QButtonGroup(this);
    sourceGroup->setExclusive(true);
    sourceGroup->addButton(m_neteaseButton, int(core::SourceId::Netease));
    sourceGroup->addButton(m_qqButton, int(core::SourceId::QqMusic));
    updateSourceButtons();
    titleRow->setSpacing(6);
    titleRow->addWidget(m_neteaseButton);
    titleRow->addWidget(m_qqButton);
    topLayout->addLayout(titleRow);
    connect(sourceGroup, &QButtonGroup::idClicked, this, [this](int sourceId) {
        // QQ 服务是按需启动的；由主窗口确认独立服务可用后再切换，避免
        // 点击时先向未监听的 3200 端口发请求。网易云可直接切换。
        if (sourceId != int(core::SourceId::QqMusic))
            setActiveSource(static_cast<core::SourceId>(sourceId));
        emit sourceActivationRequested(sourceId);
    });

    m_emptyLabel = new QLabel(this);
    m_emptyLabel->setProperty("class", "pageSub");
    m_emptyLabel->setVisible(false);
    topLayout->addWidget(m_emptyLabel);

    auto *plTitle = new QLabel(QStringLiteral("推荐歌单"), this);
    plTitle->setProperty("class", "sectionTitle");
    topLayout->addWidget(plTitle);

    m_playlistScroll = new QScrollArea(this);
    m_playlistScroll->setObjectName(QStringLiteral("recommendedPlaylistScroll"));
    m_playlistScroll->setProperty("allowHorizontalScroll", true);
    m_playlistScroll->setWidgetResizable(true);
    m_playlistScroll->setFrameShape(QFrame::NoFrame);
    m_playlistScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_playlistScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_playlistScroll->setFixedHeight(184);
    setThemedStyleSheet(m_playlistScroll, QStringLiteral(
        "QScrollArea#recommendedPlaylistScroll{background:transparent;border:none;}"
        "QScrollArea#recommendedPlaylistScroll>QWidget>QWidget{background:transparent;}"));
    m_playlistHost = new QWidget(m_playlistScroll);
    m_playlistHost->setObjectName(QStringLiteral("recommendedPlaylistHost"));
    m_playlistHost->setMinimumHeight(168);
    m_playlistHost->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    m_playlistRow = new QHBoxLayout(m_playlistHost);
    m_playlistRow->setContentsMargins(0, 0, 0, 0);
    m_playlistRow->setSpacing(16);
    m_playlistScroll->setWidget(m_playlistHost);
    topLayout->addWidget(m_playlistScroll);
    topScroll->setWidget(topHost);
    layout->addWidget(topScroll);

    auto *dailyTitle = new QLabel(QStringLiteral("每日推荐歌曲"), this);
    dailyTitle->setObjectName(QStringLiteral("recommendedDailyTitle"));
    dailyTitle->setProperty("class", "sectionTitle");
    layout->addWidget(dailyTitle);

    m_list = new SongListView(this);
    m_list->setMinimumHeight(130);
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

void RecommendPage::setSourceRegistry(core::MusicSourceRegistry *registry)
{
    m_registry = registry;
}

void RecommendPage::setActiveSource(core::SourceId sourceId)
{
    if (m_registry) {
        if (core::MusicSource *source = m_registry->source(sourceId))
            m_source = source;
    }
    if (m_neteaseButton && m_qqButton) {
        m_neteaseButton->setChecked(sourceId == core::SourceId::Netease);
        m_qqButton->setChecked(sourceId == core::SourceId::QqMusic);
    }
    refresh();
}

void RecommendPage::setSourceAvailable(core::SourceId sourceId, bool available)
{
    if (sourceId == core::SourceId::Netease)
        m_neteaseAvailable = available;
    else if (sourceId == core::SourceId::QqMusic)
        m_qqAvailable = available;
    updateSourceButtons();
}

void RecommendPage::updateSourceButtons()
{
    if (m_neteaseButton) {
        m_neteaseButton->setIcon(sourceIcon(core::SourceId::Netease, m_neteaseAvailable));
        m_neteaseButton->setToolTip(m_neteaseAvailable
            ? QStringLiteral("切换到网易云音乐推荐")
            : QStringLiteral("网易云音乐暂不可用，点击重试"));
    }
    if (m_qqButton) {
        m_qqButton->setIcon(sourceIcon(core::SourceId::QqMusic, m_qqAvailable));
        m_qqButton->setToolTip(m_qqAvailable
            ? QStringLiteral("切换到 QQ 音乐推荐")
            : QStringLiteral("QQ 音乐暂不可用，点击启动或重试"));
    }
}

void RecommendPage::refresh(bool forceNetwork)
{
    // 启动和来源切换仍然缓存优先；手动刷新保留当前内容，直接请求平台。
    core::MusicSource *source = m_source;
    if (!source) {
        emit refreshStateChanged(false, QStringLiteral("当前没有可用的推荐来源"));
        return;
    }
    const int generation = ++m_requestGeneration;
    const int sourceKey = int(source->sourceId());
    if (!forceNetwork)
        loadCache(source);
    const bool loggedIn = source->sourceId() == core::SourceId::Netease
        ? core::SettingsService::onlineUid() > 0
        : !core::SettingsService::qqUserId().isEmpty();
    if (!loggedIn || source != m_source) {
        emit refreshStateChanged(false, QStringLiteral("登录后才能刷新当前来源推荐"));
        return;
    }

    emit refreshStateChanged(true, QStringLiteral("正在刷新%1推荐…").arg(source->sourceName()));
    const auto batch = std::make_shared<RefreshBatch>();
    const QPointer<RecommendPage> pageGuard(this);
    const auto finishOne = [pageGuard, source, generation, sourceKey, batch](
                               bool success, bool changed, const QString &error) {
        if (!pageGuard || generation != pageGuard->m_requestGeneration
            || source != pageGuard->m_source) {
            return;
        }
        if (success) {
            ++batch->successes;
            batch->changed = batch->changed || changed;
        } else if (!error.isEmpty()) {
            batch->errors.append(error);
        }
        if (--batch->pending > 0)
            return;
        if (batch->successes > 0) {
            pageGuard->saveCache(pageGuard->m_songPayloads.value(sourceKey),
                                 pageGuard->m_playlistPayloads.value(sourceKey), source);
        }
        QString message;
        if (batch->successes == 0) {
            message = QStringLiteral("刷新失败，继续显示上次内容");
            if (!batch->errors.isEmpty())
                message += QStringLiteral("：%1").arg(batch->errors.join(QStringLiteral("；")));
        } else if (!batch->errors.isEmpty()) {
            message = QStringLiteral("部分推荐已刷新：%1")
                          .arg(batch->errors.join(QStringLiteral("；")));
        } else if (batch->changed) {
            message = QStringLiteral("推荐内容已刷新");
        } else {
            message = QStringLiteral("刷新成功，平台返回内容未变化");
        }
        emit pageGuard->refreshStateChanged(false, message);
    };

    source->recommendSongs(
        [pageGuard, source, generation, sourceKey, finishOne](const QJsonArray &songs) {
            if (!pageGuard || generation != pageGuard->m_requestGeneration
                || source != pageGuard->m_source) {
                return;
            }
            if (songs.isEmpty()) {
                finishOne(false, false, QStringLiteral("平台未返回推荐歌曲"));
                return;
            }
            const bool changed = songPayloadIdentity(
                                     pageGuard->m_songPayloads.value(sourceKey), source)
                != songPayloadIdentity(songs, source);
            pageGuard->m_songPayloads.insert(sourceKey, songs);
            pageGuard->buildDaily(songs, source);
            finishOne(true, changed, QString());
        },
        [pageGuard, source, generation, finishOne](const QString &message) {
            if (pageGuard && generation == pageGuard->m_requestGeneration
                && source == pageGuard->m_source) {
                finishOne(false, false, QStringLiteral("推荐歌曲：%1").arg(message));
            }
        });

    const int requestedOffset = forceNetwork ? m_playlistOffsets.value(sourceKey, 0) + 6
                                             : m_playlistOffsets.value(sourceKey, 0);
    const auto playlistRequest = std::make_shared<std::function<void(int, bool)>>();
    const std::weak_ptr<std::function<void(int, bool)>> weakPlaylistRequest = playlistRequest;
    *playlistRequest = [pageGuard, source, generation, sourceKey, finishOne,
                        weakPlaylistRequest](
                           int offset, bool fallbackUsed) {
        if (!pageGuard || generation != pageGuard->m_requestGeneration
            || source != pageGuard->m_source) {
            return;
        }
        const auto requestHolder = weakPlaylistRequest.lock();
        if (!requestHolder)
            return;
        source->topPlaylists(
            QString(), offset,
            [pageGuard, source, generation, sourceKey, offset, fallbackUsed, finishOne,
             requestHolder](const QJsonArray &playlists) {
                if (!pageGuard || generation != pageGuard->m_requestGeneration
                    || source != pageGuard->m_source) {
                    return;
                }
                if (playlists.isEmpty() && offset > 0 && !fallbackUsed) {
                    (*requestHolder)(0, true);
                    return;
                }
                if (playlists.isEmpty()) {
                    finishOne(false, false, QStringLiteral("平台未返回推荐歌单"));
                    return;
                }
                const bool changed = playlistPayloadIdentity(
                                         pageGuard->m_playlistPayloads.value(sourceKey))
                    != playlistPayloadIdentity(playlists);
                pageGuard->m_playlistOffsets.insert(sourceKey, offset);
                pageGuard->m_playlistPayloads.insert(sourceKey, playlists);
                pageGuard->buildPlaylists(playlists, source);
                finishOne(true, changed, QString());
            },
            [pageGuard, source, generation, finishOne,
             requestHolder](const QString &message) {
                if (pageGuard && generation == pageGuard->m_requestGeneration
                    && source == pageGuard->m_source) {
                    finishOne(false, false, QStringLiteral("推荐歌单：%1").arg(message));
                }
            });
    };
    (*playlistRequest)(requestedOffset, false);
}

void RecommendPage::resetAfterCacheClear()
{
    ++m_requestGeneration;
    m_songPayloads.clear();
    m_playlistPayloads.clear();
    m_playlistOffsets.clear();
    showEmpty();
    emit refreshStateChanged(false, QStringLiteral("缓存已清理，可重新刷新推荐"));
}

core::SourceId RecommendPage::activeSourceId() const
{
    return m_source ? m_source->sourceId() : core::SourceId::Netease;
}

QList<core::Song> RecommendPage::currentSongs() const
{
    return m_list->songs();
}

void RecommendPage::setPlaylistMenuItems(const QList<QPair<int, QString>> &items)
{
    m_list->setPlaylistMenuItems(items);
}

void RecommendPage::buildDaily(const QJsonArray &arr, core::MusicSource *source)
{
    const int generation = m_requestGeneration;
    QList<core::Song> songs;
    for (const QJsonValue &v : arr) {
        core::Song s = source ? source->songFromJson(v.toObject()) : core::Song();
        if (m_lib && s.isOnline()) {
            s.id = m_lib->upsertOnlineSong(s);
            if (s.id > 0) {
                const core::Song stored = m_lib->songById(s.id);
                s.coverPath = stored.coverPath;
                s.cachePath = stored.cachePath;
                s.downloadPath = stored.downloadPath;
                s.lyricPath = stored.lyricPath;
            }
        }
        songs.append(s);
    }
    m_list->setSongs(songs);
    m_emptyLabel->setVisible(false);
    m_list->setVisible(true);

    for (const core::Song &song : songs) {
        if (!song.isOnline() || song.id <= 0 || song.coverUrl.isEmpty() || !source || !m_lib)
            continue;
        const core::Song stored = m_lib->songById(song.id);
        if (!stored.coverPath.isEmpty() && QFileInfo::exists(stored.coverPath))
            continue;
        const QString path = m_lib->songCoverCachePath(song);
        if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
            m_lib->setSongCoverPath(song.id, path);
            continue;
        }
        const qint64 songId = song.id;
        const QPointer<RecommendPage> pageGuard(this);
        source->downloadToFile(QUrl(song.coverUrl), path,
                               [pageGuard, songId, path, generation](bool ok) {
            if (!pageGuard || generation != pageGuard->m_requestGeneration) {
                if (ok)
                    QFile::remove(path);
                return;
            }
            if (!ok || !pageGuard->m_lib)
                return;
            pageGuard->m_lib->setSongCoverPath(songId, path);
        });
    }
}

void RecommendPage::buildPlaylists(const QJsonArray &arr, core::MusicSource *source)
{
    const int generation = m_requestGeneration;
    while (QLayoutItem *item = m_playlistRow->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    int shown = 0;
    for (const QJsonValue &v : arr) {
        if (shown >= 6)
            break;
        const QJsonObject o = v.toObject();
        QString remoteId = o.value(QStringLiteral("remoteId")).toVariant().toString();
        if (remoteId.isEmpty())
            remoteId = o.value(QStringLiteral("id")).toVariant().toString();
        if (remoteId.isEmpty())
            continue;
        const QString name = o.value(QStringLiteral("name")).toString();
        auto *slot = new QWidget(m_playlistHost);
        slot->setMinimumWidth(132);
        slot->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *slotLayout = new QHBoxLayout(slot);
        slotLayout->setContentsMargins(0, 0, 0, 0);
        auto *card = new CoverCard(slot);
        card->setFixedCardSize(132, 116);
        card->setFullCoverCard(true);
        card->setCover(CoverProvider::placeholder(name.left(1), 116, 6));
        card->setText(name, QStringLiteral("歌单"));
        slotLayout->addWidget(card, 0, Qt::AlignHCenter | Qt::AlignTop);
        const int sourceId = int(source->sourceId());
        connect(card, &CoverCard::clicked, this, [this, sourceId, remoteId, name] {
            emit openPlaylistRequested(sourceId, remoteId, name);
        });
        m_playlistRow->addWidget(slot, 1);
        ++shown;
        // 异步下载封面
        QString pic = o.value(QStringLiteral("picUrl")).toString();
        if (pic.isEmpty())
            pic = o.value(QStringLiteral("coverImgUrl")).toString();
        if (pic.isEmpty())
            pic = o.value(QStringLiteral("coverUrl")).toString();
        if (m_lib && !pic.isEmpty()) {
            const QString path = m_lib->playlistCoverCachePath(source->sourceId(), remoteId);
            if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
                QPixmap pm(path);
                if (!pm.isNull())
                    card->setCover(pm);
            } else {
                const QPointer<CoverCard> guard(card);
                const QPointer<RecommendPage> pageGuard(this);
                source->downloadToFile(QUrl(pic), path,
                                       [pageGuard, guard, path, generation](bool ok) {
                    if (!pageGuard || generation != pageGuard->m_requestGeneration) {
                        if (ok)
                            QFile::remove(path);
                        return;
                    }
                    if (!ok || !guard)
                        return;
                    QPixmap pm(path);
                    if (!pm.isNull())
                        guard->setCover(pm);
                });
            }
        }
    }
    if (m_playlistHost) {
        const int minimumContentWidth = shown > 0
            ? shown * 132 + qMax(0, shown - 1) * m_playlistRow->spacing() : 0;
        m_playlistHost->setMinimumWidth(minimumContentWidth);
        m_playlistHost->updateGeometry();
    }
}

void RecommendPage::saveCache(const QJsonArray &songs, const QJsonArray &playlists,
                              core::MusicSource *source)
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("updatedAtMs"),
                QString::number(QDateTime::currentMSecsSinceEpoch()));
    root.insert(QStringLiteral("playlistOffset"),
                m_playlistOffsets.value(int(source ? source->sourceId()
                                                    : core::SourceId::Netease), 0));
    root.insert(QStringLiteral("songs"), songs);
    root.insert(QStringLiteral("playlists"), playlists);
    QSaveFile file(cachePath(source ? source->sourceId() : core::SourceId::Netease));
    if (!QDir().mkpath(QFileInfo(file.fileName()).absolutePath())
        || !file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) >= 0)
        file.commit();
}

void RecommendPage::loadCache(core::MusicSource *source)
{
    QFile f(cachePath(source ? source->sourceId() : core::SourceId::Netease));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        showEmpty();
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.isEmpty()) {
        showEmpty();
        return;
    }
    const int sourceKey = int(source ? source->sourceId() : core::SourceId::Netease);
    const QJsonArray songs = root.value(QStringLiteral("songs")).toArray();
    const QJsonArray playlists = root.value(QStringLiteral("playlists")).toArray();
    m_songPayloads.insert(sourceKey, songs);
    m_playlistPayloads.insert(sourceKey, playlists);
    m_playlistOffsets.insert(sourceKey, root.value(QStringLiteral("playlistOffset")).toInt(0));
    buildDaily(songs, source);
    buildPlaylists(playlists, source);
}

QString RecommendPage::cachePath(core::SourceId sourceId) const
{
    const QString base = core::SettingsService::recommendCachePath();
    if (sourceId == core::SourceId::Netease)
        return base;
    return QFileInfo(base).absolutePath() + QStringLiteral("/recommend-qq.json");
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
    if (m_playlistHost)
        m_playlistHost->setMinimumWidth(0);
}

} // namespace ui
