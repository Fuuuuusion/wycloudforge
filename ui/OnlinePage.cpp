#include "OnlinePage.h"

#include "core/LibraryService.h"
#include "ui/CoverCard.h"
#include "ui/CoverProvider.h"
#include "ui/SongListView.h"

#include <QButtonGroup>
#include <QFrame>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QUrl>

namespace ui {
namespace {

QPushButton *makePillButton(const QString &text, QWidget *parent)
{
    auto *btn = new QPushButton(text, parent);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;background:#1B1B24;color:#9A9AA5;"
        "font-size:13px;padding:7px 16px;border-radius:999px;}"
        "QPushButton:hover{background:#2A2A36;color:#E8E8E8;}"
        "QPushButton:checked{background:#3A2024;color:#EC4141;font-weight:600;}"));
    return btn;
}

} // namespace

OnlinePage::OnlinePage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 130);
    layout->setSpacing(12);

    auto *header = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel(QStringLiteral("在线音乐"), header);
    title->setProperty("class", "pageTitle");
    m_status = new QLabel(QStringLiteral("在线服务连接中…"), header);
    m_status->setStyleSheet(QStringLiteral("color:#6E6E7A;font-size:12px;"));
    m_loginBtn = makePillButton(QStringLiteral("登录"), header);
    headerLayout->addWidget(title);
    headerLayout->addStretch(1);
    headerLayout->addWidget(m_status);
    headerLayout->addWidget(m_loginBtn);
    layout->addWidget(header);
    connect(m_loginBtn, &QPushButton::clicked, this, [this] {
        if (m_loginBtn->text() == QStringLiteral("退出登录"))
            emit logoutRequested();
        else
            emit loginRequested();
    });

    auto *tabRow = new QWidget(this);
    auto *tabLayout = new QHBoxLayout(tabRow);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(8);
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    const QStringList names = { QStringLiteral("推荐"), QStringLiteral("排行榜"),
                                QStringLiteral("歌单广场"), QStringLiteral("我的歌单"),
                                QStringLiteral("私人FM") };
    for (int i = 0; i < names.size(); ++i) {
        auto *btn = makePillButton(names[i], tabRow);
        btn->setCheckable(true);
        group->addButton(btn, i);
        tabLayout->addWidget(btn);
    }
    tabLayout->addStretch(1);
    layout->addWidget(tabRow);

    m_stack = new QStackedWidget(this);
    buildRecommendTab();
    buildRankTab();
    buildPlaylistSquareTab();
    buildMyPlaylistsTab();
    buildFmTab();
    layout->addWidget(m_stack, 1);
    connect(group, &QButtonGroup::idClicked, m_stack, &QStackedWidget::setCurrentIndex);
    group->button(0)->setChecked(true);
}

void OnlinePage::setSourceProvider(core::MusicSource *source, core::LibraryService *library)
{
    m_source = source;
    m_lib = library;
    if (m_source)
        m_status->setText(QStringLiteral("在线服务:%1").arg(m_source->sourceName()));
}

void OnlinePage::setLoginInfo(const QString &nickname)
{
    if (nickname.isEmpty()) {
        m_loginBtn->setText(QStringLiteral("登录"));
        m_status->setText(m_source ? QStringLiteral("在线服务:%1").arg(m_source->sourceName())
                                   : QStringLiteral("在线服务连接中…"));
    } else {
        m_loginBtn->setText(QStringLiteral("退出登录"));
        m_status->setText(QStringLiteral("%1 · 已登录").arg(nickname));
    }
}

void OnlinePage::buildRecommendTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *row = new QHBoxLayout;
    auto *dailyBtn = makePillButton(QStringLiteral("每日推荐(需登录)"), page);
    auto *label = new QLabel(QStringLiteral("推荐歌单"), page);
    label->setProperty("class", "rowTitle");
    row->addWidget(label);
    row->addStretch(1);
    row->addWidget(dailyBtn);
    layout->addLayout(row);

    m_dailyList = new SongListView;
    m_dailyList->hide();
    layout->addWidget(m_dailyList, 1);
    connect(dailyBtn, &QPushButton::clicked, this, [this] {
        if (!m_source)
            return;
        m_source->recommendSongs([this](const QJsonArray &arr) {
            loadSongs(m_dailyList, arr);
            m_dailyList->show();
        }, [this](const QString &msg) {
            m_status->setText(QStringLiteral("每日推荐失败:%1").arg(msg));
        });
    });

    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget;
    auto *gridLayout = new QVBoxLayout(content);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    m_squareGrid = new QGridLayout;
    m_squareGrid->setContentsMargins(0, 0, 0, 0);
    m_squareGrid->setSpacing(16);
    gridLayout->addLayout(m_squareGrid);
    gridLayout->addStretch(1);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    m_stack->addWidget(page);
}

