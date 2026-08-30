#include "AccountPanel.h"

#include "core/SettingsService.h"

#include <QEasingCurve>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QVariantAnimation>

namespace ui {
namespace {
QColor blendColor(const QColor &from, const QColor &to, qreal progress)
{
    const qreal t = qBound(0.0, progress, 1.0);
    return QColor(qRound(from.red() + (to.red() - from.red()) * t),
                  qRound(from.green() + (to.green() - from.green()) * t),
                  qRound(from.blue() + (to.blue() - from.blue()) * t),
                  qRound(from.alpha() + (to.alpha() - from.alpha()) * t));
}

class SettingsButton final : public QPushButton
{
public:
    explicit SettingsButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setObjectName("accountSettings");
        setFixedSize(28, 28);
        setFlat(true);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("设置"));
        setAccessibleName(QStringLiteral("设置"));
        setAttribute(Qt::WA_Hover, true);
        setFocusPolicy(Qt::StrongFocus);

        m_animation.setEasingCurve(QEasingCurve::InOutCubic);
        connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_progress = value.toReal();
            update();
        });
    }

protected:
    void enterEvent(QEnterEvent *event) override
    {
        animateTo(1.0);
        QPushButton::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        animateTo(0.0);
        QPushButton::leaveEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        QColor background = blendColor(QColor(0x1B, 0x1B, 0x24), QColor(0x2A, 0x2A, 0x36), m_progress);
        QColor track = blendColor(QColor(0xB8, 0xB8, 0xC4), QColor(0xE8, 0xE8, 0xE8), m_progress);
        QColor accent = blendColor(QColor(0xEC, 0x41, 0x41), QColor(0xF0, 0x4A, 0x4A), m_progress);
        if (!isEnabled()) {
            background = QColor(0x16, 0x16, 0x1E);
            track = QColor(0x6E, 0x6E, 0x7A);
            accent = QColor(0x6E, 0x6E, 0x7A);
        } else if (isDown()) {
            background = QColor(0x2A, 0x2A, 0x36);
            accent = QColor(0xD6, 0x38, 0x38);
        }

        const QRectF buttonRect = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(buttonRect, 6.0, 6.0);

        if (hasFocus() && isEnabled()) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(accent, 2.0));
            painter.drawRoundedRect(buttonRect.adjusted(1.0, 1.0, -1.0, -1.0), 5.0, 5.0);
        }

        constexpr qreal left = 6.0;
        constexpr qreal right = 22.0;
        constexpr qreal ys[] = { 8.0, 14.0, 20.0 };
        constexpr qreal idlePositions[] = { 0.28, 0.72, 0.28 };
        constexpr qreal hoverPositions[] = { 0.72, 0.28, 0.72 };

        painter.setPen(QPen(track, 2.0, Qt::SolidLine, Qt::RoundCap));
        for (qreal y : ys)
            painter.drawLine(QPointF(left, y), QPointF(right, y));

        painter.setPen(Qt::NoPen);
        for (int i = 0; i < 3; ++i) {
            const qreal position = idlePositions[i]
                + (hoverPositions[i] - idlePositions[i]) * m_progress;
            const QPointF center(left + (right - left) * position, ys[i]);
            painter.setBrush(track);
            painter.drawEllipse(center, 3.0, 3.0);
            painter.setBrush(accent);
            painter.drawEllipse(center, 1.7, 1.7);
        }
    }

private:
    void animateTo(qreal target)
    {
        const qreal distance = qAbs(target - m_progress);
        if (distance < 0.001)
            return;
        m_animation.stop();
        m_animation.setStartValue(m_progress);
        m_animation.setEndValue(target);
        m_animation.setDuration(qMax(1, qRound(140.0 * distance)));
        m_animation.start();
    }

    QVariantAnimation m_animation;
    qreal m_progress = 0.0;
};

class ClickableLabel : public QLabel
{
    Q_OBJECT
public:
    using QLabel::QLabel;
signals:
    void clicked();
protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            emit clicked();
        QLabel::mouseReleaseEvent(event);
    }
};

QPixmap roundAvatar(const QPixmap &src, int size)
{
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addEllipse(0, 0, size, size);
    p.setClipPath(clip);
    p.drawPixmap(0, 0, src.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    return out;
}

QPixmap letterAvatar(const QString &letter, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xEC, 0x41, 0x41));
    p.drawEllipse(0, 0, size, size);
    p.setPen(Qt::white);
    QFont f(QStringLiteral("Microsoft YaHei UI"), qMax(9, size / 4), QFont::Bold);
    p.setFont(f);
    p.drawText(pm.rect(), Qt::AlignCenter, letter);
    return pm;
}
}

AccountPanel::AccountPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("accountPanel");

    m_avatar = new ClickableLabel(this);
    m_avatar->setFixedSize(36, 36);
    m_avatar->setCursor(Qt::PointingHandCursor);

    m_name = new QLabel(QStringLiteral("未登录"), this);
    m_name->setProperty("class", "accountName");

    auto *settingsBtn = new SettingsButton(this);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 8, 10, 8);
    layout->setSpacing(8);
    layout->addWidget(m_avatar);
    layout->addWidget(m_name, 1);
    layout->addWidget(settingsBtn);

    connect(qobject_cast<ClickableLabel *>(m_avatar), &ClickableLabel::clicked, this, &AccountPanel::accountClicked);
    connect(settingsBtn, &QPushButton::clicked, this, &AccountPanel::settingsClicked);
    refresh();
}

void AccountPanel::refresh()
{
    const int src = core::SettingsService::avatarSource();
    QString nickname;
    QString avatarPath;
    if (src == 0 && core::SettingsService::onlineUid() > 0) {
        nickname = core::SettingsService::onlineNickname();
        avatarPath = core::SettingsService::onlineAvatarUrl();
    } else if (src == 1 && !core::SettingsService::qqUserId().isEmpty()) {
        nickname = core::SettingsService::qqNickname();
        avatarPath = core::SettingsService::qqAvatarUrl();
    } else if (src == 2) {
        avatarPath = core::SettingsService::avatarUploadPath();
    }

    if (nickname.isEmpty())
        nickname = QStringLiteral("未登录");
    m_name->setText(nickname);

    QPixmap pm = !avatarPath.isEmpty() ? QPixmap(avatarPath) : QPixmap();
    if (pm.isNull())
        pm = letterAvatar(nickname.left(1), 36);
    m_avatar->setPixmap(roundAvatar(pm, 36));
    m_avatar->setToolTip(QStringLiteral("账号中心"));
}

} // namespace ui

#include "AccountPanel.moc"
