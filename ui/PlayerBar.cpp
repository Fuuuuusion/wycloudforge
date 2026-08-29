#include "PlayerBar.h"

#include "ui/AuroraBackground.h"
#include "ProgressSlider.h"
#include "ui/SvgIcon.h"

#include <QFileInfo>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QImage>
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

// 真实高斯模糊:用 QGraphicsBlurEffect 渲染,比 1/N 降采样更通透、不泛灰
QImage gaussianBlur(const QImage &src, qreal radius)
{
    if (src.isNull())
        return src;
    QGraphicsScene scene;
    auto *item = scene.addPixmap(QPixmap::fromImage(src));
    auto *blur = new QGraphicsBlurEffect;
    blur->setBlurHints(QGraphicsBlurEffect::QualityHint);
    blur->setBlurRadius(radius);
    item->setGraphicsEffect(blur);
    QImage out(src.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    scene.render(&p, QRectF(0, 0, src.width(), src.height()));
    return out;
}

// 饱和度 + 亮度提升(亮度加权混合法,半透明像素保留原 alpha)
QImage tuneSaturationBrightness(const QImage &src, qreal sat, qreal brightness)
{
    QImage out = src.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < out.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            const QRgb c = line[x];
            const int r = qRed(c), g = qGreen(c), b = qBlue(c);
            const qreal gray = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            const qreal nr = (gray + (r - gray) * sat) * brightness;
            const qreal ng = (gray + (g - gray) * sat) * brightness;
            const qreal nb = (gray + (b - gray) * sat) * brightness;
            line[x] = qRgba(qBound(0, qRound(nr), 255),
                            qBound(0, qRound(ng), 255),
                            qBound(0, qRound(nb), 255), qAlpha(c));
        }
    }
    return out;
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
    const QRectF r = rect();
    const qreal radius = 40.0;

    // 背底缓存:内容+极光是缓变内容,约 120ms 刷新一次即可,避免每帧高成本模糊
    const qint64 now = m_clock.isValid() ? m_clock.elapsed() : 0;
    // 背景动画仍保持流动，但胶囊背底的截图和高斯模糊不需要每 120ms 重算。
    // 启动时网络回调密集，较低刷新频率可以避免与列表重建争抢 UI 线程。
    constexpr qint64 kBackdropInterval = 500;
    if (!m_backdropValid || now - m_backdropMs > kBackdropInterval) {
        const int pad = 30; // 模糊缓冲,避免边缘裁切生硬
        const QSize areaSize(size() + QSize(pad * 2, pad * 2));
        QImage area(areaSize, QImage::Format_ARGB32_Premultiplied);
        area.fill(QColor(0x0E, 0x0E, 0x14));
        QPainter ap(&area);
        ap.setRenderHint(QPainter::Antialiasing);
        const qreal t = m_clock.isValid() ? m_clock.elapsed() / 1000.0 : 0.0;
        AuroraBackground::renderScene(&ap, QRectF(QPointF(0, 0), QSizeF(area.size())), t);

        // 抓取胶囊背后真实内容(内容区 body),叠加到极光之上,实现"内容透出"
        if (m_backdropSource && m_backdropSource->isVisible()) {
            const QPoint topLeft = m_backdropSource->mapFrom(this, QPoint(0, 0));
            const QRect src(topLeft, size());
            const QRect clip = src.adjusted(-pad, -pad, pad, pad);
            const QPixmap grab = m_backdropSource->grab(clip);
            if (!grab.isNull())
                ap.drawPixmap(QRect(0, 0, clip.width(), clip.height()), grab);
        }
        ap.end();

        QImage blurred = gaussianBlur(area, 16.0);
        blurred = tuneSaturationBrightness(blurred, 1.6, 1.12);
        const QImage cropped = blurred.copy(pad, pad, width(), height());
        m_backdrop = QPixmap::fromImage(cropped);
        m_backdropValid = true;
        m_backdropMs = now;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 玻璃主体(裁剪圆角):模糊+饱和+亮度背底
    QPainterPath clip;
    clip.addRoundedRect(r, radius, radius);
    p.save();
    p.setClipPath(clip);
    p.drawPixmap(r.toRect(), m_backdrop);

    // 白色渐变膜:linear-gradient(135deg, .11, .04 45%, .09)
    QLinearGradient glass(r.topLeft(), r.bottomRight());
    glass.setColorAt(0.0, QColor(255, 255, 255, 28));
    glass.setColorAt(0.45, QColor(255, 255, 255, 10));
    glass.setColorAt(1.0, QColor(255, 255, 255, 23));
    p.fillRect(r, glass);

    // 斜向反光折射带(仿 ::before 105deg 高光)
    p.save();
    p.translate(r.center());
    p.rotate(15.0);
    QLinearGradient sheen(QPointF(-r.width() * 0.6, 0), QPointF(r.width() * 0.6, 0));
    sheen.setColorAt(0.0, QColor(255, 255, 255, 0));
    sheen.setColorAt(0.42, QColor(255, 255, 255, 0));
    sheen.setColorAt(0.5, QColor(255, 255, 255, 46));
    sheen.setColorAt(0.58, QColor(255, 255, 255, 0));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.fillRect(QRectF(-r.width() * 0.7, -r.height(), r.width() * 1.4, r.height() * 2.0), sheen);
    p.restore();
    p.restore();

    // 1px 边缘光:border rgba(255,255,255,.14)
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 22), 1));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

void PlayerBar::setBackdropSource(QWidget *widget)
{
    m_backdropSource = widget;
    m_backdropValid = false; // 让下一帧重新取样
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
