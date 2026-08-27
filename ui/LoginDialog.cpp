#include "LoginDialog.h"

#include "core/SettingsService.h"

#include <QLabel>
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
    m_source->qrKey([this](const QString &key) {
        m_key = key;
        m_source->qrCreate(key, [this](const QString &qrimg) {
            const QByteArray data = QByteArray::fromBase64(qrimg.toUtf8());
            QPixmap pm;
            if (pm.loadFromData(data, "PNG")) {
                m_qrLabel->setPixmap(pm.scaled(240, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                m_statusLabel->setText(QStringLiteral("请用%1 App 扫码并确认").arg(m_source->sourceName()));
                m_timer->start();
            } else {
                m_statusLabel->setText(QStringLiteral("二维码生成失败"));
            }
        }, [this](const QString &msg) {
            m_statusLabel->setText(QStringLiteral("获取二维码失败:%1").arg(msg));
        });
    }, [this](const QString &msg) {
        m_statusLabel->setText(QStringLiteral("获取二维码失败:%1").arg(msg));
    });
}

void LoginDialog::poll()
{
    if (!m_source || m_key.isEmpty())
        return;
    m_source->qrCheck(m_key, [this](const QJsonObject &obj) {
        const int code = obj.value(QStringLiteral("code")).toInt();
        if (code == 800) {
            m_statusLabel->setText(QStringLiteral("等待扫码…"));
        } else if (code == 801) {
            m_statusLabel->setText(QStringLiteral("已扫码,请在手机上确认"));
        } else if (code == 802) {
            m_timer->stop();
            const QString cookie = obj.value(QStringLiteral("cookie")).toString();
            const QJsonObject profile = obj.value(QStringLiteral("profile")).toObject();
            m_uid = profile.value(QStringLiteral("userId")).toVariant().toLongLong();
            m_nickname = profile.value(QStringLiteral("nickname")).toString();
            m_source->setCookie(cookie);
            core::SettingsService::setOnlineCookie(cookie);
            core::SettingsService::setOnlineUid(m_uid);
            core::SettingsService::setOnlineNickname(m_nickname);
            m_statusLabel->setText(QStringLiteral("登录成功:%1").arg(m_nickname));
            accept();
        }
    }, [this](const QString &msg) {
        m_statusLabel->setText(QStringLiteral("登录状态查询失败:%1").arg(msg));
    });
}

} // namespace ui
