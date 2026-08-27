#include "LoginDialog.h"

#include "core/SettingsService.h"

#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
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
    QPointer<LoginDialog> self(this);
    m_source->qrKey([self](const QString &key) {
        if (!self)
            return;
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
            }
        }, [self](const QString &msg) {
            if (self)
                self->m_statusLabel->setText(QStringLiteral("获取二维码失败:%1").arg(msg));
        });
    }, [self](const QString &msg) {
        if (self)
            self->m_statusLabel->setText(QStringLiteral("获取二维码失败:%1").arg(msg));
    });
}

void LoginDialog::poll()
{
    if (!m_source || m_key.isEmpty())
        return;
    QPointer<LoginDialog> self(this);
    m_source->qrCheck(m_key, [self](const QJsonObject &obj) {
        if (!self)
            return;
        const int code = obj.value(QStringLiteral("code")).toInt();
        if (code == 800) {
            self->m_statusLabel->setText(QStringLiteral("等待扫码…"));
        } else if (code == 801) {
            self->m_statusLabel->setText(QStringLiteral("已扫码,请在手机上确认"));
        } else if (code == 802 || code == 803) {
            self->m_timer->stop();
            const QString cookie = obj.value(QStringLiteral("cookie")).toString();
            const QJsonObject profile = obj.value(QStringLiteral("profile")).toObject();
            self->m_uid = profile.value(QStringLiteral("userId")).toVariant().toLongLong();
            self->m_nickname = profile.value(QStringLiteral("nickname")).toString();
            self->m_source->setCookie(cookie);
            core::SettingsService::setOnlineCookie(cookie);
            core::SettingsService::setOnlineUid(self->m_uid);
            core::SettingsService::setOnlineNickname(self->m_nickname);
            self->m_statusLabel->setText(QStringLiteral("登录成功:%1").arg(self->m_nickname));
            self->accept();
        }
    }, [self](const QString &msg) {
        if (self)
            self->m_statusLabel->setText(QStringLiteral("登录状态查询失败:%1").arg(msg));
    });
}

} // namespace ui
