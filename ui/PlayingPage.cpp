#include "PlayingPage.h"

#include "ui/CoverProvider.h"
#include "ui/LyricWidget.h"

#include "core/LyricsLoader.h"
#include "core/MusicSource.h"

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
    m_title->setStyleSheet(QStringLiteral("font-size:18px;font-weight:700;color:#E8E8E8;"));
    m_artist = new QLabel(this);
    m_artist->setStyleSheet(QStringLiteral("font-size:13px;color:#9A9AA5;"));

    m_lyric = new LyricWidget(this);

    m_editBtn = new QPushButton(QStringLiteral("编辑歌词"), this);
    m_editBtn->setCursor(Qt::PointingHandCursor);
    m_editBtn->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;border-radius:15px;background:#1B1B24;color:#C8C8D0;padding:5px 16px;}"
        "QPushButton:hover{background:#2A2A36;color:#FF5A5A;}"));
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
        btn->setStyleSheet(QStringLiteral(
            "QPushButton{border:none;background:#1B1B24;color:#9A9AA5;"
            "font-size:12px;padding:4px 14px;border-radius:999px;}"
            "QPushButton:hover{background:#2A2A36;color:#E8E8E8;}"
            "QPushButton:checked{background:#3A2024;color:#EC4141;font-weight:600;}"));
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
    layout->addWidget(m_cover);
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

void PlayingPage::loadLyricsFor(const core::Song &song)
{
    const quint64 requestGeneration = ++m_lyricRequestGeneration;

    // 本地 LRC 先立即显示，断网或在线接口返回空时也不会被清掉。
    m_lrc = core::LyricsLoader::load(song);
    m_tlyrc.clear();
    m_romalrc.clear();
    applyLyricMode();

    if (song.isOnline() && song.onlineId > 0 && m_source) {
        const QPointer<PlayingPage> guard(this);
        m_source->lyric(song.onlineId, [guard, requestGeneration](const QString &lrc, const QString &tlyrc,
                                                                 const QString &romalrc) {
            if (!guard || requestGeneration != guard->m_lyricRequestGeneration)
                return;
            const QList<core::LyricLine> onlineLyrics = core::LrcParser::parseBytes(lrc.toUtf8());
            if (onlineLyrics.isEmpty())
                return;
            guard->m_lrc = onlineLyrics;
            guard->m_tlyrc = core::LrcParser::parseBytes(tlyrc.toUtf8());
            guard->m_romalrc = core::LrcParser::parseBytes(romalrc.toUtf8());
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
