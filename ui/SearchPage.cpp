#include "SearchPage.h"

#include "core/SearchService.h"
#include "ui/SongListView.h"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace ui {
namespace {

QPushButton *makeResultRow(const QString &text, const QString &sub, QWidget *parent)
{
    auto *btn = new QPushButton(parent);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(QStringLiteral(
        "QPushButton{background:rgba(255,255,255,0.05);border:none;border-radius:6px;"
        "padding:12px 14px;text-align:left;color:#E8E8E8;}"
        "QPushButton:hover{background:rgba(255,255,255,0.08);}"));
    btn->setText(sub.isEmpty() ? text : QStringLiteral("%1    %2").arg(text, sub));
    return btn;
}

} // namespace

SearchPage::SearchPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    m_title = new QLabel(QStringLiteral("搜索"), this);
    m_title->setProperty("class", "pageTitle");
    layout->addWidget(m_title);

    auto *tabRow = new QWidget(this);
    auto *tabLayout = new QHBoxLayout(tabRow);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(30);
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    const QStringList names = { QStringLiteral("单曲"), QStringLiteral("歌手"), QStringLiteral("专辑") };
    for (int i = 0; i < names.size(); ++i) {
        auto *btn = new QPushButton(names[i], tabRow);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton{border:none;background:transparent;color:#9A9AA5;font-size:14px;"
            "padding:7px 16px;border-radius:999px;}"
            "QPushButton:hover{background:rgba(255,255,255,0.08);color:#E8E8E8;}"
            "QPushButton:checked{background:rgba(236,65,65,0.16);color:#EC4141;font-weight:600;}"));
        group->addButton(btn, i);
        tabLayout->addWidget(btn);
    }
    tabLayout->addStretch(1);
    layout->addWidget(tabRow);

    m_stack = new QStackedWidget(this);
    m_songList = new SongListView;
    m_stack->addWidget(m_songList);

    auto *artistPage = new QWidget;
    auto *artistOuter = new QVBoxLayout(artistPage);
    artistOuter->setContentsMargins(0, 0, 0, 0);
    auto *artistScroll = new QScrollArea(artistPage);
    artistScroll->setWidgetResizable(true);
    artistScroll->setFrameShape(QFrame::NoFrame);
    auto *artistContent = new QWidget;
    m_artistLayout = new QVBoxLayout(artistContent);
    m_artistLayout->setContentsMargins(0, 0, 0, 0);
    m_artistLayout->setSpacing(6);
    artistScroll->setWidget(artistContent);
    artistOuter->addWidget(artistScroll);
    m_stack->addWidget(artistPage);

    auto *albumPage = new QWidget;
    auto *albumOuter = new QVBoxLayout(albumPage);
    albumOuter->setContentsMargins(0, 0, 0, 0);
    auto *albumScroll = new QScrollArea(albumPage);
    albumScroll->setWidgetResizable(true);
    albumScroll->setFrameShape(QFrame::NoFrame);
    auto *albumContent = new QWidget;
    m_albumLayout = new QVBoxLayout(albumContent);
    m_albumLayout->setContentsMargins(0, 0, 0, 0);
    m_albumLayout->setSpacing(6);
    albumScroll->setWidget(albumContent);
    albumOuter->addWidget(albumScroll);
    m_stack->addWidget(albumPage);

    layout->addWidget(m_stack, 1);
    connect(group, &QButtonGroup::idClicked, m_stack, &QStackedWidget::setCurrentIndex);

    connect(m_songList, &SongListView::playRequested, this, [this](int row) {
        emit playRequested(m_results, row);
    });
    connect(m_songList, &SongListView::heartRequested, this, &SearchPage::heartRequested);
    connect(m_songList, &SongListView::addToPlaylistRequested, this, &SearchPage::addToPlaylistRequested);
    connect(m_songList, &SongListView::removeFromPlaylistRequested, this, &SearchPage::removeFromPlaylistRequested);
    connect(m_songList, &SongListView::deleteFromLibraryRequested, this, &SearchPage::deleteFromLibraryRequested);
}

void SearchPage::performSearch(const QList<core::Song> &allSongs, const QString &query)
{
    m_results.clear();
    const auto results = core::SearchService::search(allSongs, query);
    for (const auto &r : results)
        m_results.append(allSongs[r.index]);

    m_title->setText(QStringLiteral("搜索 \"%1\" · %2 个结果").arg(query).arg(m_results.size()));
    m_songList->setSongs(m_results);
    m_songList->setHighlightQuery(query);

    while (QLayoutItem *item = m_artistLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto artists = core::SearchService::artists(m_results);
    for (const auto &a : artists) {
        if (!a.name.contains(query, Qt::CaseInsensitive))
            continue;
        auto *row = makeResultRow(a.name, QStringLiteral("%1 首").arg(a.count), this);
        connect(row, &QPushButton::clicked, this, [this, name = a.name] {
            emit artistClicked(name);
        });
        m_artistLayout->addWidget(row);
    }
    m_artistLayout->addStretch(1);

    while (QLayoutItem *item = m_albumLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto albums = core::SearchService::albums(m_results);
    for (const auto &a : albums) {
        if (!a.name.contains(query, Qt::CaseInsensitive) && !a.artist.contains(query, Qt::CaseInsensitive))
            continue;
        auto *row = makeResultRow(a.name, a.artist + QStringLiteral(" · %1 首").arg(a.count), this);
        connect(row, &QPushButton::clicked, this, [this, name = a.name, artist = a.artist] {
            emit albumClicked(name, artist);
        });
        m_albumLayout->addWidget(row);
    }
    m_albumLayout->addStretch(1);
}

} // namespace ui
