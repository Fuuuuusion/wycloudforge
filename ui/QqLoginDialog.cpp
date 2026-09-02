#include "QqLoginDialog.h"

#include "ui/ThemeManager.h"

#include "core/CredentialStore.h"
#include "core/QqMusicSource.h"
#include "core/SettingsService.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace ui {

QqLoginDialog::QqLoginDialog(core::QqMusicSource *source, QWidget *parent)
    : QDialog(parent)
    , m_source(source)
{
    setWindowTitle(QStringLiteral("QQ 音乐扫码登录"));
    setFixedSize(340, 500);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    auto *methodRow = new QHBoxLayout;
    auto *methodGroup = new QButtonGroup(this);
    methodGroup->setExclusive(true);
    m_qqButton = new QPushButton(QStringLiteral("QQ 扫码"), this);
    m_wechatButton = new QPushButton(QStringLiteral("微信扫码"), this);
    for (QPushButton *button : { m_qqButton, m_wechatButton }) {
        button->setCheckable(true);
        methodGroup->addButton(button);
        methodRow->addWidget(button);
    }
    m_qqButton->setChecked(true);
    layout->addLayout(methodRow);

    m_qrLabel = new QLabel(this);
    m_qrLabel->setFixedSize(240, 240);
    m_qrLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_qrLabel, 0, Qt::AlignHCenter);

    m_statusLabel = new QLabel(QStringLiteral("正在启动 QQ 音乐登录…"), this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    setThemedStyleSheet(m_statusLabel, QStringLiteral("color:@textSecondary;font-size:12px;"));
    layout->addWidget(m_statusLabel);

    m_refreshButton = new QPushButton(QStringLiteral("刷新二维码"), this);
    m_refreshButton->hide();
    layout->addWidget(m_refreshButton);

    auto *manualCookieButton = new QPushButton(QStringLiteral("高级：手动 Cookie 登录"), this);
    manualCookieButton->setToolTip(QStringLiteral("仅在扫码服务不可用时使用；凭据会经 Windows DPAPI 加密保存"));
    layout->addWidget(manualCookieButton);

    auto *closeButton = new QPushButton(QStringLiteral("关闭"), this);
    layout->addWidget(closeButton);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &QqLoginDialog::poll);
    connect(m_qqButton, &QPushButton::clicked, this, [this] { switchMethod(QStringLiteral("qq")); });
    connect(m_wechatButton, &QPushButton::clicked, this, [this] { switchMethod(QStringLiteral("wechat")); });
    connect(m_refreshButton, &QPushButton::clicked, this, &QqLoginDialog::startLogin);
    connect(manualCookieButton, &QPushButton::clicked, this, &QqLoginDialog::manualCookieLogin);
    connect(closeButton, &QPushButton::clicked, this, &QqLoginDialog::reject);
    startLogin();
}

QqLoginDialog::~QqLoginDialog()
{
    cancelAttempt();
}

void QqLoginDialog::reject()
{
    m_closing = true;
    ++m_generation;
    cancelAttempt();
    QDialog::reject();
}

void QqLoginDialog::switchMethod(const QString &method)
{
    if (m_method == method && !m_attemptId.isEmpty())
        return;
    ++m_generation;
    cancelAttempt();
    m_method = method;
    m_qqButton->setChecked(method == QStringLiteral("qq"));
    m_wechatButton->setChecked(method == QStringLiteral("wechat"));
    startLogin();
}

void QqLoginDialog::startLogin()
{
    if (!m_source || m_closing)
        return;
    ++m_generation;
    cancelAttempt();
    const quint64 generation = m_generation;
    m_qrLabel->clear();
    m_refreshButton->hide();
    m_pollInFlight = false;
    m_statusLabel->setText(QStringLiteral("正在获取%1二维码…")
                               .arg(m_method == QStringLiteral("qq") ? QStringLiteral("QQ")
                                                                      : QStringLiteral("微信")));
    const QPointer<QqLoginDialog> guard(this);
    m_source->startQrLogin(m_method, [guard, generation](const QJsonObject &data) {
        if (!guard || guard->m_closing || generation != guard->m_generation)
            return;
        const QString attemptId = data.value(QStringLiteral("loginAttemptId")).toString();
        if (attemptId.isEmpty() || !guard->showQrImage(data.value(QStringLiteral("qrImage")).toString())) {
            guard->m_statusLabel->setText(QStringLiteral("二维码生成失败，请刷新重试"));
            guard->m_refreshButton->show();
            return;
        }
        guard->m_attemptId = attemptId;
        guard->m_statusLabel->setText(guard->m_method == QStringLiteral("qq")
            ? QStringLiteral("请使用手机 QQ 扫码，并在手机上确认")
            : QStringLiteral("请使用微信扫码，并在手机上确认"));
        guard->scheduleNextPoll(200);
    }, [guard, generation](const QString &message) {
        if (!guard || generation != guard->m_generation)
            return;
        guard->m_statusLabel->setText(QStringLiteral("获取二维码失败：%1").arg(message));
        guard->m_refreshButton->show();
    });
}

