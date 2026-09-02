#include "PlayerBar.h"

#include "CoverProvider.h"
#include "ProgressSlider.h"
#include "ui/SvgIcon.h"
#include "ui/ThemeManager.h"

#include <QEvent>
#include <QFileInfo>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolTip>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QtMath>

namespace ui {

namespace {
QString formatTime(qint64 ms)
{
    qint64 total = qMax<qint64>(0, ms) / 1000;
    return QStringLiteral("%1:%2")
        .arg(total / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

class ElidedLabel final : public QLabel
{
public:
    explicit ElidedLabel(QWidget *parent = nullptr)
        : QLabel(parent)
    {
    }

    void setFullText(const QString &text)
    {
        m_fullText = text;
        setToolTip(text);
        updateElidedText();
    }

    QString fullText() const { return m_fullText; }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateElidedText();
    }

    void changeEvent(QEvent *event) override
    {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
            updateElidedText();
    }

private:
    void updateElidedText()
    {
        QLabel::setText(fontMetrics().elidedText(m_fullText, Qt::ElideRight, qMax(0, width())));
    }

    QString m_fullText;
};

QPixmap tintedSvg(const QString &path, const QColor &color, qreal dpr)
{
    QSvgRenderer renderer(path);
    const int pixels = qMax(1, qRound(30.0 * dpr));
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, pixels, pixels));
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), color);
    painter.end();

    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

QColor blendedColor(const QColor &from, const QColor &to, qreal progress)
{
    const qreal t = qBound<qreal>(0.0, progress, 1.0);
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                            from.greenF() + (to.greenF() - from.greenF()) * t,
                            from.blueF() + (to.blueF() - from.blueF()) * t,
                            from.alphaF() + (to.alphaF() - from.alphaF()) * t);
}

class FavoriteButton final : public QPushButton
{
public:
    explicit FavoriteButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setFixedSize(30, 30);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("喜欢"));
        setAccessibleName(QStringLiteral("喜欢"));
        setProperty("hoverProgress", 0.0);

        m_hoverAnimation.setDuration(200);
        connect(&m_hoverAnimation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) {
            setProperty("hoverProgress", value);
            update();
        });
        m_bounceAnimation.setDuration(200);
        m_bounceAnimation.setStartValue(0.0);
        m_bounceAnimation.setEndValue(1.0);
        connect(&m_bounceAnimation, &QVariantAnimation::valueChanged, this, [this] { update(); });
    }

    void setFavorite(bool favorite, bool animate)
    {
        if (m_favorite == favorite)
            return;
        m_favorite = favorite;
        const QString label = favorite ? QStringLiteral("取消喜欢") : QStringLiteral("喜欢");
        setToolTip(label);
        setAccessibleName(label);
        if (animate) {
            m_bounceAnimation.stop();
            m_bounceAnimation.start();
        } else {
            m_bounceAnimation.stop();
        }
        update();
    }

