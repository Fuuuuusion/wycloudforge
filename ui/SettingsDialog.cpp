#include "SettingsDialog.h"

#include "core/SettingsService.h"

#include "core/ApiService.h"
#include "core/LibraryService.h"
#include "core/NeteaseApiClient.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

namespace ui {

SettingsDialog::SettingsDialog(core::ApiService *apiService, core::NeteaseApiClient *apiClient,
                               core::LibraryService *library, QWidget *parent)
    : QDialog(parent)
    , m_apiService(apiService)
    , m_apiClient(apiClient)
    , m_library(library)
{
    setWindowTitle(QStringLiteral("设置"));
    setMinimumWidth(480);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 14);
    layout->setSpacing(14);

    auto *folderTitle = new QLabel(QStringLiteral("音乐文件夹"), this);
    folderTitle->setStyleSheet(QStringLiteral("font-weight:600;font-size:14px;"));
    layout->addWidget(folderTitle);

    m_folderList = new QListWidget(this);
    m_folderList->setStyleSheet(QStringLiteral(
        "QListWidget{background:rgba(255,255,255,0.05);border:none;border-radius:6px;}"
        "QListWidget::item{padding:8px;border-radius:4px;}"
        "QListWidget::item:selected{background:rgba(236,65,65,0.16);color:#FF5A5A;}"));
    layout->addWidget(m_folderList);

    auto *folderBtns = new QHBoxLayout;
    folderBtns->setSpacing(8);
    auto *addBtn = new QPushButton(QStringLiteral("添加文件夹"), this);
    auto *removeBtn = new QPushButton(QStringLiteral("移除"), this);
    auto *rescanBtn = new QPushButton(QStringLiteral("重新扫描"), this);
    folderBtns->addWidget(addBtn);
    folderBtns->addWidget(removeBtn);
    folderBtns->addWidget(rescanBtn);
    folderBtns->addStretch(1);
    layout->addLayout(folderBtns);

