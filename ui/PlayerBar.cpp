#include "PlayerBar.h"

#include "ui/AuroraBackground.h"
#include "ProgressSlider.h"
#include "ui/SvgIcon.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
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
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c1(0xEC, 0x41, 0x41);
    const QColor c2(0xFF, 0x9A, 0x76);
    QLinearGradient g(0, 0, 60, 60);
    g.setColorAt(0, c1);
    g.setColorAt(1, c2);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(0, 0, 60, 60, 6, 6);
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
    setAttribute(Qt::WA_TranslucentBackground);
    m_clock.start();

    m_cover = new QLabel(this);
    m_cover->setFixedSize(60, 60);
    m_cover->setPixmap(placeholderCover(QStringLiteral("乐")));

    m_title = new QLabel(QStringLiteral("未在播放"), this);
    m_title->setProperty("class", "nowTitle");
    m_artist = new QLabel(this);
    m_artist->setProperty("class", "nowSub");

    m_sourceBadge = new QLabel(this);
    m_sourceBadge->setStyleSheet(QStringLiteral(
        "QLabel{color:#8F8F9C;background:rgba(255,255,255,0.06);"
        "border-radius:8px;padding:1px 7px;font-size:11px;}"));

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

    auto *leftBox = new QWidget(this);
    leftBox->setFixedWidth(250);
    auto *leftLayout = new QHBoxLayout(leftBox);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);
    leftLayout->addWidget(m_cover);
    leftLayout->addWidget(infoBox, 1);
    leftLayout->addWidget(m_heartBtn);

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
    controls->addWidget(m_modeBtn);
    controls->addWidget(m_prevBtn);
    controls->addWidget(m_playBtn);
    controls->addWidget(m_nextBtn);
    controls->addSpacing(6);
    controls->addWidget(m_muteBtn);
    controls->addWidget(m_volume);

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
    rightBox->setFixedWidth(110);
    auto *rightLayout = new QHBoxLayout(rightBox);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(2);
    rightLayout->addStretch(1);
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
    const QRectF r = rect();

    // 玻璃背底:取当前极光场景,快速降采样模糊得到雾透质感
    QPixmap back(size());
    back.fill(Qt::transparent);
    QPainter bp(&back);
    const qreal t = m_clock.isValid() ? m_clock.elapsed() / 1000.0 : 0.0;
    AuroraBackground::renderScene(&bp, r, t);
    bp.end();
    const QPixmap frosted =
        back.scaled(qMax(1, width() / 10), qMax(1, height() / 10), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .scaled(size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 投影
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 70));
    p.drawRoundedRect(r.translated(0, 8), 40, 40);

    // 玻璃主体(裁剪圆角):模糊背底 + 玻璃渐变
    QPainterPath clip;
    clip.addRoundedRect(r, 40, 40);
    p.save();
    p.setClipPath(clip);
    p.drawPixmap(r.toRect(), frosted);
    QLinearGradient glass(r.topLeft(), r.bottomLeft());
    glass.setColorAt(0.0, QColor(255, 255, 255, 26));
    glass.setColorAt(0.45, QColor(255, 255, 255, 8));
    glass.setColorAt(1.0, QColor(255, 255, 255, 20));
    p.fillRect(r, glass);
    // 斜向反光折射
    p.save();
    p.translate(r.center());
    p.rotate(18.0);
    QLinearGradient sheen(QPointF(-r.width() * 0.6, 0), QPointF(r.width() * 0.6, 0));
    sheen.setColorAt(0.0, QColor(255, 255, 255, 0));
    sheen.setColorAt(0.5, QColor(255, 255, 255, 28));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.fillRect(QRectF(-r.width() * 0.7, -r.height(), r.width() * 1.4, r.height() * 2.0), sheen);
    p.restore();
    p.restore();

    // 边缘反光:1px 高光描边 + 顶部内高光
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 36), 1));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 40, 40);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 22));
    p.drawRoundedRect(QRectF(r.left() + 6, r.top() + 2, r.width() - 12, r.height() * 0.42), 36, 36);
}

void PlayerBar::setSong(const core::Song &song, bool favorite)
{
    m_song = song;
    m_favorite = favorite;
    m_title->setText(song.title.isEmpty() ? QFileInfo(song.filePath).completeBaseName() : song.title);
    m_artist->setText(song.artist.isEmpty() ? QStringLiteral("未知歌手") : song.artist);
    if (song.isOnline())
        m_sourceBadge->setText(song.isCached() ? QStringLiteral("☁ 已缓存") : QStringLiteral("☁ 在线"));
    else
        m_sourceBadge->setText(QStringLiteral("本地"));
    m_heartBtn->setIcon(makeSvgIcon(favorite ? QStringLiteral(":/icons/icon-heart-fill.svg")
                                       : QStringLiteral(":/icons/icon-heart.svg"), 18));
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