protected:
    void enterEvent(QEnterEvent *event) override
    {
        animateHoverTo(1.0);
        m_hovered = true;
        QPushButton::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        animateHoverTo(0.0);
        m_hovered = false;
        QPushButton::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        QPushButton::mousePressEvent(event);
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        QPushButton::mouseReleaseEvent(event);
        update();
    }

    void focusInEvent(QFocusEvent *event) override
    {
        m_showFocusRing = event->reason() == Qt::TabFocusReason
                          || event->reason() == Qt::BacktabFocusReason
                          || event->reason() == Qt::ShortcutFocusReason;
        QPushButton::focusInEvent(event);
        update();
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        m_showFocusRing = false;
        QPushButton::focusOutEvent(event);
        update();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        if (hasFocus() && m_showFocusRing) {
            painter.setPen(QPen(themeColor(ThemeColor::TextTertiary), 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(rect()).adjusted(3, 3, -3, -3), 7, 7);
        }

        QColor color;
        if (!isEnabled())
            color = themeColor(ThemeColor::TextTertiary);
        else if (isDown())
            color = themeColor(ThemeColor::AccentPressed);
        else if (m_favorite)
            color = themeColor(ThemeColor::Accent);
        else
            color = blendedColor(themeColor(ThemeColor::TextSecondary),
                                 themeColor(ThemeColor::AccentHover), hoverProgress());

        qreal scale = 1.0 + 0.06 * hoverProgress();
        if (isDown())
            scale = 0.92;
        if (m_bounceAnimation.state() == QAbstractAnimation::Running) {
            const qreal t = m_bounceAnimation.currentValue().toReal();
            const qreal amplitude = m_favorite ? 0.14 : -0.06;
            scale += amplitude * qSin(M_PI * t);
        }

        ensurePixmap(color);
        const qreal iconSize = 18.0 * scale;
        const QRectF target((width() - iconSize) / 2.0, (height() - iconSize) / 2.0,
                            iconSize, iconSize);
        painter.drawPixmap(target, m_pixmap, QRectF(m_pixmap.rect()));
    }

private:
    void animateHoverTo(qreal target)
    {
        const qreal current = hoverProgress();
        m_hoverAnimation.stop();
        m_hoverAnimation.setStartValue(current);
        m_hoverAnimation.setEndValue(target);
        m_hoverAnimation.start();
    }

    qreal hoverProgress() const
    {
        return m_hoverAnimation.state() == QAbstractAnimation::Running
            ? m_hoverAnimation.currentValue().toReal()
            : (m_hovered ? 1.0 : 0.0);
    }

    void ensurePixmap(const QColor &color)
    {
        const qreal dpr = devicePixelRatioF();
        if (m_cachedColor == color && qFuzzyCompare(m_cachedDpr, dpr))
            return;
        m_cachedColor = color;
        m_cachedDpr = dpr;
        m_pixmap = tintedSvg(m_favorite ? QStringLiteral(":/icons/icon-heart-fill.svg")
                                        : QStringLiteral(":/icons/icon-heart.svg"), color, dpr);
    }

    QVariantAnimation m_hoverAnimation;
    QVariantAnimation m_bounceAnimation;
    QPixmap m_pixmap;
    QColor m_cachedColor;
    qreal m_cachedDpr = 0.0;
    bool m_favorite = false;
    bool m_showFocusRing = false;
    bool m_hovered = false;
};

class DownloadStateButton final : public QPushButton
{
public:
    enum State { Unavailable, Download, Loading, Delete };

    explicit DownloadStateButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setFixedSize(30, 30);
        setCursor(Qt::PointingHandCursor);
        setProperty("hoverProgress", 0.0);

        m_hoverAnimation.setDuration(200);
        connect(&m_hoverAnimation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) {
            setProperty("hoverProgress", value);
            update();
        });
        m_stateAnimation.setDuration(200);
        m_stateAnimation.setStartValue(0.0);
        m_stateAnimation.setEndValue(1.0);
        connect(&m_stateAnimation, &QVariantAnimation::valueChanged, this, [this] { update(); });

        m_loadingTimer.setInterval(33);
        connect(&m_loadingTimer, &QTimer::timeout, this, [this] {
            m_loadingAngle = (m_loadingAngle + 16) % 360;
            if (isVisible() && !window()->isMinimized())
                update();
        });
        applyStateMetadata();
    }

    void setState(State state, bool animate)
    {
        if (m_state == state)
            return;
        m_fromState = m_state;
        m_state = state;
        applyStateMetadata();
        if (state == Loading || m_fromState == Loading)
            m_loadingTimer.start();
        if (animate) {
            m_stateAnimation.stop();
            m_stateAnimation.start();
        } else {
            m_stateAnimation.stop();
            if (state != Loading)
                m_loadingTimer.stop();
        }
        update();
    }

    State state() const { return m_state; }