    connect(addBtn, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择音乐文件夹"));
        if (!dir.isEmpty()) {
            for (int i = 0; i < m_folderList->count(); ++i) {
                if (m_folderList->item(i)->text() == dir)
                    return;
            }
            m_folderList->addItem(dir);
        }
    });
    connect(removeBtn, &QPushButton::clicked, this, [this] {
        delete m_folderList->currentItem();
    });
    connect(rescanBtn, &QPushButton::clicked, this, &SettingsDialog::rescanRequested);

    auto *fontRow = new QHBoxLayout;
    fontRow->setSpacing(10);
    auto *fontLabel = new QLabel(QStringLiteral("歌词字号"), this);
    m_fontSlider = new QSlider(Qt::Horizontal, this);
    m_fontSlider->setRange(12, 30);
    m_fontSlider->setValue(core::SettingsService::lyricFontSize());
    m_fontValue = new QLabel(this);
    fontRow->addWidget(fontLabel);
    fontRow->addWidget(m_fontSlider, 1);
    fontRow->addWidget(m_fontValue);
    layout->addLayout(fontRow);
    connect(m_fontSlider, &QSlider::valueChanged, this, [this](int v) {
        m_fontValue->setText(QString::number(v));
    });
    m_fontValue->setText(QString::number(m_fontSlider->value()));

    // ---------- 在线服务 ----------
    auto *onlineTitle = new QLabel(QStringLiteral("在线服务"), this);
    onlineTitle->setStyleSheet(QStringLiteral("font-weight:600;font-size:14px;"));
    layout->addWidget(onlineTitle);

    auto *apiRow = new QHBoxLayout;
    apiRow->setSpacing(8);
    apiRow->addWidget(new QLabel(QStringLiteral("API 地址"), this));
    m_apiBaseEdit = new QLineEdit(core::SettingsService::onlineApiBase(), this);
    m_apiBaseEdit->setStyleSheet(QStringLiteral(
        "QLineEdit{background:rgba(255,255,255,0.08);border:none;border-radius:6px;padding:6px 10px;color:#E8E8E8;}"));
    apiRow->addWidget(m_apiBaseEdit, 1);
    layout->addLayout(apiRow);

    auto *autoRow = new QHBoxLayout;
    m_autoStartCheck = new QCheckBox(QStringLiteral("自动启动在线服务"), this);
    m_autoStartCheck->setChecked(core::SettingsService::onlineAutoStart());
    autoRow->addWidget(m_autoStartCheck);
    autoRow->addStretch(1);
    layout->addLayout(autoRow);

    m_onlineStatus = new QLabel(QStringLiteral("状态:未知"), this);
    m_onlineStatus->setStyleSheet(QStringLiteral("color:#9A9AA5;font-size:12px;"));
    layout->addWidget(m_onlineStatus);
    auto *startBtn = new QPushButton(QStringLiteral("启动服务"), this);
    auto *stopBtn = new QPushButton(QStringLiteral("停止服务"), this);
    auto *srvRow = new QHBoxLayout;
    srvRow->addWidget(startBtn);
    srvRow->addWidget(stopBtn);
    srvRow->addStretch(1);
    layout->addLayout(srvRow);
    connect(startBtn, &QPushButton::clicked, this, [this] {
        if (m_apiService)
            m_apiService->start();
    });
    connect(stopBtn, &QPushButton::clicked, this, [this] {
        if (m_apiService)
            m_apiService->stop();
    });
    if (m_apiService) {
        connect(m_apiService, &core::ApiService::serverStateChanged, this, [this](bool running) {
            m_onlineStatus->setText(running ? QStringLiteral("状态:运行中") : QStringLiteral("状态:未运行"));
        });
        m_onlineStatus->setText(m_apiService->isRunning() ? QStringLiteral("状态:运行中") : QStringLiteral("状态:未运行"));
    }

    m_loginLabel = new QLabel(this);
    m_loginLabel->setStyleSheet(QStringLiteral("color:#9A9AA5;font-size:12px;"));
    layout->addWidget(m_loginLabel);
    auto *logoutBtn = new QPushButton(QStringLiteral("退出登录"), this);
    layout->addWidget(logoutBtn, 0, Qt::AlignLeft);
    connect(logoutBtn, &QPushButton::clicked, this, [this] {
        if (m_apiClient)
            m_apiClient->logout([](const QJsonObject &) {}, [](const QString &) {});
        core::SettingsService::setOnlineCookie(QString());
        core::SettingsService::setOnlineUid(0);
        core::SettingsService::setOnlineNickname(QString());
        m_loginLabel->setText(QStringLiteral("未登录"));
    });

    auto *cacheRow = new QHBoxLayout;
    cacheRow->setSpacing(8);
    cacheRow->addWidget(new QLabel(QStringLiteral("缓存上限(首)"), this));
    m_cacheCountSpin = new QSpinBox(this);
    m_cacheCountSpin->setRange(10, 2000);
    m_cacheCountSpin->setValue(core::SettingsService::onlineCacheMaxCount());
    cacheRow->addWidget(m_cacheCountSpin);
    cacheRow->addWidget(new QLabel(QStringLiteral("容量(MB)"), this));
    m_cacheMBSpin = new QSpinBox(this);
    m_cacheMBSpin->setRange(100, 20480);
    m_cacheMBSpin->setValue(core::SettingsService::onlineCacheMaxMB());
    cacheRow->addWidget(m_cacheMBSpin);
    cacheRow->addStretch(1);
    layout->addLayout(cacheRow);

    m_cacheStats = new QLabel(this);
    m_cacheStats->setStyleSheet(QStringLiteral("color:#9A9AA5;font-size:12px;"));
    layout->addWidget(m_cacheStats);
    auto *clearBtn = new QPushButton(QStringLiteral("一键清空缓存"), this);
    layout->addWidget(clearBtn, 0, Qt::AlignLeft);
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        if (!m_library)
            return;
        if (QMessageBox::question(this, QStringLiteral("清空缓存"),
                                  QStringLiteral("确定删除所有已缓存的在线歌曲文件?"))
            == QMessageBox::Yes) {
            m_library->clearCache();
            updateCacheStats();
        }
    });

    auto *downloadTitle = new QLabel(QStringLiteral("永久下载"), this);
    downloadTitle->setStyleSheet(QStringLiteral("font-weight:600;font-size:14px;"));
    layout->addWidget(downloadTitle);
    auto *downloadRow = new QHBoxLayout;
    downloadRow->setSpacing(8);
    downloadRow->addWidget(new QLabel(QStringLiteral("保存目录"), this));
    m_downloadDirEdit = new QLineEdit(core::SettingsService::onlineDownloadDir(), this);
    m_downloadDirEdit->setStyleSheet(QStringLiteral(
        "QLineEdit{background:rgba(255,255,255,0.08);border:none;border-radius:6px;padding:6px 10px;color:#E8E8E8;}"));
    auto *chooseDownloadDir = new QPushButton(QStringLiteral("选择"), this);
    downloadRow->addWidget(m_downloadDirEdit, 1);
    downloadRow->addWidget(chooseDownloadDir);
    layout->addLayout(downloadRow);
    connect(chooseDownloadDir, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择永久下载目录"),
                                                               m_downloadDirEdit->text());
        if (!dir.isEmpty())
            m_downloadDirEdit->setText(dir);
    });

    // ---------- 本地数据库 ----------
    auto *databaseTitle = new QLabel(QStringLiteral("本地数据库"), this);
    databaseTitle->setStyleSheet(QStringLiteral("font-weight:600;font-size:14px;"));
    layout->addWidget(databaseTitle);
    auto *databasePath = new QLabel(
        m_library ? QStringLiteral("数据库文件：%1").arg(QDir::toNativeSeparators(m_library->databasePath()))
                  : QStringLiteral("数据库不可用"), this);
    databasePath->setWordWrap(true);
    databasePath->setStyleSheet(QStringLiteral("color:#9A9AA5;font-size:12px;"));
    layout->addWidget(databasePath);
    auto *databaseBtns = new QHBoxLayout;
    auto *openDatabaseBtn = new QPushButton(QStringLiteral("打开数据库目录"), this);
    auto *reloadDatabaseBtn = new QPushButton(QStringLiteral("重新读取数据库"), this);
    databaseBtns->addWidget(openDatabaseBtn);
    databaseBtns->addWidget(reloadDatabaseBtn);
    databaseBtns->addStretch(1);
    layout->addLayout(databaseBtns);
    connect(openDatabaseBtn, &QPushButton::clicked, this, [this] {
        if (m_library)
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_library->databasePath()).absolutePath()));
    });
    connect(reloadDatabaseBtn, &QPushButton::clicked, this, [this] {
        emit databaseReloadRequested();
        QMessageBox::information(this, QStringLiteral("数据库"), QStringLiteral("已重新读取本地数据库。"));
    });
    updateCacheStats();
    m_loginLabel->setText(core::SettingsService::onlineNickname().isEmpty()
                              ? QStringLiteral("未登录")
                              : QStringLiteral("已登录:%1").arg(core::SettingsService::onlineNickname()));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        core::SettingsService::setOnlineApiBase(m_apiBaseEdit->text().trimmed());
        core::SettingsService::setOnlineAutoStart(m_autoStartCheck->isChecked());
        core::SettingsService::setOnlineCacheMaxCount(m_cacheCountSpin->value());
        core::SettingsService::setOnlineCacheMaxMB(m_cacheMBSpin->value());
        core::SettingsService::setOnlineDownloadDir(m_downloadDirEdit->text().trimmed());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    for (const QString &folder : core::SettingsService::musicFolders())
        m_folderList->addItem(folder);
}

QStringList SettingsDialog::folders() const
{
    QStringList folders;
    for (int i = 0; i < m_folderList->count(); ++i)
        folders << m_folderList->item(i)->text();
    return folders;
}

int SettingsDialog::lyricFontSize() const
{
    return m_fontSlider->value();
}

void SettingsDialog::updateCacheStats()
{
    if (!m_library)
        return;
    qint64 bytes = 0;
    int count = 0;
    m_library->cacheUsage(&bytes, &count);
    m_cacheStats->setText(QStringLiteral("缓存:%1 首 · %2 MB")
                              .arg(count)
                              .arg(bytes / 1024 / 1024));
}

} // namespace ui
