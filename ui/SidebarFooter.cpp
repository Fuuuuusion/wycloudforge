#include "ui/SidebarFooter.h"

#include "ui/AccountSettingsButton.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QPushButton>

namespace ui {
namespace {

QIcon tintedIcon(const QString &resourcePath, const QColor &color)
{
    const QImage source(resourcePath);
    if (source.isNull())
        return {};
    QImage tinted(source.size(), QImage::Format_ARGB32);
    tinted.fill(Qt::transparent);
    for (int y = 0; y < source.height(); ++y) {
        QRgb *target = reinterpret_cast<QRgb *>(tinted.scanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            target[x] = qRgba(color.red(), color.green(), color.blue(),
                              source.pixelColor(x, y).alpha());
        }
    }
    return QIcon(QPixmap::fromImage(tinted));
}

} // namespace

SidebarFooter::SidebarFooter(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("settingsFooter"));
    setFixedHeight(128);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    m_settingsButton = new AccountSettingsButton(this);
    layout->addWidget(m_settingsButton, 0, Qt::AlignLeft | Qt::AlignBottom);

    m_refreshButton = new QPushButton(this);
    m_refreshButton->setObjectName(QStringLiteral("sidebarRefreshButton"));
    m_refreshButton->setFixedSize(28, 28);
    m_refreshButton->setIconSize(QSize(18, 18));
    m_refreshButton->setIcon(tintedIcon(QStringLiteral(":/icons/icon-refresh.png"),
                                        QColor(QStringLiteral("#B8B8C4"))));
    m_refreshButton->setCursor(Qt::PointingHandCursor);
    m_refreshButton->setToolTip(QStringLiteral("刷新当前页面和推荐内容"));
    m_refreshButton->setAccessibleName(QStringLiteral("刷新当前页面和推荐内容"));
    m_refreshButton->setFocusPolicy(Qt::StrongFocus);
    m_refreshButton->setStyleSheet(QStringLiteral(
        "QPushButton#sidebarRefreshButton{background:transparent;border:none;border-radius:6px;padding:0;}"
        "QPushButton#sidebarRefreshButton:hover{background:#1B1B24;}"
        "QPushButton#sidebarRefreshButton:pressed{background:#24242E;}"
        "QPushButton#sidebarRefreshButton:disabled{background:transparent;}"));
    layout->addWidget(m_refreshButton, 0, Qt::AlignLeft | Qt::AlignBottom);
    layout->addStretch(1);

    connect(m_settingsButton, &QPushButton::clicked, this, &SidebarFooter::settingsClicked);
    connect(m_refreshButton, &QPushButton::clicked, this, &SidebarFooter::refreshClicked);
}

} // namespace ui
