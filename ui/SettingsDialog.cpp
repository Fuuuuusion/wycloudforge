#include "SettingsDialog.h"

#include "core/SettingsService.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace ui {

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
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

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
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

} // namespace ui
