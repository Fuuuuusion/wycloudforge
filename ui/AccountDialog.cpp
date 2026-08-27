#include "AccountDialog.h"

#include "core/MusicSource.h"
#include "core/SettingsService.h"
#include "ui/LoginDialog.h"

#include <QDir>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
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
        QLinearGradient g(0, 0, size, size);
        g.setColorAt(0, QColor(0xEC, 0x41, 0x41));
        g.setColorAt(1, QColor(0xFF, 0x9A, 0x76));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
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

AccountDialog::AccountDialog(core::MusicSource *netease, QWidget *parent)
    : QDialog(parent)
    , m_netease(netease)
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

    // QQ音乐(预留)
    auto *qqRow = new QWidget(this);
    auto *qqL = new QHBoxLayout(qqRow);
    qqL->setContentsMargins(0, 0, 0, 0);
    qqL->setSpacing(10);
    qqL->addWidget(avatarLabel(QString(), QStringLiteral("Q"), 40, qqRow));
    auto *qqInfo = new QVBoxLayout;
    auto *qqName = new QLabel(QStringLiteral("未接入"), qqRow);
    qqName->setProperty("class", "accountName");
    auto *qqStatus = new QLabel(QStringLiteral("QQ音乐 · 后续接入"), qqRow);
    qqStatus->setProperty("class", "accountSub");
    qqInfo->addWidget(qqName);
    qqInfo->addWidget(qqStatus);
    qqL->addLayout(qqInfo, 1);
    auto *qqBtn = new QPushButton(QStringLiteral("未接入"), qqRow);
    qqBtn->setEnabled(false);
    qqL->addWidget(qqBtn);
    layout->addWidget(qqRow);

    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;border-radius:14px;background:rgba(255,255,255,0.10);color:#E8E8E8;padding:7px 22px;}"
        "QPushButton:hover{background:rgba(255,255,255,0.16);}"));
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
    // LoginDialog 已写入 cookie;/login/qr/check 往往不带 profile,故从 /login/status 补齐 uid/昵称/头像
    QPointer<AccountDialog> self(this);
    m_netease->loginStatus([self](const QJsonObject &obj) {
        if (!self)
            return;
        const QJsonObject data = obj.value(QStringLiteral("data")).toObject();
        const QJsonObject profile = data.value(QStringLiteral("profile")).toObject();
        const qint64 userId = profile.value(QStringLiteral("userId")).toVariant().toLongLong();
        const QString nickname = profile.value(QStringLiteral("nickname")).toString();
        const QString avatar = profile.value(QStringLiteral("avatarUrl")).toString();
        if (userId > 0) {
            core::SettingsService::setOnlineUid(userId);
            core::SettingsService::setOnlineNickname(nickname);
        }
        auto finish = [self] {
            if (self) {
                emit self->accountStateChanged();
                self->accept();
            }
        };
        if (!avatar.isEmpty() && self->m_netease) {
            const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            QDir().mkpath(dir);
            const QString path = dir + QStringLiteral("/netease_avatar.png");
            self->m_netease->downloadToFile(QUrl(avatar), path, [self, path, finish](bool ok) {
                if (ok)
                    core::SettingsService::setOnlineAvatarUrl(path);
                finish();
            });
        } else {
            finish();
        }
    }, [self](const QString &) { if (self) self->accept(); });
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
    emit accountStateChanged();
    accept();
}

} // namespace ui
