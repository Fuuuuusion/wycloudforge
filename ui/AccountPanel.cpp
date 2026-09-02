#include "AccountPanel.h"

#include "core/SettingsService.h"

#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {
namespace {
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
    setFixedHeight(224);

    m_avatar = new QPushButton(this);
    m_avatar->setObjectName(QStringLiteral("accountAvatarButton"));
    m_avatar->setFixedSize(120, 120);
    m_avatar->setIconSize(QSize(120, 120));
    m_avatar->setFlat(true);
    m_avatar->setCursor(Qt::PointingHandCursor);
    m_avatar->setStyleSheet(QStringLiteral(
        "QPushButton{background:transparent;border:none;padding:0;}"));
    m_avatar->setAccessibleName(QStringLiteral("打开账号中心"));

    m_accountButton = new QPushButton(QStringLiteral("登录"), this);
    m_accountButton->setObjectName(QStringLiteral("accountActionButton"));
    m_accountButton->setProperty("class", "accountAction");
    m_accountButton->setCursor(Qt::PointingHandCursor);
    m_accountButton->setAccessibleName(QStringLiteral("登录"));
    m_accountButton->setToolTip(QStringLiteral("登录网易云或 QQ音乐"));
    m_accountButton->setMinimumWidth(0);
    m_accountButton->setFixedHeight(38);
    m_accountButton->setStyleSheet(QStringLiteral(
        "QPushButton{background:transparent;border:none;color:#E8E8E8;font-size:14px;"
        "padding:4px;text-align:center;}"
        "QPushButton:hover{color:#EC4141;}"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    layout->addWidget(m_avatar, 0, Qt::AlignHCenter);
    layout->addWidget(m_accountButton);
    layout->addStretch(1);

    connect(m_avatar, &QPushButton::clicked, this, &AccountPanel::accountClicked);
    connect(m_accountButton, &QPushButton::clicked, this, &AccountPanel::accountClicked);
    refresh();
}

void AccountPanel::refresh()
{
    const bool neteaseLoggedIn = core::SettingsService::onlineUid() > 0;
    const bool qqLoggedIn = !core::SettingsService::qqUserId().isEmpty();
    int accountSource = core::SettingsService::accountDisplaySource();
    if ((accountSource == 0 && !neteaseLoggedIn)
        || (accountSource == 1 && !qqLoggedIn)
        || (accountSource != 0 && accountSource != 1)) {
        accountSource = qqLoggedIn ? 1 : (neteaseLoggedIn ? 0 : -1);
        core::SettingsService::setAccountDisplaySource(accountSource);
    }

    QString nickname;
    QString avatarPath;
    if (accountSource == 0) {
        nickname = core::SettingsService::onlineNickname();
        avatarPath = core::SettingsService::onlineAvatarUrl();
    } else if (accountSource == 1) {
        nickname = core::SettingsService::qqNickname();
        avatarPath = core::SettingsService::qqAvatarUrl();
    }
    if (core::SettingsService::avatarSource() == 2
        && !core::SettingsService::avatarUploadPath().isEmpty()) {
        avatarPath = core::SettingsService::avatarUploadPath();
    }

    if (nickname.isEmpty())
        nickname = QStringLiteral("未登录");
    const bool loggedIn = neteaseLoggedIn || qqLoggedIn;
    const QString accountText = loggedIn ? nickname : QStringLiteral("登录");
    m_accountButton->setText(accountText);
    m_accountButton->setAccessibleName(loggedIn ? QStringLiteral("查看账号")
                                                : QStringLiteral("登录"));
    m_accountButton->setToolTip(loggedIn ? QStringLiteral("查看账号")
                                         : QStringLiteral("登录网易云或 QQ音乐"));

    QPixmap pm = !avatarPath.isEmpty() ? QPixmap(avatarPath) : QPixmap();
    if (pm.isNull())
        pm = letterAvatar(nickname.left(1), 120);
    m_avatar->setIcon(QIcon(roundAvatar(pm, 120)));
}

} // namespace ui
