#include "PlayingPage.h"

#include "ui/ThemeManager.h"

#include "ui/CoverProvider.h"
#include "ui/LyricWidget.h"

#include "core/LyricsLoader.h"
#include "core/LibraryService.h"
#include "core/MusicSource.h"
#include "core/MusicSourceRegistry.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {

PlayingPage::PlayingPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("playingPage"));

    m_cover = new QLabel(this);
    m_cover->setFixedSize(320, 320);
    m_cover->setPixmap(CoverProvider::placeholder(QStringLiteral("乐"), 320, 14));

    m_title = new QLabel(QStringLiteral("未在播放"), this);
    setThemedStyleSheet(m_title, QStringLiteral("font-size:18px;font-weight:700;color:@textPrimary;"));
    m_artist = new QLabel(this);
    setThemedStyleSheet(m_artist, QStringLiteral("font-size:13px;color:@textSecondary;"));

    m_lyric = new LyricWidget(this);

    m_editBtn = new QPushButton(QStringLiteral("编辑歌词"), this);
    m_editBtn->setCursor(Qt::PointingHandCursor);
    setThemedStyleSheet(m_editBtn, QStringLiteral(
        "QPushButton{border:none;border-radius:15px;background:@surfaceAlt;color:@textSecondary;padding:5px 16px;}"
        "QPushButton:hover{background:@surfaceHover;color:@accentHover;}"));
    connect(m_editBtn, &QPushButton::clicked, this, &PlayingPage::editLyricsRequested);

    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->setExclusive(true);
    auto *modeRow = new QHBoxLayout;
    modeRow->setContentsMargins(0, 0, 0, 0);
    modeRow->setSpacing(8);
    const QStringList modes = { QStringLiteral("原文"), QStringLiteral("双语"), QStringLiteral("音译") };
    for (int i = 0; i < modes.size(); ++i) {
        auto *btn = new QPushButton(modes[i], this);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        setThemedStyleSheet(btn, QStringLiteral(
            "QPushButton{border:none;background:@surfaceAlt;color:@textSecondary;"
            "font-size:12px;padding:4px 14px;border-radius:999px;}"
            "QPushButton:hover{background:@surfaceHover;color:@textPrimary;}"
            "QPushButton:checked{background:@accentSoft;color:@accent;font-weight:600;}"));
        m_modeGroup->addButton(btn, i);
        modeRow->addWidget(btn);
    }
    modeRow->addStretch(1);
    connect(m_modeGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_lyricMode = id;
        applyLyricMode();
    });
    m_modeGroup->button(0)->setChecked(true);

    auto *right = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);
    rightLayout->addWidget(m_title, 0, Qt::AlignCenter);
    rightLayout->addWidget(m_artist, 0, Qt::AlignCenter);
    rightLayout->addWidget(m_lyric, 1);
    rightLayout->addLayout(modeRow);
    rightLayout->addWidget(m_editBtn, 0, Qt::AlignCenter);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(64, 30, 64, 30);
    layout->setSpacing(56);
    auto *left = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(14);
    auto *back = new QPushButton(QStringLiteral("返回"), left);
    back->setFixedSize(68, 32);
    back->setCursor(Qt::PointingHandCursor);
    setThemedStyleSheet(back, QStringLiteral(
        "QPushButton{border:none;background:@surfaceAlt;color:@textSecondary;border-radius:16px;}"
        "QPushButton:hover{background:@accentSoft;color:@accent;}"));
    leftLayout->addWidget(back, 0, Qt::AlignLeft);
    leftLayout->addWidget(m_cover);
    leftLayout->addStretch(1);
    connect(back, &QPushButton::clicked, this, &PlayingPage::backRequested);
    layout->addWidget(left);
    layout->addWidget(right, 1);

    connect(m_lyric, &LyricWidget::seekRequested, this, &PlayingPage::seekRequested);
}

void PlayingPage::setSong(const core::Song &song, const QPixmap &cover)
{
    m_song = song;
    m_coverPix = cover.isNull() ? CoverProvider::coverFor(song, 320, 14) : cover;
    m_cover->setPixmap(m_coverPix);
    m_title->setText(song.title.isEmpty() ? QFileInfo(song.filePath).completeBaseName() : song.title);
    m_artist->setText(song.artist.isEmpty() ? QStringLiteral("未知歌手") : song.artist);
}

void PlayingPage::setLyrics(const QList<core::LyricLine> &lines)
{
    m_lrc = lines;
    m_tlyrc.clear();
    m_romalrc.clear();
    applyLyricMode();
}

void PlayingPage::setSourceProvider(core::MusicSource *source)
{
    m_source = source;
}

void PlayingPage::setSourceRegistry(core::MusicSourceRegistry *registry)
{
    m_registry = registry;
}

void PlayingPage::loadLyricsFor(const core::Song &song)
{
    const quint64 requestGeneration = ++m_lyricRequestGeneration;

    // 本地 LRC 先立即显示，断网或在线接口返回空时也不会被清掉。
    m_lrc = core::LyricsLoader::load(song);
    m_tlyrc.clear();
    m_romalrc.clear();
    applyLyricMode();

    core::MusicSource *source = m_registry ? m_registry->sourceFor(song) : m_source;
    if (song.hasRemoteIdentity() && source) {
        const QPointer<PlayingPage> guard(this);
        source->lyric(song.effectiveRemoteId(), [guard, requestGeneration, song](const QString &lrc,
                                                                 const QString &tlyrc,
                                                                 const QString &romalrc) {
            if (!guard || requestGeneration != guard->m_lyricRequestGeneration)
                return;
            const QList<core::LyricLine> onlineLyrics = core::LrcParser::parseBytes(lrc.toUtf8());
            if (onlineLyrics.isEmpty())
                return;
            guard->m_lrc = onlineLyrics;
            guard->m_tlyrc = core::LrcParser::parseBytes(tlyrc.toUtf8());
            guard->m_romalrc = core::LrcParser::parseBytes(romalrc.toUtf8());
            if (guard->m_library && song.id > 0) {
                const QString path = guard->m_library->lyricCachePathFor(song);
                core::Song cachedSong = song;
                cachedSong.lyricPath = path;
                if (!path.isEmpty() && core::LyricsLoader::saveSidecar(cachedSong, lrc))
                    guard->m_library->setSongLyricPath(song.id, path);
            }
            guard->applyLyricMode();
        });
    }
}

void PlayingPage::applyLyricMode()
{
    if (m_lyricMode == 0)
        m_lyric->setLyrics(m_lrc);
    else if (m_lyricMode == 1)
        m_lyric->setLyrics(m_lrc, m_tlyrc);
    else
        m_lyric->setLyrics(m_romalrc.isEmpty() ? m_lrc : m_romalrc);
}

void PlayingPage::setPosition(qint64 ms)
{
    m_lyric->setPosition(ms);
}

void PlayingPage::setLyricFontSize(int px)
{
    m_lyric->setFontSize(px);
}

} // namespace ui