protected:
    void enterEvent(QEnterEvent *event) override
    {
        animateHoverTo(1.0);
        m_hovered = true;
        QPushButton::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        animateHoverTo(0.0);
        m_hovered = false;
        QPushButton::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        QPushButton::mousePressEvent(event);
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        QPushButton::mouseReleaseEvent(event);
        update();
    }

    void focusInEvent(QFocusEvent *event) override
    {
        m_showFocusRing = event->reason() == Qt::TabFocusReason
                          || event->reason() == Qt::BacktabFocusReason
                          || event->reason() == Qt::ShortcutFocusReason;
        QPushButton::focusInEvent(event);
        update();
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        m_showFocusRing = false;
        QPushButton::focusOutEvent(event);
        update();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        if (m_stateAnimation.state() == QAbstractAnimation::Running) {
            const qreal progress = m_stateAnimation.currentValue().toReal();
            drawState(painter, m_fromState, 1.0 - progress, progress);
            drawState(painter, m_state, progress, 0.0);
            if (progress >= 0.99 && m_state != Loading)
                m_loadingTimer.stop();
        } else {
            drawState(painter, m_state, 1.0, 0.0);
            if (m_state != Loading)
                m_loadingTimer.stop();
        }

        if (hasFocus() && m_showFocusRing) {
            painter.setOpacity(1.0);
            painter.setPen(QPen(themeColor(ThemeColor::TextTertiary), 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(rect()).adjusted(3, 3, -3, -3), 7, 7);
        }
    }

private:
    void applyStateMetadata()
    {
        QString label;
        switch (m_state) {
        case Download: label = QStringLiteral("下载"); break;
        case Loading: label = QStringLiteral("下载中"); break;
        case Delete: label = QStringLiteral("删除下载"); break;
        case Unavailable: label = QStringLiteral("本地歌曲无需下载"); break;
        }
        setToolTip(label);
        setAccessibleName(label);
        setEnabled(m_state == Download || m_state == Delete);
        setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }

    void animateHoverTo(qreal target)
    {
        const qreal current = hoverProgress();
        m_hoverAnimation.stop();
        m_hoverAnimation.setStartValue(current);
        m_hoverAnimation.setEndValue(target);
        m_hoverAnimation.start();
    }

    qreal hoverProgress() const
    {
        return m_hoverAnimation.state() == QAbstractAnimation::Running
            ? m_hoverAnimation.currentValue().toReal()
            : (m_hovered && isEnabled() ? 1.0 : 0.0);
    }

    void drawState(QPainter &painter, State state, qreal opacity, qreal transitionProgress)
    {
        if (opacity <= 0.0)
            return;
        painter.save();
        painter.setOpacity(opacity);
        const qreal hover = hoverProgress();
        const QColor color = !isEnabled() && state != Loading
            ? themeColor(ThemeColor::TextTertiary)
            : isDown() ? themeColor(ThemeColor::AccentPressed)
            : blendedColor(themeColor(ThemeColor::TextSecondary),
                           themeColor(ThemeColor::AccentHover), hover);
        const QPointF center = QRectF(rect()).center();

        if (state == Loading) {
            painter.setPen(QPen(themeColor(ThemeColor::AccentHover), 1.8,
                                Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(QRectF(center.x() - 8, center.y() - 8, 16, 16),
                            (90 - m_loadingAngle) * 16, -270 * 16);
        } else if (state == Delete) {
            const QPixmap trash = tintedSvg(QStringLiteral(":/icons/icon-trash.svg"), color,
                                             devicePixelRatioF());
            const qreal offset = 2.0 * hover;
            painter.drawPixmap(QRectF(center.x() - 8, center.y() - 8 + offset, 16, 16),
                               trash, QRectF(trash.rect()));
        } else {
            const qreal arrowOffset = 2.0 * hover + 3.0 * transitionProgress;
            painter.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawLine(QPointF(center.x(), center.y() - 7 + arrowOffset),
                             QPointF(center.x(), center.y() + 1 + arrowOffset));
            painter.drawLine(QPointF(center.x() - 3.5, center.y() - 2.5 + arrowOffset),
                             QPointF(center.x(), center.y() + 1 + arrowOffset));
            painter.drawLine(QPointF(center.x() + 3.5, center.y() - 2.5 + arrowOffset),
                             QPointF(center.x(), center.y() + 1 + arrowOffset));
            painter.drawLine(QPointF(center.x() - 6, center.y() + 5),
                             QPointF(center.x() + 6, center.y() + 5));
            painter.drawLine(QPointF(center.x() - 6, center.y() + 5),
                             QPointF(center.x() - 6, center.y() + 3));
            painter.drawLine(QPointF(center.x() + 6, center.y() + 5),
                             QPointF(center.x() + 6, center.y() + 3));
        }
        painter.restore();
    }

    QVariantAnimation m_hoverAnimation;
    QVariantAnimation m_stateAnimation;
    QTimer m_loadingTimer;
    State m_state = Unavailable;
    State m_fromState = Unavailable;
    int m_loadingAngle = 0;
    bool m_showFocusRing = false;
    bool m_hovered = false;
};

class DirectionalButton final : public QPushButton
{
public:
    DirectionalButton(const QString &iconPath, int direction,
                      const QString &label, QWidget *parent = nullptr)
        : QPushButton(parent)
        , m_iconPath(iconPath)
        , m_direction(direction)
    {
        setFixedSize(30, 30);
        setCursor(Qt::PointingHandCursor);
        setToolTip(label);
        setAccessibleName(label);

        m_hoverAnimation.setDuration(200);
        connect(&m_hoverAnimation, &QVariantAnimation::valueChanged, this, [this] { update(); });
        m_pressAnimation.setDuration(90);
        connect(&m_pressAnimation, &QVariantAnimation::valueChanged, this, [this] { update(); });
    }

protected:
    void enterEvent(QEnterEvent *event) override
    {
        animate(m_hoverAnimation, hoverProgress(), 1.0);
        QPushButton::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        animate(m_hoverAnimation, hoverProgress(), 0.0);
        QPushButton::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        animate(m_pressAnimation, pressProgress(), 1.0);
        QPushButton::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        animate(m_pressAnimation, pressProgress(), 0.0);
        QPushButton::mouseReleaseEvent(event);
    }

    void focusInEvent(QFocusEvent *event) override
    {
        m_showFocusRing = event->reason() == Qt::TabFocusReason
                          || event->reason() == Qt::BacktabFocusReason
                          || event->reason() == Qt::ShortcutFocusReason;
        QPushButton::focusInEvent(event);
        update();
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        m_showFocusRing = false;
        QPushButton::focusOutEvent(event);
        update();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal hover = isEnabled() ? hoverProgress() : 0.0;
        const qreal press = isEnabled() ? pressProgress() : 0.0;

        QColor background = themeColor(ThemeColor::SurfaceHover);
        background.setAlphaF(hover);
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawEllipse(QRectF(rect()).adjusted(1, 1, -1, -1));

        const QColor color = !isEnabled() ? themeColor(ThemeColor::TextTertiary)
            : blendedColor(themeColor(ThemeColor::TextSecondary),
                           themeColor(ThemeColor::TextPrimary), hover);
        const QPixmap icon = tintedSvg(m_iconPath, color, devicePixelRatioF());
        const qreal scale = 1.0 - 0.08 * press;
        const qreal iconSize = 20.0 * scale;
        const qreal offset = qreal(m_direction) * 2.0 * hover;
        const QPointF center = QRectF(rect()).center() + QPointF(offset, 0);
        painter.drawPixmap(QRectF(center.x() - iconSize / 2.0,
                                  center.y() - iconSize / 2.0, iconSize, iconSize),
                           icon, QRectF(icon.rect()));

        if (hasFocus() && m_showFocusRing) {
            painter.setPen(QPen(themeColor(ThemeColor::TextTertiary), 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QRectF(rect()).adjusted(3, 3, -3, -3));
        }
    }

private:
    static void animate(QVariantAnimation &animation, qreal from, qreal to)
    {
        animation.stop();
        animation.setStartValue(from);
        animation.setEndValue(to);
        animation.start();
    }

    qreal hoverProgress() const
    {
        return m_hoverAnimation.state() == QAbstractAnimation::Running
            ? m_hoverAnimation.currentValue().toReal()
            : (underMouse() ? 1.0 : 0.0);
    }

    qreal pressProgress() const
    {
        return m_pressAnimation.state() == QAbstractAnimation::Running
            ? m_pressAnimation.currentValue().toReal()
            : (isDown() ? 1.0 : 0.0);
    }

    QString m_iconPath;
    int m_direction = 0;
    QVariantAnimation m_hoverAnimation;
    QVariantAnimation m_pressAnimation;
    bool m_showFocusRing = false;
};

class AnimatedPlayPauseButton final : public QPushButton
{
public:
    explicit AnimatedPlayPauseButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setObjectName(QStringLiteral("playPauseBtn"));
        setFixedSize(40, 40);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("播放"));
        setAccessibleName(QStringLiteral("播放"));

        m_animation.setDuration(500);
        m_animation.setStartValue(0.0);
        m_animation.setEndValue(1.0);
        connect(&m_animation, &QVariantAnimation::valueChanged, this, [this] { update(); });
        connect(&m_animation, &QVariantAnimation::finished, this, [this] {
            m_fromPlaying = m_playing;
            update();
        });
    }

    void setPlaying(bool playing)
    {
        if (m_playing == playing)
            return;

        m_fromPlaying = m_playing;
        m_playing = playing;
        m_animation.stop();
        m_animation.start();
        update();
    }

protected:
    void focusInEvent(QFocusEvent *event) override
    {
        m_showFocusRing = event->reason() == Qt::TabFocusReason
                          || event->reason() == Qt::BacktabFocusReason
                          || event->reason() == Qt::ShortcutFocusReason;
        QPushButton::focusInEvent(event);
        update();
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        m_showFocusRing = false;
        QPushButton::focusOutEvent(event);
        update();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        if (hasFocus() && m_showFocusRing) {
            QPen focusPen(themeColor(ThemeColor::TextTertiary), 2.0);
            painter.setPen(focusPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QRectF(rect()).adjusted(3, 3, -3, -3));
        }

        QColor color = themeColor(ThemeColor::TextPrimary);
        if (!isEnabled())
            color = themeColor(ThemeColor::TextTertiary);
        else if (isDown())
            color = themeColor(ThemeColor::TextSecondary);
        else if (underMouse())
            color = themeColor(ThemeColor::TextPrimary);
        ensurePixmaps(color);

        if (m_animation.state() != QAbstractAnimation::Running) {
            drawIcon(painter, m_playing, 1.0, 0.0, 1.0);
            return;
        }

        const qreal progress = m_animation.currentValue().toReal();
        const qreal outgoingProgress = qMin<qreal>(1.0, progress * 2.0);
        drawIcon(painter,
                 m_fromPlaying,
                 1.0 - outgoingProgress,
                 180.0 * outgoingProgress,
                 1.0 - outgoingProgress);

        qreal incomingRotation = 0.0;
        qreal incomingScale = 1.0;
        qreal incomingOpacity = 1.0;
        if (progress <= 0.5) {
            const qreal firstHalf = progress * 2.0;
            incomingRotation = -180.0 + 170.0 * firstHalf;
            incomingScale = 1.2 * firstHalf;
            incomingOpacity = firstHalf;
        } else {
            const qreal secondHalf = (progress - 0.5) * 2.0;
            incomingRotation = -10.0 + 10.0 * secondHalf;
            incomingScale = 1.2 - 0.2 * secondHalf;
        }
        drawIcon(painter, m_playing, incomingOpacity, incomingRotation, incomingScale);
    }

private:
    void ensurePixmaps(const QColor &color)
    {
        const qreal dpr = devicePixelRatioF();
        if (m_cachedColor == color && qFuzzyCompare(m_cachedDpr, dpr))
            return;

        m_cachedColor = color;
        m_cachedDpr = dpr;
        m_playPixmap = tintedSvg(QStringLiteral(":/icons/icon-play-white.svg"), color, dpr);
        m_pausePixmap = tintedSvg(QStringLiteral(":/icons/icon-pause-white.svg"), color, dpr);
    }

    void drawIcon(QPainter &painter,
                  bool playing,
                  qreal opacity,
                  qreal rotation,
                  qreal scale)
    {
        if (opacity <= 0.0 || scale <= 0.0)
            return;

        const QPixmap &pixmap = playing ? m_pausePixmap : m_playPixmap;
        painter.save();
        painter.setOpacity(opacity);
        painter.translate(QRectF(rect()).center());
        painter.rotate(rotation);
        painter.scale(scale, scale);
        painter.drawPixmap(QPointF(-15.0, -15.0), pixmap);
        painter.restore();
    }

    QVariantAnimation m_animation;
    QPixmap m_playPixmap;
    QPixmap m_pausePixmap;
    QColor m_cachedColor;
    qreal m_cachedDpr = 0.0;
    bool m_playing = false;
    bool m_fromPlaying = false;
    bool m_showFocusRing = false;
};
}

