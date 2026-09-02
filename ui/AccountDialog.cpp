#include "AccountDialog.h"

#include "core/MusicSource.h"
#include "core/CredentialStore.h"
#include "core/QqApiService.h"
#include "core/QqMusicSource.h"
#include "core/SettingsService.h"
#include "ui/LoginDialog.h"
#include "ui/QqLoginDialog.h"

#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {
namespace {
QPixmap accountAvatar(const QString &path, const QString &nickname, int size)
{
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPixmap src = !path.isEmpty() ? QPixmap(path) : QPixmap();
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addEllipse(0, 0, size, size);
    p.setClipPath(clip);
    if (src.isNull()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xEC, 0x41, 0x41));
        p.drawEllipse(0, 0, size, size);
        p.setPen(Qt::white);
        QFont f(QStringLiteral("Microsoft YaHei UI"), qMax(9, size / 4), QFont::Bold);
        p.setFont(f);
        p.drawText(out.rect(), Qt::AlignCenter, nickname.left(1));
    } else {
        p.drawPixmap(0, 0, src.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }
    return out;
}

QLabel *avatarLabel(const QString &path, const QString &nickname, int size, QWidget *parent)
{
    auto *l = new QLabel(parent);
    l->setFixedSize(size, size);
    l->setPixmap(accountAvatar(path, nickname, size));
    return l;
}
}

AccountDialog::AccountDialog(core::MusicSource *netease, core::QqMusicSource *qq,
                             core::QqApiService *qqService, QWidget *parent)
    : QDialog(parent)
    , m_netease(netease)
    , m_qq(qq)
    , m_qqService(qqService)
{
    setWindowTitle(QStringLiteral("账号中心"));
    setObjectName("accountDialog");
    setFixedWidth(380);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("账号中心"), this);
    title->setProperty("class", "pageTitle");
    layout->addWidget(title);

    // 网易云
    const bool ne = core::SettingsService::onlineUid() > 0;
    auto *neRow = new QWidget(this);
    auto *neL = new QHBoxLayout(neRow);
    neL->setContentsMargins(0, 0, 0, 0);
    neL->setSpacing(10);
    neL->addWidget(avatarLabel(core::SettingsService::onlineAvatarUrl(),
                               core::SettingsService::onlineNickname(), 40, neRow));
    auto *neInfo = new QVBoxLayout;
    auto *neName = new QLabel(ne ? core::SettingsService::onlineNickname() : QStringLiteral("未登录"), neRow);
    neName->setProperty("class", "accountName");
    auto *neStatus = new QLabel(ne ? QStringLiteral("已登录 · 网易云") : QStringLiteral("网易云 · 扫码登录"), neRow);
    neStatus->setProperty("class", "accountSub");
    neInfo->addWidget(neName);
    neInfo->addWidget(neStatus);
    neL->addLayout(neInfo, 1);
    auto *neBtn = new QPushButton(ne ? QStringLiteral("退出登录") : QStringLiteral("登录"), neRow);
    connect(neBtn, &QPushButton::clicked, this, ne ? &AccountDialog::logoutNetease : &AccountDialog::loginNetease);
    neL->addWidget(neBtn);
    layout->addWidget(neRow);

    // QQ音乐：QQ 与微信扫码共用同一账号槽位。
    const bool qqLoggedIn = !core::SettingsService::qqUserId().isEmpty();
    auto *qqRow = new QWidget(this);
    auto *qqL = new QHBoxLayout(qqRow);
    qqL->setContentsMargins(0, 0, 0, 0);
    qqL->setSpacing(10);
    qqL->addWidget(avatarLabel(core::SettingsService::qqAvatarUrl(),
                               core::SettingsService::qqNickname(), 40, qqRow));
    auto *qqInfo = new QVBoxLayout;
    auto *qqName = new QLabel(qqLoggedIn ? core::SettingsService::qqNickname()
                                         : QStringLiteral("未登录"), qqRow);
    qqName->setProperty("class", "accountName");
    QString qqStatusText = QStringLiteral("QQ音乐 · QQ/微信扫码");
    if (qqLoggedIn) {
        const int vip = core::SettingsService::qqVipStatus();
        qqStatusText = vip > 0 ? QStringLiteral("已登录 · VIP 已识别")
                     : vip == 0 ? QStringLiteral("已登录 · 普通账号")
                                : QStringLiteral("已登录 · VIP 状态待验证");
    }
    auto *qqStatus = new QLabel(qqStatusText, qqRow);
    qqStatus->setProperty("class", "accountSub");
    qqInfo->addWidget(qqName);
    qqInfo->addWidget(qqStatus);
    qqL->addLayout(qqInfo, 1);
    auto *qqBtn = new QPushButton(qqLoggedIn ? QStringLiteral("退出登录")
                                             : QStringLiteral("登录"), qqRow);
    connect(qqBtn, &QPushButton::clicked, this,
            qqLoggedIn ? &AccountDialog::logoutQq : &AccountDialog::loginQq);
    qqL->addWidget(qqBtn);
    layout->addWidget(qqRow);

    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;border-radius:14px;background:#1B1B24;color:#E8E8E8;padding:7px 22px;}"
        "QPushButton:hover{background:#2A2A36;}"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(closeBtn, 0, Qt::AlignHCenter);
}