void OnlinePage::buildRankTab()
{
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);
    m_topListWidget = new QListWidget(page);
    m_topListWidget->setFixedWidth(220);
    m_topListWidget->setStyleSheet(QStringLiteral(
        "QListWidget{background:#16161E;border:none;border-radius:10px;}"
        "QListWidget::item{padding:9px 12px;border-radius:6px;color:#C8C8D0;}"
        "QListWidget::item:selected{background:#3A2024;color:#EC4141;}"));
    m_rankList = new SongListView;
    layout->addWidget(m_topListWidget);
    layout->addWidget(m_rankList, 1);
    m_stack->addWidget(page);

    connect(m_topListWidget, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || !m_source)
            return;
        const qint64 id = m_topListWidget->item(row)->data(Qt::UserRole).toLongLong();
        m_source->playlistTracks(id, [this](const QJsonArray &arr) { loadSongs(m_rankList, arr); },
                                 [this](const QString &msg) { m_status->setText(QStringLiteral("榜单加载失败:%1").arg(msg)); });
    });
    if (m_source) {
        m_source->topLists([this](const QJsonArray &arr) {
            for (const QJsonValue &v : arr) {
                const QJsonObject o = v.toObject();
                auto *item = new QListWidgetItem(o.value(QStringLiteral("name")).toString(), m_topListWidget);
                item->setData(Qt::UserRole, o.value(QStringLiteral("id")).toVariant().toLongLong());
            }
        });
    }
}

void OnlinePage::buildPlaylistSquareTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *catRow = new QHBoxLayout;
    catRow->setSpacing(8);
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    const QStringList cats = { QStringLiteral("华语"), QStringLiteral("流行"), QStringLiteral("摇滚"),
                               QStringLiteral("民谣"), QStringLiteral("电子"), QStringLiteral("ACG") };
    for (int i = 0; i < cats.size(); ++i) {
        auto *btn = makePillButton(cats[i], page);
        btn->setCheckable(true);
        group->addButton(btn, i);
        catRow->addWidget(btn);
    }
    catRow->addStretch(1);
    layout->addLayout(catRow);
    group->button(0)->setChecked(true);
    connect(group, &QButtonGroup::idClicked, this, [this, cats](int id) {
        m_squareCat = cats[id];
        refreshPlaylistSquare();
    });

    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget;
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(0, 0, 0, 0);
    m_squareGrid = new QGridLayout;
    m_squareGrid->setContentsMargins(0, 0, 0, 0);
    m_squareGrid->setSpacing(16);
    v->addLayout(m_squareGrid);
    v->addStretch(1);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    m_stack->addWidget(page);
}

void OnlinePage::buildMyPlaylistsTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    auto *hint = new QLabel(QStringLiteral("登录后可查看你的网易云歌单"), page);
    hint->setStyleSheet(QStringLiteral("color:#6E6E7A;font-size:12px;"));
    layout->addWidget(hint);
    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget;
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(0, 0, 0, 0);
    m_mineGrid = new QGridLayout;
    m_mineGrid->setContentsMargins(0, 0, 0, 0);
    m_mineGrid->setSpacing(16);
    v->addLayout(m_mineGrid);
    v->addStretch(1);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    m_stack->addWidget(page);
}

void OnlinePage::buildFmTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    auto *btn = makePillButton(QStringLiteral("开始私人FM"), page);
    layout->addWidget(btn, 0, Qt::AlignLeft);
    m_fmList = new SongListView;
    layout->addWidget(m_fmList, 1);
    m_stack->addWidget(page);
    connect(btn, &QPushButton::clicked, this, [this] {
        if (!m_source)
            return;
        m_source->personalFm([this](const QJsonArray &arr) { loadSongs(m_fmList, arr); },
                             [this](const QString &msg) { m_status->setText(QStringLiteral("私人FM失败:%1").arg(msg)); });
    });
}

