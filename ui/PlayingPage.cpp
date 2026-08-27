#include "PlayingPage.h"

#include "ui/CoverProvider.h"
#include "ui/LyricWidget.h"

#include <QHBoxLayout>
#include <QFileInfo>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {

PlayingPage::PlayingPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("playingPage"));

    m_backdrop = new QLabel(this);
    m_backdrop->lower();

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
        "QPushButton{border:none;border-radius:15px;background:rgba(255,255,255,0.08);color:#C8C8D0;padding:5px 16px;}"
        "QPushButton:hover{background:rgba(236,65,65,0.16);color:#FF5A5A;}"));
    connect(m_editBtn, &QPushButton::clicked, this, &PlayingPage::editLyricsRequested);

    auto *right = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);
    rightLayout->addWidget(m_title, 0, Qt::AlignCenter);
    rightLayout->addWidget(m_artist, 0, Qt::AlignCenter);
    rightLayout->addWidget(m_lyric, 1);
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
    updateBackdrop();
}

void PlayingPage::setLyrics(const QList<core::LyricLine> &lines)
{
    m_lyric->setLyrics(lines);
}

void PlayingPage::setPosition(qint64 ms)
{
    m_lyric->setPosition(ms);
}

void PlayingPage::setLyricFontSize(int px)
{
    m_lyric->setFontSize(px);
}

void PlayingPage::resizeEvent(QResizeEvent *event)
{
    m_backdrop->setGeometry(rect());
    updateBackdrop();
    QWidget::resizeEvent(event);
}

void PlayingPage::updateBackdrop()
{
    if (m_coverPix.isNull())
        return;
    const QSize size = m_backdrop->size();
    if (size.isEmpty())
        return;
    QPixmap blurred = CoverProvider::blur(m_coverPix.scaled(size, Qt::KeepAspectRatioByExpanding,
                                                            Qt::SmoothTransformation),
                                          36, size);
    QPainter p(&blurred);
    p.fillRect(blurred.rect(), QColor(10, 10, 16, 200));
    p.end();
    m_backdrop->setPixmap(blurred);
}

} // namespace ui