void AccountDialog::loginNetease()
{
    if (!m_netease)
        return;
    ui::LoginDialog dlg(m_netease, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    core::SettingsService::setAccountDisplaySource(0);
    if (core::SettingsService::avatarSource() != 2)
        core::SettingsService::setAvatarSource(0);
    // 二维码对话框只有在 803 授权完成且 /login/status 返回有效账号后才会接受。
    emit accountStateChanged();
    accept();
}

void AccountDialog::logoutNetease()
{
    if (m_netease)
        m_netease->logout([](const QJsonObject &) {}, [](const QString &) {});
    core::SettingsService::setOnlineCookie(QString());
    core::SettingsService::setOnlineUid(0);
    core::SettingsService::setOnlineNickname(QString());
    core::SettingsService::setOnlineAvatarUrl(QString());
    if (m_netease)
        m_netease->setCookie(QString());
    if (core::SettingsService::accountDisplaySource() == 0)
        core::SettingsService::setAccountDisplaySource(
            core::SettingsService::qqUserId().isEmpty() ? -1 : 1);
    emit accountStateChanged();
    accept();
}

void AccountDialog::loginQq()
{
    if (!m_qq || !m_qqService)
        return;
    setEnabled(false);
    m_qqService->ensureRunning([this] {
        setEnabled(true);
        QqLoginDialog dialog(m_qq, this);
        if (dialog.exec() == QDialog::Accepted) {
            core::SettingsService::setAccountDisplaySource(1);
            if (core::SettingsService::avatarSource() != 2)
                core::SettingsService::setAvatarSource(1);
            emit accountStateChanged();
            accept();
        }
    }, [this](const QString &message) {
        setEnabled(true);
        QMessageBox::information(this, QStringLiteral("QQ 音乐服务"), message);
    });
}

void AccountDialog::logoutQq()
{
    if (m_qq)
        m_qq->logout([](const QJsonObject &) {}, [](const QString &) {});
    core::CredentialStore::remove(QStringLiteral("qqmusic"));
    core::SettingsService::setQqUserId(QString());
    core::SettingsService::setQqNickname(QString());
    core::SettingsService::setQqAvatarUrl(QString());
    core::SettingsService::setQqAvatarRemoteUrl(QString());
    core::SettingsService::setQqVipStatus(-1);
    if (m_qq)
        m_qq->setCookie(QString());
    if (core::SettingsService::accountDisplaySource() == 1)
        core::SettingsService::setAccountDisplaySource(
            core::SettingsService::onlineUid() > 0 ? 0 : -1);
    emit accountStateChanged();
    accept();
}

} // namespace ui
