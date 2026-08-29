#include "AccountPanel.h"

#include "core/SettingsService.h"
#include "ui/SvgIcon.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>

namespace ui {
namespace {
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

    auto *settingsBtn = new QPushButton(this);
    settingsBtn->setObjectName("accountSettings");
    settingsBtn->setIcon(makeSvgIcon(QStringLiteral(":/icons/icon-settings.svg"), 18));
    settingsBtn->setIconSize(QSize(18, 18));
    settingsBtn->setFixedSize(28, 28);
    settingsBtn->setCursor(Qt::PointingHandCursor);
    settingsBtn->setToolTip(QStringLiteral("设置"));

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
