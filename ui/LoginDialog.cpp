#include "LoginDialog.h"

#include "core/SettingsService.h"

#include <QDir>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QJsonObject>
#include <QImage>
#include <QPixmap>

namespace ui {

LoginDialog::LoginDialog(core::MusicSource *source, QWidget *parent)
    : QDialog(parent)
    , m_source(source)
{
    setWindowTitle(QStringLiteral("扫码登录 · %1").arg(source ? source->sourceName() : QStringLiteral("在线音乐")));
    setFixedSize(320, 400);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    m_qrLabel = new QLabel(this);
    m_qrLabel->setAlignment(Qt::AlignCenter);
    m_qrLabel->setFixedSize(240, 240);
    layout->addWidget(m_qrLabel, 0, Qt::AlignHCenter);

    m_statusLabel = new QLabel(QStringLiteral("正在获取二维码…"), this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#9A9AA5;font-size:12px;"));
    layout->addWidget(m_statusLabel);

    m_refreshButton = new QPushButton(QStringLiteral("刷新二维码"), this);
    m_refreshButton->hide();
    connect(m_refreshButton, &QPushButton::clicked, this, &LoginDialog::startLogin);
    layout->addWidget(m_refreshButton);

    auto *cancel = new QPushButton(QStringLiteral("关闭"), this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    layout->addWidget(cancel);

    m_timer = new QTimer(this);
    m_timer->setInterval(2000);
    connect(m_timer, &QTimer::timeout, this, &LoginDialog::poll);
    startLogin();
}

void LoginDialog::startLogin()
{
    if (!m_source)
        return;
    m_timer->stop();
    m_pollInFlight = false;
    m_key.clear();
    m_uid = 0;
    m_nickname.clear();
    m_qrLabel->clear();
    m_refreshButton->hide();
    m_statusLabel->setText(QStringLiteral("正在获取二维码…"));
    QPointer<LoginDialog> self(this);
    m_source->qrKey([self](const QString &key) {
        if (!self)
            return;
        if (key.isEmpty()) {
            self->m_statusLabel->setText(QStringLiteral("二维码密钥为空，请重试"));
            self->m_refreshButton->show();
            return;
        }
        self->m_key = key;
        self->m_source->qrCreate(key, [self](const QString &qrimg) {
            if (!self)
                return;
            QString b64 = qrimg;
            const int comma = b64.indexOf(QLatin1Char(','));
            if (comma >= 0)
                b64 = b64.mid(comma + 1);
            const QByteArray data = QByteArray::fromBase64(b64.toUtf8());
            QPixmap pm;
            if (pm.loadFromData(data, "PNG")) {
                self->m_qrLabel->setPixmap(pm.scaled(240, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                self->m_statusLabel->setText(QStringLiteral("请用%1 App 扫码并确认").arg(self->m_source->sourceName()));
                self->m_timer->start();
            } else {
                self->m_statusLabel->setText(QStringLiteral("二维码生成失败"));
                self->m_refreshButton->show();
            }
        }, [self](const QString &msg) {
            if (self) {
                self->m_statusLabel->setText(QStringLiteral("获取二维码失败:%1").arg(msg));
                self->m_refreshButton->show();
            }
        });
    }, [self](const QString &msg) {
        if (self) {
            self->m_statusLabel->setText(QStringLiteral("获取二维码失败:%1").arg(msg));
            self->m_refreshButton->show();
        }
    });
}

void LoginDialog::poll()
{
    if (!m_source || m_key.isEmpty() || m_pollInFlight)
        return;
    m_pollInFlight = true;
    QPointer<LoginDialog> self(this);
    m_source->qrCheck(m_key, [self](const QJsonObject &obj) {
        if (!self)
            return;
        self->m_pollInFlight = false;
        const int code = obj.value(QStringLiteral("code")).toInt();
        if (code == 800) {
            self->m_timer->stop();
            self->m_statusLabel->setText(QStringLiteral("二维码已过期，请刷新后重新扫码"));
            self->m_refreshButton->show();
        } else if (code == 801) {
            self->m_statusLabel->setText(QStringLiteral("等待扫码…"));
        } else if (code == 802) {
            self->m_statusLabel->setText(QStringLiteral("已扫码,请在手机上确认"));
        } else if (code == 803) {
            self->m_timer->stop();
            const QString cookie = obj.value(QStringLiteral("cookie")).toString();
            if (cookie.isEmpty()) {
                self->m_statusLabel->setText(QStringLiteral("授权成功但未收到登录凭据，请刷新重试"));
                self->m_refreshButton->show();
                return;
            }
            self->m_source->setCookie(cookie);
            core::SettingsService::setOnlineCookie(cookie);
            if (core::SettingsService::onlineCookie() != cookie) {
                self->m_source->setCookie(QString());
                self->m_statusLabel->setText(
                    QStringLiteral("无法用 Windows DPAPI 安全保存登录凭据，请检查用户配置文件"));
                self->m_refreshButton->show();
                return;
            }
            self->m_statusLabel->setText(QStringLiteral("授权成功，正在同步账号信息…"));
            // 网易云偶尔会在 803 后延迟数秒才让 /login/status 读到新会话。
            // 给服务端足够的同步时间，避免用户已经确认却被立即判为登录失败。
            self->completeLogin(10);
        } else {
            const QString message = obj.value(QStringLiteral("message")).toString();
            self->m_statusLabel->setText(message.isEmpty()
                                             ? QStringLiteral("登录状态异常:%1").arg(code)
                                             : message);
        }
    }, [self](const QString &msg) {
        if (self) {
            self->m_pollInFlight = false;
            self->m_statusLabel->setText(QStringLiteral("登录状态查询失败:%1").arg(msg));
        }
    });
}

void LoginDialog::completeLogin(int attemptsLeft)
{
    if (!m_source)
        return;
    QPointer<LoginDialog> self(this);
    m_source->loginStatus([self, attemptsLeft](const QJsonObject &obj) {
        if (!self)
            return;
        const QJsonObject data = obj.value(QStringLiteral("data")).toObject();
        QJsonObject profile = data.value(QStringLiteral("profile")).toObject();
        if (profile.isEmpty())
            profile = obj.value(QStringLiteral("profile")).toObject();
        const QJsonObject account = data.value(QStringLiteral("account")).toObject();
        qint64 uid = profile.value(QStringLiteral("userId")).toVariant().toLongLong();
        if (uid <= 0)
            uid = account.value(QStringLiteral("id")).toVariant().toLongLong();
        if (uid <= 0) {
            if (attemptsLeft > 1) {
                self->m_statusLabel->setText(QStringLiteral("账号信息尚未同步，正在重试…"));
                QTimer::singleShot(1000, self, [self, attemptsLeft] {
                    if (self)
                        self->completeLogin(attemptsLeft - 1);
                });
            } else {
                self->m_source->setCookie(QString());
                core::SettingsService::setOnlineCookie(QString());
                self->m_statusLabel->setText(QStringLiteral("账号信息同步失败，请刷新二维码重试"));
                self->m_refreshButton->show();
            }
            return;
        }

        self->m_uid = uid;
        self->m_nickname = profile.value(QStringLiteral("nickname")).toString();
        if (self->m_nickname.isEmpty())
            self->m_nickname = QStringLiteral("网易云用户");
        core::SettingsService::setOnlineUid(self->m_uid);
        core::SettingsService::setOnlineNickname(self->m_nickname);

        const QString avatar = profile.value(QStringLiteral("avatarUrl")).toString();
        if (avatar.isEmpty()) {
            self->m_statusLabel->setText(QStringLiteral("登录成功:%1").arg(self->m_nickname));
            self->accept();
            return;
        }
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/netease_avatar.png");
        // 头像属于非关键数据，不应让登录窗口再等待一次网络下载。
        self->m_source->downloadToFile(QUrl(avatar), path, [path](bool ok) {
            if (ok)
                core::SettingsService::setOnlineAvatarUrl(path);
        });
        self->m_statusLabel->setText(QStringLiteral("登录成功:%1").arg(self->m_nickname));
        self->accept();
    }, [self, attemptsLeft](const QString &message) {
        if (!self)
            return;
        if (attemptsLeft > 1) {
            QTimer::singleShot(1000, self, [self, attemptsLeft] {
                if (self)
                    self->completeLogin(attemptsLeft - 1);
            });
        } else {
            self->m_source->setCookie(QString());
            core::SettingsService::setOnlineCookie(QString());
            self->m_statusLabel->setText(QStringLiteral("账号信息同步失败:%1").arg(message));
            self->m_refreshButton->show();
        }
    });
}

} // namespace ui
