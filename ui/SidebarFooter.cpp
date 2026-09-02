#include "ui/SidebarFooter.h"

#include "ui/ThemeManager.h"

#include "ui/AccountSettingsButton.h"
#include "ui/SvgIcon.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QDateTime>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>

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

class DownloadStatusButton final : public QPushButton
{
public:
    enum Status { Empty, Queued, Downloading, Complete };

    explicit DownloadStatusButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setObjectName(QStringLiteral("sidebarDownloadButton"));
        setFixedSize(28, 28);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setToolTip(QStringLiteral("下载管理"));
        setAccessibleName(QStringLiteral("下载管理"));
        m_timer.setInterval(40);
        connect(&m_timer, &QTimer::timeout, this, [this] { update(); });
    }

    void setStatus(Status status)
    {
        if (m_status == status)
            return;
        m_status = status;
        if (status == Downloading)
            m_timer.start();
        else
            m_timer.stop();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (underMouse() || isDown()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(themeColor(isDown() ? ThemeColor::SurfacePressed
                                                 : ThemeColor::SurfaceAlt));
            painter.drawRoundedRect(rect(), 6, 6);
        }
        const QColor color = themeColor(ThemeColor::Focus);
        if (m_status != Downloading) {
            const QString path = m_status == Queued ? QStringLiteral(":/icons/download-queued.png")
                : m_status == Complete ? QStringLiteral(":/icons/download-complete.png")
                                       : QStringLiteral(":/icons/download-local.png");
            const QIcon icon = tintedIcon(path, color);
            icon.paint(&painter, QRect(5, 5, 18, 18));
        } else {
            painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const qreal phase = (QDateTime::currentMSecsSinceEpoch() % 900) / 900.0;
            const qreal y = -3.0 + phase * 24.0;
            painter.save();
            painter.setClipRect(QRectF(4, 3, 20, 17));
            painter.drawLine(QPointF(14, y - 5), QPointF(14, y + 4));
            painter.drawLine(QPointF(10.5, y + 0.5), QPointF(14, y + 4));
            painter.drawLine(QPointF(17.5, y + 0.5), QPointF(14, y + 4));
            painter.restore();
            painter.drawLine(QPointF(7, 21), QPointF(21, 21));
        }
        if (hasFocus()) {
            painter.setPen(QPen(themeColor(ThemeColor::TextTertiary), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 5, 5);
        }
    }

private:
    Status m_status = Empty;
    QTimer m_timer;
};

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
    m_refreshButton->setIcon(makeThemedRasterIcon(QStringLiteral(":/icons/icon-refresh.png")));
    m_refreshButton->setCursor(Qt::PointingHandCursor);
    m_refreshButton->setToolTip(QStringLiteral("刷新当前页面和推荐内容"));
    m_refreshButton->setAccessibleName(QStringLiteral("刷新当前页面和推荐内容"));
    m_refreshButton->setFocusPolicy(Qt::StrongFocus);
    setThemedStyleSheet(m_refreshButton, QStringLiteral(
        "QPushButton#sidebarRefreshButton{background:transparent;border:none;border-radius:6px;padding:0;}"
        "QPushButton#sidebarRefreshButton:hover{background:@surfaceAlt;}"
        "QPushButton#sidebarRefreshButton:pressed{background:@surfacePressed;}"
        "QPushButton#sidebarRefreshButton:disabled{background:transparent;}"));
    layout->addWidget(m_refreshButton, 0, Qt::AlignLeft | Qt::AlignBottom);

    m_downloadButton = new DownloadStatusButton(this);
    layout->addWidget(m_downloadButton, 0, Qt::AlignLeft | Qt::AlignBottom);
    layout->addStretch(1);

    connect(m_settingsButton, &QPushButton::clicked, this, &SidebarFooter::settingsClicked);
    connect(m_refreshButton, &QPushButton::clicked, this, &SidebarFooter::refreshClicked);
    connect(m_downloadButton, &QPushButton::clicked, this, &SidebarFooter::downloadClicked);
}

void SidebarFooter::setDownloadStatus(bool downloading, bool queued, bool hasDownloads)
{
    auto *button = static_cast<DownloadStatusButton *>(m_downloadButton);
    button->setStatus(downloading ? DownloadStatusButton::Downloading
                      : queued ? DownloadStatusButton::Queued
                      : hasDownloads ? DownloadStatusButton::Complete
                                     : DownloadStatusButton::Empty);
}

} // namespace ui