void OnlinePage::refresh()
{
    if (!m_source)
        return;
    refreshPlaylistSquare();
    if (m_source->sourceId() == 1) {
        // 网易云源:加载排行榜(已登录时拉取我的歌单)
        m_source->topLists([this](const QJsonArray &arr) {
            m_topListWidget->clear();
            for (const QJsonValue &v : arr) {
                const QJsonObject o = v.toObject();
                auto *item = new QListWidgetItem(o.value(QStringLiteral("name")).toString(), m_topListWidget);
                item->setData(Qt::UserRole, o.value(QStringLiteral("id")).toVariant().toLongLong());
            }
        });
    }
}

void OnlinePage::refreshPlaylistSquare()
{
    if (!m_source)
        return;
    m_source->topPlaylists(m_squareCat, 0, [this](const QJsonArray &arr) {
        while (QLayoutItem *item = m_squareGrid->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        int i = 0;
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            auto *card = new CoverCard;
            const QString name = o.value(QStringLiteral("name")).toString();
            card->setCover(CoverProvider::placeholder(name, 150, 8));
            card->setText(o.value(QStringLiteral("name")).toString(),
                          QStringLiteral("%1 首").arg(o.value(QStringLiteral("trackCount")).toInt()));
            const qint64 id = o.value(QStringLiteral("id")).toVariant().toLongLong();
            connect(card, &CoverCard::clicked, this, [this, id, name] {
                emit openPlaylistRequested(id, name);
            });
            m_squareGrid->addWidget(card, i / 5, i % 5);
            QString coverUrl = o.value(QStringLiteral("coverImgUrl")).toString();
            if (coverUrl.isEmpty())
                coverUrl = o.value(QStringLiteral("picUrl")).toString();
            if (m_source && m_lib && !coverUrl.isEmpty()) {
                const QString path = m_lib->playlistCoverCachePath(id);
                auto apply = [card, path](bool ok) {
                    if (!ok)
                        return;
                    const QPixmap pm(path);
                    if (!pm.isNull())
                        card->setCover(pm.scaled(150, 150, Qt::KeepAspectRatioByExpanding,
                                                  Qt::SmoothTransformation));
                };
                if (QFileInfo::exists(path) && QFileInfo(path).size() > 0)
                    apply(true);
                else
                    m_source->downloadToFile(QUrl(coverUrl), path, apply);
            }
            ++i;
        }
        m_squareGrid->setColumnStretch(5, 1);
    });
}

void OnlinePage::loadSongs(SongListView *view, const QJsonArray &arr)
{
    if (!m_source || !m_lib)
        return;
    QList<core::Song> songs;
    for (const QJsonValue &v : arr) {
        core::Song s = m_source->songFromJson(v.toObject());
        s.id = m_lib->upsertOnlineSong(s);
        if (s.id > 0) {
            const core::Song stored = m_lib->songById(s.id);
            s.coverPath = stored.coverPath;
            s.cachePath = stored.cachePath;
            s.downloadPath = stored.downloadPath;
            s.lyricPath = stored.lyricPath;
        }
        songs.append(s);
    }
    view->setSongs(songs);
    for (const core::Song &song : songs)
        ensureCover(view, song);
}

void OnlinePage::ensureCover(SongListView *view, const core::Song &song)
{
    if (!song.isOnline() || song.coverUrl.isEmpty() || song.id <= 0 || !m_source || !m_lib)
        return;
    const core::Song current = m_lib->songById(song.id);
    if (!current.coverPath.isEmpty() && QFileInfo::exists(current.coverPath))
        return;
    const QString path = m_lib->songCoverCachePath(song);
    if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
        m_lib->setSongCoverPath(song.id, path);
        updateSongCover(view, song.id, path);
        return;
    }
    const QUrl url(song.coverUrl);
    const qint64 id = song.id;
    m_source->downloadToFile(url, path, [this, id, path](bool ok) {
        if (ok && m_lib) {
            m_lib->setSongCoverPath(id, path);
            updateSongCover(m_dailyList, id, path);
            updateSongCover(m_rankList, id, path);
            updateSongCover(m_squareList, id, path);
            updateSongCover(m_mineList, id, path);
            updateSongCover(m_fmList, id, path);
        }
    });
}

void OnlinePage::updateSongCover(SongListView *view, qint64 songId, const QString &path)
{
    if (!view || path.isEmpty() || !QFileInfo::exists(path))
        return;
    auto songs = view->songs();
    bool changed = false;
    for (core::Song &song : songs) {
        if (song.id == songId && song.coverPath != path) {
            song.coverPath = path;
            changed = true;
        }
    }
    if (changed)
        view->setSongs(songs);
}

} // namespace ui
