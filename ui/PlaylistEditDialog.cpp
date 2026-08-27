#include "PlaylistEditDialog.h"

#include "core/PlaylistController.h"
#include "ui/CoverProvider.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace ui {
namespace {
QString coverDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/playlist_covers");
    QDir().mkpath(dir);
    return dir;
}
}

PlaylistEditDialog::PlaylistEditDialog(core::PlaylistController *playlists, int playlistId,
                                       const QString &name, const QString &description,
                                       const QString &coverPath, QWidget *parent)
    : QDialog(parent)
    , m_playlists(playlists)
    , m_playlistId(playlistId)
    , m_coverPath(coverPath)
{
    setWindowTitle(QStringLiteral("编辑歌单"));
    setObjectName("playlistEditDialog");
    setFixedWidth(360);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("编辑歌单"), this);
    title->setProperty("class", "pageTitle");
    layout->addWidget(title);

    m_coverLabel = new QLabel(this);
    m_coverLabel->setFixedSize(96, 96);
    m_coverLabel->setScaledContents(false);
    if (!m_coverPath.isEmpty()) {
        QPixmap pm(m_coverPath);
        m_coverLabel->setPixmap(pm.scaled(96, 96, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    } else {
        m_coverLabel->setPixmap(CoverProvider::placeholder(name, 96, 8));
    }

    auto *coverRow = new QHBoxLayout;
    coverRow->setSpacing(12);
    coverRow->addWidget(m_coverLabel);
    auto *coverBtn = new QPushButton(QStringLiteral("选择封面"), this);
    coverBtn->setCursor(Qt::PointingHandCursor);
    connect(coverBtn, &QPushButton::clicked, this, &PlaylistEditDialog::chooseCover);
    coverRow->addWidget(coverBtn, 0, Qt::AlignTop);
    coverRow->addStretch(1);
    layout->addLayout(coverRow);

    auto *nameLabel = new QLabel(QStringLiteral("歌单名称"), this);
    nameLabel->setProperty("class", "sectionTitle");
    layout->addWidget(nameLabel);
    m_name = new QLineEdit(name, this);
    m_name->setPlaceholderText(QStringLiteral("歌单名称"));
    layout->addWidget(m_name);

    auto *descLabel = new QLabel(QStringLiteral("简介"), this);
    descLabel->setProperty("class", "sectionTitle");
    layout->addWidget(descLabel);
    m_desc = new QPlainTextEdit(description, this);
    m_desc->setFixedHeight(80);
    m_desc->setPlaceholderText(QStringLiteral("写点什么…"));
    layout->addWidget(m_desc);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    auto *cancel = new QPushButton(QStringLiteral("取消"), this);
    auto *save = new QPushButton(QStringLiteral("保存"), this);
    save->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;border-radius:14px;background:#EC4141;color:white;padding:7px 22px;}"
        "QPushButton:hover{background:#FF5A5A;}"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(cancel);
    btnRow->addWidget(save);
    layout->addLayout(btnRow);
}

void PlaylistEditDialog::chooseCover()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择封面"), QString(),
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp *.webp)"));
    if (path.isEmpty())
        return;
    const QString ext = QFileInfo(path).suffix();
    const QString dest = coverDir()
        + QStringLiteral("/pl%1_%2.%3").arg(m_playlistId).arg(QDateTime::currentMSecsSinceEpoch()).arg(ext);
    if (QFile::copy(path, dest))
        m_coverPath = dest;
    else
        m_coverPath = path;
    QPixmap pm(m_coverPath);
    if (!pm.isNull())
        m_coverLabel->setPixmap(pm.scaled(96, 96, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
}

void PlaylistEditDialog::accept()
{
    const QString name = m_name->text().trimmed();
    if (name.isEmpty()) {
        m_name->setFocus();
        return;
    }
    if (m_playlists && m_playlistId > 0) {
        m_playlists->renamePlaylist(m_playlistId, name);
        m_playlists->setPlaylistDescription(m_playlistId, m_desc->toPlainText());
        if (!m_coverPath.isEmpty())
            m_playlists->setPlaylistCover(m_playlistId, m_coverPath);
    }
    QDialog::accept();
}

} // namespace ui