PlayerBar::PlayerBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("playerBar");
    setFixedHeight(204);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_StyledBackground, true);

    m_cover = new QLabel(this);
    m_cover->setObjectName(QStringLiteral("playerCover"));
    m_cover->setFixedSize(128, 128);
    m_cover->setPixmap(CoverProvider::placeholder(QStringLiteral("乐"), 128, 12));

    auto *titleLabel = new ElidedLabel(this);
    titleLabel->setFullText(QStringLiteral("未在播放"));
    m_title = titleLabel;
    m_title->setObjectName(QStringLiteral("playerTitle"));
    m_title->setProperty("class", "nowTitle");
    m_title->setMinimumWidth(0);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *artistLabel = new ElidedLabel(this);
    artistLabel->setFullText(QString());
    m_artist = artistLabel;
    m_artist->setObjectName(QStringLiteral("playerArtist"));
    m_artist->setProperty("class", "nowSub");
    m_artist->setMinimumWidth(0);
    m_artist->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_infoBox = new QWidget(this);
    m_infoBox->setMinimumWidth(0);
    m_infoBox->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *infoLayout = new QVBoxLayout(m_infoBox);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(4);
    infoLayout->addWidget(m_title);
    infoLayout->addWidget(m_artist);

    m_heartBtn = new FavoriteButton(this);
    m_heartBtn->setObjectName(QStringLiteral("favoriteActionButton"));
    connect(m_heartBtn, &QPushButton::clicked, this, [this] {
        m_favorite = !m_favorite;
        static_cast<FavoriteButton *>(m_heartBtn)->setFavorite(m_favorite, true);
        emit heartToggled(m_favorite);
    });

    m_downloadBtn = new DownloadStateButton(this);
    m_downloadBtn->setObjectName(QStringLiteral("downloadActionButton"));
    connect(m_downloadBtn, &QPushButton::clicked, this, [this] {
        const auto state = static_cast<DownloadStateButton *>(m_downloadBtn)->state();
        if (state == DownloadStateButton::Download)
            emit downloadRequested();
        else if (state == DownloadStateButton::Delete)
            emit deleteDownloadRequested();
    });

    m_leftBox = new QWidget(this);
    m_leftBox->setObjectName(QStringLiteral("playerLeftBox"));
    m_leftBox->setMinimumWidth(270);
    m_leftBox->setMaximumWidth(360);
    m_leftBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto *leftLayout = new QHBoxLayout(m_leftBox);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);
    leftLayout->addWidget(m_cover);
    leftLayout->addWidget(m_infoBox, 1);

    m_actionBox = new QWidget(this);
    m_actionBox->setObjectName(QStringLiteral("playerActionBox"));
    m_actionBox->setFixedWidth(72);
    auto *actionLayout = new QHBoxLayout(m_actionBox);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(12);
    actionLayout->addStretch(1);
    actionLayout->addWidget(m_heartBtn, 0, Qt::AlignCenter);
    actionLayout->addWidget(m_downloadBtn, 0, Qt::AlignCenter);
    actionLayout->addStretch(1);

    m_progress = new ProgressSlider(this);
    m_progress->setObjectName(QStringLiteral("playerProgress"));
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
        btn->setIcon(icon.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)
                         ? makeThemedRasterIcon(icon)
                         : makeSvgIcon(icon, 20));
        btn->setIconSize(QSize(20, 20));
        btn->setFixedSize(30, 30);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
    };

    m_modeBtn = makeCtrlButton(QStringLiteral(":/icons/player-mode-loop.png"),
                               QStringLiteral("列表循环"));
    m_modeBtn->setObjectName(QStringLiteral("playerModeButton"));
    m_prevBtn = new DirectionalButton(QStringLiteral(":/icons/icon-prev.svg"), -1,
                                      QStringLiteral("上一首"), this);
    m_prevBtn->setObjectName(QStringLiteral("previousTrackButton"));
    m_playBtn = new AnimatedPlayPauseButton(this);
    m_nextBtn = new DirectionalButton(QStringLiteral(":/icons/icon-next.svg"), 1,
                                      QStringLiteral("下一首"), this);
    m_nextBtn->setObjectName(QStringLiteral("nextTrackButton"));

    m_muteBtn = makeCtrlButton(QStringLiteral(":/icons/icon-volume.svg"), QStringLiteral("静音"));
    m_muteBtn->setObjectName(QStringLiteral("playerMuteButton"));
    m_volume = new ProgressSlider(this);
    m_volume->setMinimumWidth(80);
    m_volume->setMaximumWidth(180);
    m_volume->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_volume->setRange(0, 100);
    m_volume->setValue(70, false);

    auto *controls = new QHBoxLayout;
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setSpacing(20);
    controls->addWidget(m_prevBtn);
    controls->addWidget(m_playBtn);
    controls->addWidget(m_nextBtn);

    m_centerBox = new QWidget(this);
    m_centerBox->setObjectName(QStringLiteral("playerCenterBox"));
    m_centerBox->setMinimumWidth(250);
    m_centerBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *centerLayout = new QVBoxLayout(m_centerBox);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(12);
    centerLayout->addStretch(1);
    centerLayout->addLayout(timeRow);
    centerLayout->addLayout(controls, 0);
    centerLayout->setAlignment(controls, Qt::AlignCenter);
    centerLayout->addStretch(1);

    m_lyricsBtn = makeCtrlButton(QStringLiteral(":/icons/player-lyrics.png"), QStringLiteral("歌词"));
    m_queueBtn = makeCtrlButton(QStringLiteral(":/icons/player-queue.png"), QStringLiteral("播放列表"));
    m_lyricsBtn->setObjectName(QStringLiteral("playerLyricsButton"));
    m_queueBtn->setObjectName(QStringLiteral("playerQueueButton"));

    m_rightBox = new QWidget(this);
    m_rightBox->setObjectName(QStringLiteral("playerRightBox"));
    m_rightBox->setMinimumWidth(240);
    m_rightBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *rightLayout = new QVBoxLayout(m_rightBox);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(20);
    auto *volumeRow = new QHBoxLayout;
    volumeRow->setContentsMargins(0, 0, 0, 0);
    volumeRow->setSpacing(8);
    volumeRow->addWidget(m_muteBtn);
    volumeRow->addWidget(m_volume);
    volumeRow->addStretch(1);
    auto *toolRow = new QHBoxLayout;
    toolRow->setContentsMargins(0, 0, 0, 0);
    toolRow->setSpacing(12);
    toolRow->addWidget(m_modeBtn);
    toolRow->addWidget(m_lyricsBtn);
    toolRow->addWidget(m_queueBtn);
    toolRow->addStretch(1);
    rightLayout->addStretch(1);
    rightLayout->addLayout(volumeRow);
    rightLayout->addLayout(toolRow);
    rightLayout->addStretch(1);

    m_rootLayout = new QHBoxLayout(this);
    m_rootLayout->setContentsMargins(28, 12, 28, 12);
    m_rootLayout->setSpacing(18);
    m_rootLayout->addWidget(m_leftBox);
    m_rootLayout->addWidget(m_actionBox);
    m_rootLayout->addWidget(m_centerBox, 45);
    m_rootLayout->addWidget(m_rightBox, 55);

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
        if (m_durationMs > 0) {
            const qint64 targetMs = qint64(v) * m_durationMs / 1000;
            m_positionMs = targetMs;
            updateTimeLabel();
            emit seekRequested(targetMs);
        }
    });
    connect(m_progress, &ProgressSlider::dragStarted, this, [this] {
        m_progressDragging = true;
    });
    connect(m_progress, &ProgressSlider::dragFinished, this, [this] {
        m_progressDragging = false;
    });
    updateResponsiveLayout();
}

void PlayerBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_rootLayout)
        return;

    updateResponsiveLayout();
}

void PlayerBar::updateResponsiveLayout()
{
    if (!m_rootLayout || !m_leftBox || !m_actionBox || !m_centerBox || !m_rightBox)
        return;

    const bool compact = width() < 780;
    const bool medium = width() < 1100;

    m_volume->setVisible(!compact);
    m_lyricsBtn->setVisible(!compact);
    m_queueBtn->setVisible(!compact);
    m_downloadBtn->show();
    m_heartBtn->show();
    m_cover->setVisible(!compact);
    m_infoBox->show();
    m_modeBtn->show();
    m_muteBtn->show();

    const int coverSize = medium ? 104 : 128;
    if (!compact && m_cover->size() != QSize(coverSize, coverSize)) {
        m_cover->setFixedSize(coverSize, coverSize);
        updateCoverPixmap();
    }

    if (compact) {
        m_leftBox->setMinimumWidth(140);
        m_leftBox->setMaximumWidth(210);
    } else if (medium) {
        m_leftBox->setMinimumWidth(220);
        m_leftBox->setMaximumWidth(300);
    } else {
        m_leftBox->setMinimumWidth(270);
        m_leftBox->setMaximumWidth(360);
    }
    m_centerBox->setMinimumWidth(250);
    m_rightBox->setMinimumWidth(76);

    const int horizontalMargin = compact ? 12 : medium ? 20 : 28;
    m_rootLayout->setContentsMargins(horizontalMargin, 12, horizontalMargin, 12);
    m_rootLayout->setSpacing(compact ? 10 : medium ? 14 : 18);
    m_rootLayout->invalidate();
}