void QqLoginDialog::poll()
{
    if (!m_source || m_attemptId.isEmpty() || m_pollInFlight || m_closing)
        return;
    m_pollInFlight = true;
    const quint64 generation = m_generation;
    const QString attemptId = m_attemptId;
    const QPointer<QqLoginDialog> guard(this);
    m_source->pollQrLogin(attemptId, [guard, generation](const QJsonObject &data) {
        if (!guard || generation != guard->m_generation)
            return;
        guard->m_pollInFlight = false;
        const QString state = data.value(QStringLiteral("state")).toString();
        if (state == QStringLiteral("WAITING_SCAN")) {
            guard->m_statusLabel->setText(QStringLiteral("等待扫码…"));
            guard->scheduleNextPoll(guard->m_method == QStringLiteral("qq") ? 1800 : 150);
            return;
        }
        if (state == QStringLiteral("WAITING_CONFIRM")) {
            // 二维码保持显示，不能复现扫码后提前消失的问题。
            guard->m_statusLabel->setText(QStringLiteral("已扫码，请在手机上确认授权"));
            guard->scheduleNextPoll(guard->m_method == QStringLiteral("qq") ? 1200 : 150);
            return;
        }
        if (state == QStringLiteral("EXPIRED") || state == QStringLiteral("REFUSED")) {
            guard->m_statusLabel->setText(state == QStringLiteral("EXPIRED")
                ? QStringLiteral("二维码已过期，请刷新后重试")
                : QStringLiteral("手机端已拒绝或取消登录，请刷新重试"));
            guard->m_refreshButton->show();
            return;
        }
        if (state != QStringLiteral("AUTHORIZED")) {
            guard->m_statusLabel->setText(QStringLiteral("登录状态异常，请稍后重试"));
            guard->scheduleNextPoll(1800);
            return;
        }

        const QString credential = data.value(QStringLiteral("credential")).toString();
        const QJsonObject profile = data.value(QStringLiteral("profile")).toObject();
        const QString userId = profile.value(QStringLiteral("userId")).toVariant().toString();
        if (credential.isEmpty() || userId.isEmpty()) {
            guard->m_statusLabel->setText(QStringLiteral("授权成功但账号资料验证失败，不会保存凭据"));
            guard->m_refreshButton->show();
            return;
        }
        guard->completeAuthorization(credential, profile);
    }, [guard, generation](const QString &message) {
        if (!guard || generation != guard->m_generation)
            return;
        guard->m_pollInFlight = false;
        // 网络错误不等同于过期或退出登录，保留二维码并继续当前任务。
        guard->m_statusLabel->setText(QStringLiteral("网络暂时不可用，保留二维码并重试：%1").arg(message));
        guard->scheduleNextPoll(2500);
    });
}

void QqLoginDialog::manualCookieLogin()
{
    if (!m_source || m_closing)
        return;
    bool ok = false;
    const QString credential = QInputDialog::getText(
        this, QStringLiteral("QQ 音乐手动 Cookie"),
        QStringLiteral("仅用于扫码故障回退。请输入完整 Cookie："),
        QLineEdit::Password, QString(), &ok).trimmed();
    if (!ok || credential.isEmpty())
        return;
    ++m_generation;
    cancelAttempt();
    const quint64 generation = m_generation;
    m_statusLabel->setText(QStringLiteral("正在远端验证手动凭据…"));
    const QPointer<QqLoginDialog> guard(this);
    m_source->validateCredential(credential, QStringLiteral("manual-cookie"),
        [guard, generation, credential](const QJsonObject &profile) {
            if (!guard || guard->m_closing || generation != guard->m_generation)
                return;
            guard->completeAuthorization(credential, profile);
        }, [guard, generation](const QString &message) {
            if (!guard || generation != guard->m_generation)
                return;
            guard->m_statusLabel->setText(QStringLiteral("手动凭据验证失败：%1").arg(message));
            guard->m_refreshButton->show();
        });
}

void QqLoginDialog::completeAuthorization(const QString &credential, const QJsonObject &profile)
{
    const QString userId = profile.value(QStringLiteral("userId")).toVariant().toString();
    if (credential.isEmpty() || userId.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("账号资料验证失败，不会保存凭据"));
        m_refreshButton->show();
        return;
    }
    QString credentialError;
    if (!core::CredentialStore::write(QStringLiteral("qqmusic"), credential, &credentialError)
        || core::CredentialStore::read(QStringLiteral("qqmusic")) != credential) {
        m_statusLabel->setText(QStringLiteral("无法安全保存登录凭据：%1").arg(credentialError));
        m_refreshButton->show();
        return;
    }
    m_source->setCookie(credential);
    core::SettingsService::setQqUserId(userId);
    core::SettingsService::setQqNickname(profile.value(QStringLiteral("nickname")).toString());
    core::SettingsService::setQqAvatarRemoteUrl(profile.value(QStringLiteral("avatarUrl")).toString());
    if (!m_attemptId.isEmpty())
        m_source->cancelQrLogin(m_attemptId);
    m_attemptId.clear();
    m_statusLabel->setText(QStringLiteral("登录成功：%1")
                               .arg(core::SettingsService::qqNickname()));
    accept();
}

void QqLoginDialog::cancelAttempt()
{
    if (m_timer)
        m_timer->stop();
    const QString attemptId = m_attemptId;
    m_attemptId.clear();
    m_pollInFlight = false;
    if (m_source && !attemptId.isEmpty())
        m_source->cancelQrLogin(attemptId);
}

void QqLoginDialog::scheduleNextPoll(int delayMs)
{
    if (!m_closing && !m_attemptId.isEmpty())
        m_timer->start(qMax(50, delayMs));
}

bool QqLoginDialog::showQrImage(const QString &dataUrl)
{
    QString base64 = dataUrl;
    const int comma = base64.indexOf(QLatin1Char(','));
    if (comma >= 0)
        base64 = base64.mid(comma + 1);
    QPixmap image;
    if (!image.loadFromData(QByteArray::fromBase64(base64.toUtf8())))
        return false;
    m_qrLabel->setPixmap(image.scaled(240, 240, Qt::KeepAspectRatio, Qt::FastTransformation));
    return true;
}

} // namespace ui
