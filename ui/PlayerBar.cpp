#include "PlayerBar.h"

#include "ProgressSlider.h"
#include "ui/SvgIcon.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {

namespace {
QString formatTime(qint64 ms)
{
    qint64 total = qMax<qint64>(0, ms) / 1000;
    return QStringLiteral("%1:%2")
        .arg(total / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

QPixmap placeholderCover(const QString &text)
{
    QPixmap pm(60, 60);
    pm.fill(QColor(0xEC, 0x41, 0x41));
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::white);
    QFont f(QStringLiteral("Microsoft YaHei UI"), 14, QFont::Bold);
    p.setFont(f);
    p.drawText(pm.rect(), Qt::AlignCenter, text.left(1));
    return pm;
}
}

PlayerBar::PlayerBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("playerBar");
    setFixedSize(860, 80);

    m_cover = new QLabel(this);
    m_cover->setFixedSize(60, 60);
    m_cover->setPixmap(placeholderCover(QStringLiteral("乐")));

    m_title = new QLabel(QStringLiteral("未在播放"), this);
    m_title->setProperty("class", "nowTitle");
    m_artist = new QLabel(this);
    m_artist->setProperty("class", "nowSub");

    m_sourceBadge = new QLabel(this);
    m_sourceBadge->setStyleSheet(QStringLiteral(
        "QLabel{color:#8F8F9C;background:#2A2A36;"
        "border-radius:8px;padding:1px 7px;font-size:11px;}"));
    m_sourceBadge->setVisible(false);

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(6);
    titleRow->addWidget(m_title);
    titleRow->addWidget(m_sourceBadge);
    titleRow->addStretch(1);

    auto *infoBox = new QWidget(this);
    auto *infoLayout = new QVBoxLayout(infoBox);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(2);
    infoLayout->addLayout(titleRow);
    infoLayout->addWidget(m_artist);

    m_heartBtn = new QPushButton(this);
    m_heartBtn->setProperty("class", "ctrlBtn");
    m_heartBtn->setIcon(makeSvgIcon(QStringLiteral(":/icons/icon-heart.svg"), 18));
    m_heartBtn->setIconSize(QSize(18, 18));
    m_heartBtn->setFixedSize(30, 30);
    m_heartBtn->setCursor(Qt::PointingHandCursor);
    m_heartBtn->setToolTip(QStringLiteral("喜欢"));
    connect(m_heartBtn, &QPushButton::clicked, this, [this] {
        m_favorite = !m_favorite;
    m_heartBtn->setIcon(makeSvgIcon(m_favorite ? QStringLiteral(":/icons/icon-heart-fill.svg")
                                             : QStringLiteral(":/icons/icon-heart.svg"), 18));
        emit heartToggled(m_favorite);
    });

    m_downloadBtn = new QPushButton(this);
    m_downloadBtn->setProperty("class", "ctrlBtn");
    m_downloadBtn->setIcon(makeSvgIcon(QStringLiteral(":/icons/icon-download.svg"), 18));
    m_downloadBtn->setIconSize(QSize(18, 18));
    m_downloadBtn->setFixedSize(30, 30);
    m_downloadBtn->setCursor(Qt::PointingHandCursor);
    connect(m_downloadBtn, &QPushButton::clicked, this, &PlayerBar::downloadRequested);