void PlayerBar::setSong(const core::Song &song, bool favorite, bool animateDownloadState)
{
    const bool songChanged = m_song.selectionIdentity() != song.selectionIdentity();
    if (songChanged)
        m_downloadActive = false;
    m_song = song;
    m_favorite = favorite;
    const QString title = song.title.isEmpty() ? QFileInfo(song.filePath).completeBaseName() : song.title;
    const QString artist = song.artist.isEmpty() ? QStringLiteral("未知歌手") : song.artist;
    static_cast<ElidedLabel *>(m_title)->setFullText(title);
    static_cast<ElidedLabel *>(m_artist)->setFullText(artist);
    setToolTip(QString());
    m_cover->setToolTip(QString());
    static_cast<FavoriteButton *>(m_heartBtn)->setFavorite(favorite, false);
    updateDownloadButtonState(animateDownloadState);
    updateCoverPixmap();
    setDuration(song.durationMs);
    if (songChanged)
        setPosition(0);
}

void PlayerBar::setDownloadActive(bool active)
{
    if (m_downloadActive == active)
        return;
    m_downloadActive = active;
    updateDownloadButtonState(true);
}

void PlayerBar::setPlaybackError(const QString &message)
{
    const QString error = message.trimmed().isEmpty() ? QStringLiteral("播放失败") : message;
    setToolTip(error);
    m_title->setToolTip(error);
    m_cover->setToolTip(error);
    QToolTip::showText(m_title->mapToGlobal(QPoint(0, m_title->height())), error,
                       this, QRect(), 3500);
}

