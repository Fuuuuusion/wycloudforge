#include "RecommendPage.h"

#include "core/LibraryService.h"
#include "core/MusicSource.h"
#include "core/MusicSourceRegistry.h"
#include "core/SettingsService.h"
#include "ui/CoverCard.h"
#include "ui/CoverProvider.h"
#include "ui/SongListView.h"
#include "ui/SourceIcons.h"

#include <QButtonGroup>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
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

    auto *topScroll = new QScrollArea(this);
    topScroll->setObjectName(QStringLiteral("recommendTopScroll"));
    topScroll->setWidgetResizable(true);
    topScroll->setFrameShape(QFrame::NoFrame);
    topScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    topScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    topScroll->setMinimumHeight(92);
    topScroll->setMaximumHeight(280);
    topScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    topScroll->setStyleSheet(QStringLiteral(
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
        button->setStyleSheet(QString::fromLatin1(
            "QPushButton { border: none; border-radius: 8px; padding: 0; "
            "background-color: transparent; }"
            "QPushButton:hover { background-color: #1B1B24; }"
            "QPushButton:checked { background-color: #3A2024; border: 1px solid #EC4141; }"
            "QPushButton:pressed { background-color: #24242E; }"));
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
    m_playlistScroll->setStyleSheet(QStringLiteral(
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

void RecommendPage::refresh()
{
    // 启动时先展示上次成功获取的内容，避免登录校验和 API 自启动期间整页空白。
    // 在线请求完成后会用最新结果无缝替换缓存。
    core::MusicSource *source = m_source;
    if (!source)
        return;
    const int generation = ++m_requestGeneration;
    loadCache(source);
    const bool loggedIn = source->sourceId() == core::SourceId::Netease
        ? core::SettingsService::onlineUid() > 0
        : !core::SettingsService::qqUserId().isEmpty();
    if (!loggedIn || !m_source)
        return;
    source->recommendSongs([this, source, generation](const QJsonArray &songs) {
        if (generation != m_requestGeneration || source != m_source)
            return;
        buildDaily(songs, source);
        const auto playlistsReady = [this, source, songs, generation](const QJsonArray &pls) {
            if (generation != m_requestGeneration || source != m_source)
                return;
            buildPlaylists(pls, source);
            saveCache(songs, pls, source);
        };
        const auto playlistsFailed = [this, source, songs, generation](const QString &) {
            if (generation == m_requestGeneration && source == m_source)
                saveCache(songs, QJsonArray(), source);
        };
        if (source->sourceId() == core::SourceId::QqMusic)
            source->userPlaylists(core::SettingsService::qqUserId(), playlistsReady, playlistsFailed);
        else
            source->topPlaylists(QString(), 0, playlistsReady, playlistsFailed);
    }, [this, source, generation](const QString &) {
        if (generation == m_requestGeneration && source == m_source)
            loadCache(source);
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

void RecommendPage::buildDaily(const QJsonArray &arr, core::MusicSource *source)
{
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
        source->downloadToFile(QUrl(song.coverUrl), path, [this, songId, path](bool ok) {
            if (!ok || !m_lib)
                return;
            m_lib->setSongCoverPath(songId, path);
        });
    }
}

void RecommendPage::buildPlaylists(const QJsonArray &arr, core::MusicSource *source)
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
                source->downloadToFile(QUrl(pic), path, [guard, path](bool ok) {
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
    root.insert(QStringLiteral("songs"), songs);
    root.insert(QStringLiteral("playlists"), playlists);
    QFile f(cachePath(source ? source->sourceId() : core::SourceId::Netease));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void RecommendPage::loadCache(core::MusicSource *source)
{
    QFile f(cachePath(source ? source->sourceId() : core::SourceId::Netease));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        showEmpty();
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    buildDaily(root.value(QStringLiteral("songs")).toArray(), source);
    buildPlaylists(root.value(QStringLiteral("playlists")).toArray(), source);
    if (root.isEmpty())
        showEmpty();
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