    auto *leftBox = new QWidget(this);
    leftBox->setFixedWidth(240);
    auto *leftLayout = new QHBoxLayout(leftBox);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);
    leftLayout->addWidget(m_cover);
    leftLayout->addWidget(infoBox, 1);
    leftLayout->addWidget(m_heartBtn);
    leftLayout->addWidget(m_downloadBtn);

    m_progress = new ProgressSlider(this);
    m_progress->setRange(0, 1000);
    m_progress->setValue(0, false);

    m_timeCur = new QLabel(QStringLiteral("00:00"), this);
    m_timeCur->setProperty("class", "timeLabel");
    m_timeCur->setFixedWidth(40);
    m_timeCur->setAlignment(Qt::AlignCenter);
    m_timeTotal = new QLabel(QStringLiteral("00:00"), this);
    m_timeTotal->setProperty("class", "timeLabel");
    m_timeTotal->setFixedWidth(40);
    m_timeTotal->setAlignment(Qt::AlignCenter);

    auto *timeRow = new QHBoxLayout;
    timeRow->setContentsMargins(0, 0, 0, 0);
    timeRow->setSpacing(10);
    timeRow->addWidget(m_timeCur);
    timeRow->addWidget(m_progress, 1);
    timeRow->addWidget(m_timeTotal);

    auto makeCtrlButton = [this](const QString &icon, const QString &tip) {
        auto *btn = new QPushButton(this);
        btn->setProperty("class", "ctrlBtn");
        btn->setIcon(makeSvgIcon(icon, 20));
        btn->setIconSize(QSize(20, 20));
        btn->setFixedSize(30, 30);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
    };

    m_modeBtn = makeCtrlButton(QStringLiteral(":/icons/icon-loop.svg"), QStringLiteral("列表循环"));
    m_prevBtn = makeCtrlButton(QStringLiteral(":/icons/icon-prev.svg"), QStringLiteral("上一首"));
    m_playBtn = new QPushButton(this);
    m_playBtn->setObjectName("playPauseBtn");
    m_playBtn->setIcon(makeSvgIcon(QStringLiteral(":/icons/icon-play-white.svg"), 17));
    m_playBtn->setIconSize(QSize(17, 17));
    m_playBtn->setFixedSize(36, 36);
    m_playBtn->setCursor(Qt::PointingHandCursor);
    m_playBtn->setToolTip(QStringLiteral("播放"));
    m_nextBtn = makeCtrlButton(QStringLiteral(":/icons/icon-next.svg"), QStringLiteral("下一首"));

    m_muteBtn = makeCtrlButton(QStringLiteral(":/icons/icon-volume.svg"), QStringLiteral("静音"));
    m_volume = new ProgressSlider(this);
    m_volume->setFixedWidth(90);
    m_volume->setRange(0, 100);
    m_volume->setValue(70, false);

    auto *controls = new QHBoxLayout;
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setSpacing(20);
    controls->addWidget(m_prevBtn);
    controls->addWidget(m_playBtn);
    controls->addWidget(m_nextBtn);

    auto *centerBox = new QWidget(this);
    auto *centerLayout = new QVBoxLayout(centerBox);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(5);
    centerLayout->addLayout(timeRow);
    centerLayout->addLayout(controls, 0);
    centerLayout->setAlignment(controls, Qt::AlignCenter);

    m_lyricsBtn = makeCtrlButton(QStringLiteral(":/icons/icon-lyrics.svg"), QStringLiteral("歌词"));
    m_queueBtn = makeCtrlButton(QStringLiteral(":/icons/icon-queue.svg"), QStringLiteral("播放列表"));

    auto *rightBox = new QWidget(this);
    rightBox->setFixedWidth(240);
    auto *rightLayout = new QHBoxLayout(rightBox);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);
    rightLayout->addStretch(1);
    rightLayout->addWidget(m_modeBtn);
    rightLayout->addWidget(m_muteBtn);
    rightLayout->addWidget(m_volume);
    rightLayout->addWidget(m_lyricsBtn);
    rightLayout->addWidget(m_queueBtn);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(44, 0, 44, 0);
    layout->setSpacing(16);
    layout->addWidget(leftBox);
    layout->addWidget(centerBox, 1);
    layout->addWidget(rightBox);

    connect(m_modeBtn, &QPushButton::clicked, this, &PlayerBar::modeClicked);
    connect(m_prevBtn, &QPushButton::clicked, this, &PlayerBar::prevClicked);
    connect(m_playBtn, &QPushButton::clicked, this, &PlayerBar::playPauseClicked);
    connect(m_nextBtn, &QPushButton::clicked, this, &PlayerBar::nextClicked);
    connect(m_lyricsBtn, &QPushButton::clicked, this, &PlayerBar::lyricsClicked);
    connect(m_queueBtn, &QPushButton::clicked, this, &PlayerBar::playlistClicked);
    connect(m_muteBtn, &QPushButton::clicked, this, [this] {
        m_muted = !m_muted;
        updateVolumeIcon();
        emit muteToggled(m_muted);
    });
    connect(m_volume, &ProgressSlider::valueChanged, this, [this](int v) {
        m_volumeValue = v;
        if (m_muted) {
            m_muted = false;
            updateVolumeIcon();
        }
        updateVolumeIcon();
        emit volumeChanged(v);
    });
    connect(m_progress, &ProgressSlider::seekFinished, this, [this](int v) {
        if (m_durationMs > 0)
            emit seekRequested(qint64(v) * m_durationMs / 1000);
    });
}

void PlayerBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x1B, 0x1B, 0x24));
}

void PlayerBar::setSong(const core::Song &song, bool favorite)
{
    m_song = song;
    m_favorite = favorite;
    m_title->setText(song.title.isEmpty() ? QFileInfo(song.filePath).completeBaseName() : song.title);
    m_artist->setText(song.artist.isEmpty() ? QStringLiteral("未知歌手") : song.artist);
    if (song.isOnline())
        m_sourceBadge->setText(song.isDownloaded() ? QStringLiteral("☁ 已下载")
                               : song.isCached() ? QStringLiteral("☁ 已缓存") : QStringLiteral("☁ 在线"));
    else
        m_sourceBadge->setText(QStringLiteral("本地"));
    m_sourceBadge->setVisible(!m_sourceBadge->text().isEmpty());
    m_sourceBadge->setToolTip(QString());
    m_heartBtn->setIcon(makeSvgIcon(favorite ? QStringLiteral(":/icons/icon-heart-fill.svg")
                                       : QStringLiteral(":/icons/icon-heart.svg"), 18));
    const bool downloadable = song.isOnline() && !song.isDownloaded();
    m_downloadBtn->setEnabled(downloadable);
    m_downloadBtn->setToolTip(song.isOnline()
                                  ? (song.isDownloaded() ? QStringLiteral("已下载") : QStringLiteral("未下载，点击下载"))
                                  : QStringLiteral("本地歌曲无需下载"));
    if (!song.coverPath.isEmpty()) {
        QPixmap pm(song.coverPath);
        if (!pm.isNull())
            m_cover->setPixmap(pm.scaled(60, 60, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    } else {
        m_cover->setPixmap(placeholderCover(song.title.isEmpty() ? QStringLiteral("乐") : song.title.left(1)));
    }
    setDuration(song.durationMs);
    setPosition(0);
}

void PlayerBar::setPlaybackError(const QString &message)
{
    m_sourceBadge->setText(QStringLiteral("⚠ 播放失败"));
    m_sourceBadge->setToolTip(message);
    m_sourceBadge->setVisible(true);
}

void PlayerBar::setPlaying(bool playing)
{
    m_playing = playing;
    updatePlayIcon();
}

void PlayerBar::setPosition(qint64 ms)
{
    m_positionMs = ms;
    updateTimeLabel();
    if (m_durationMs > 0)
        m_progress->setValue(int(ms * 1000 / m_durationMs));
}

void PlayerBar::setDuration(qint64 ms)
{
    m_durationMs = ms;
    m_timeTotal->setText(formatTime(ms));
    if (ms > 0)
        m_progress->setValue(int(m_positionMs * 1000 / ms));
}

void PlayerBar::setVolume(int volume)
{
    m_volumeValue = volume;
    m_volume->setValue(volume);
    updateVolumeIcon();
}

void PlayerBar::setMuted(bool muted)
{
    m_muted = muted;
    updateVolumeIcon();
}

void PlayerBar::setMode(int mode)
{
    m_mode = mode;
    static const char *icons[] = { ":/icons/icon-loop.svg", ":/icons/icon-single.svg", ":/icons/icon-shuffle.svg" };
    static const char *tips[] = { "列表循环", "单曲循环", "随机播放" };
    if (mode >= 0 && mode <= 2) {
        m_modeBtn->setIcon(makeSvgIcon(QLatin1String(icons[mode]), 20));
        m_modeBtn->setToolTip(QLatin1String(tips[mode]));
    }
}

void PlayerBar::updatePlayIcon()
{
    m_playBtn->setIcon(makeSvgIcon(m_playing ? QStringLiteral(":/icons/icon-pause-white.svg")
                                       : QStringLiteral(":/icons/icon-play-white.svg"), 17));
    m_playBtn->setToolTip(m_playing ? QStringLiteral("暂停") : QStringLiteral("播放"));
}

void PlayerBar::updateVolumeIcon()
{
    const bool muted = m_muted || m_volumeValue == 0;
    m_muteBtn->setIcon(makeSvgIcon(muted ? QStringLiteral(":/icons/icon-volume-mute.svg")
                                   : QStringLiteral(":/icons/icon-volume.svg"), 20));
}

void PlayerBar::updateTimeLabel()
{
    m_timeCur->setText(formatTime(m_positionMs));
}

} // namespace ui