void PlayerBar::setPlaying(bool playing)
{
    m_playing = playing;
    if (playing) {
        setToolTip(QString());
        m_title->setToolTip(static_cast<ElidedLabel *>(m_title)->fullText());
        m_cover->setToolTip(QString());
    }
    updatePlayIcon();
}

void PlayerBar::setPosition(qint64 ms)
{
    m_positionMs = ms;
    updateTimeLabel();
    if (m_durationMs > 0 && !m_progressDragging)
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
    static const char *icons[] = { ":/icons/player-mode-loop.png",
                                  ":/icons/player-mode-single.png",
                                  ":/icons/player-mode-shuffle.png" };
    static const char *tips[] = { "列表循环", "单曲循环", "随机播放" };
    if (mode >= 0 && mode <= 2) {
        m_modeBtn->setIcon(makeThemedRasterIcon(QLatin1String(icons[mode])));
        m_modeBtn->setToolTip(QLatin1String(tips[mode]));
    }
}

void PlayerBar::updatePlayIcon()
{
    static_cast<AnimatedPlayPauseButton *>(m_playBtn)->setPlaying(m_playing);
    const QString accessibleText = m_playing ? QStringLiteral("暂停") : QStringLiteral("播放");
    m_playBtn->setToolTip(accessibleText);
    m_playBtn->setAccessibleName(accessibleText);
}

void PlayerBar::updateDownloadButtonState(bool animate)
{
    auto state = DownloadStateButton::Unavailable;
    if (m_song.isOnline()) {
        if (m_downloadActive)
            state = DownloadStateButton::Loading;
        else if (m_song.isDownloaded())
            state = DownloadStateButton::Delete;
        else
            state = DownloadStateButton::Download;
    }
    static_cast<DownloadStateButton *>(m_downloadBtn)->setState(state, animate);
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

void PlayerBar::updateCoverPixmap()
{
    const int size = qMax(1, m_cover->width());
    m_cover->setPixmap(CoverProvider::coverFor(m_song, size, 12));
}

} // namespace ui
