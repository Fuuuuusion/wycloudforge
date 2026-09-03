#include "ui/AiReportPage.h"

#include "ui/ThemeManager.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

namespace ui {
namespace {

const QString &demoReportText()
{
    static const QString text = QStringLiteral(
        "AI 听歌报告（演示版）\n"
        "\n"
        "根据你的播放记录，整体听歌风格以华语流行和抒情为主，节奏相对舒缓，情绪表达细腻。"
        "周杰伦、孙燕姿、邓紫棋等歌手出现的频率较高，说明你偏好旋律辨识度高、歌词有故事感的作品。\n"
        "\n"
        "你的听歌口味比较稳定，既喜欢温暖治愈的慢歌，也会被轻快节奏吸引。"
        "近期听歌时间多集中在晚上，适合放松、通勤或学习时播放。"
        "整体来看，你的歌单更偏向“安静陪伴”和“情绪共鸣”。\n"
        "\n"
        "推荐歌曲：\n"
        "1. 《晴天》 - 周杰伦\n"
        "2. 《遇见》 - 孙燕姿\n"
        "3. 《平凡之路》 - 朴树\n"
        "4. 《光年之外》 - 邓紫棋\n"
        "5. 《起风了》 - 买辣椒也用券\n"
        "6. 《南山南》 - 马頔\n"
        "7. 《慢慢喜欢你》 - 莫文蔚\n"
        "8. 《旅行中忘记》 - 袁娅维\n");
    return text;
}

QPushButton *makeReportButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(32);
    setThemedStyleSheet(button, QStringLiteral(
        "QPushButton{border:none;background:@surfaceAlt;color:@textSecondary;"
        "padding:5px 14px;border-radius:16px;font-size:13px;}"
        "QPushButton:hover{background:@accentSoft;color:@accent;}"
        "QPushButton:disabled{background:@surfaceAlt;color:@textTertiary;}"));
    return button;
}

} // namespace

AiReportPage::AiReportPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("aiReportPage"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 28);
    layout->setSpacing(16);

    auto *header = new QHBoxLayout;
    m_backButton = makeReportButton(QStringLiteral("返回"), this);
    m_backButton->setFixedSize(68, 32);
    connect(m_backButton, &QPushButton::clicked, this, &AiReportPage::backRequested);
    header->addWidget(m_backButton);
    header->addSpacing(12);

    m_titleLabel = new QLabel(QStringLiteral("AI 听歌报告"), this);
    m_titleLabel->setProperty("class", "pageTitle");
    header->addWidget(m_titleLabel);
    header->addStretch(1);
    layout->addLayout(header);

    m_hintLabel = new QLabel(
        QStringLiteral("AI 会根据你的播放记录生成听歌报告与推荐歌曲。"), this);
    setThemedStyleSheet(m_hintLabel, QStringLiteral(
        "color:@textTertiary;font-size:12px;"));
    layout->addWidget(m_hintLabel);

    m_statusLabel = new QLabel(QStringLiteral("准备生成"), this);
    m_statusLabel->setObjectName(QStringLiteral("aiReportStatus"));
    setThemedStyleSheet(m_statusLabel, QStringLiteral(
        "color:@textSecondary;font-size:14px;"));
    layout->addWidget(m_statusLabel);

    m_reportView = new QTextBrowser(this);
    m_reportView->setObjectName(QStringLiteral("aiReportView"));
    m_reportView->setFrameShape(QFrame::NoFrame);
    m_reportView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_reportView->setReadOnly(true);
    m_reportView->setTextInteractionFlags(Qt::TextSelectableByMouse);
    setThemedStyleSheet(m_reportView, QStringLiteral(
        "QTextBrowser{background:@surface;border:none;border-radius:10px;"
        "padding:18px;color:@textPrimary;font-size:14px;}"
        "QTextBrowser{selection-background-color:@accentSoft;"
        "selection-color:@accent;}"));
    m_reportView->hide();
    layout->addWidget(m_reportView, 1);

    auto *actionRow = new QHBoxLayout;
    m_generateButton = makeReportButton(QStringLiteral("重新生成"), this);
    m_generateButton->setObjectName(QStringLiteral("aiReportGenerateButton"));
    connect(m_generateButton, &QPushButton::clicked, this, &AiReportPage::startDemo);
    actionRow->addWidget(m_generateButton);
    actionRow->addStretch(1);
    layout->addLayout(actionRow);
    m_generateButton->hide();

    m_loadingTimer = new QTimer(this);
    m_loadingTimer->setInterval(350);
    connect(m_loadingTimer, &QTimer::timeout, this, [this] {
        updateLoadingDots();
    });

    m_demoTimer = new QTimer(this);
    m_demoTimer->setSingleShot(true);
    connect(m_demoTimer, &QTimer::timeout, this, [this] {
        finishDemo();
    });
}

void AiReportPage::startDemo()
{
    m_loadingTimer->stop();
    m_demoTimer->stop();
    m_dotCount = 0;
    m_statusLabel->setText(QStringLiteral("AI 正在分析你的听歌偏好…"));
    m_statusLabel->show();
    m_reportView->hide();
    m_generateButton->hide();
    m_loadingTimer->start(350);
    m_demoTimer->start(m_demoDelayMs);
}

void AiReportPage::setDemoDelayMsForTesting(int ms)
{
    m_demoDelayMs = qMax(0, ms);
}

void AiReportPage::completeForTesting()
{
    finishDemo();
}

void AiReportPage::finishDemo()
{
    m_loadingTimer->stop();
    m_demoTimer->stop();
    m_statusLabel->setText(QStringLiteral("报告生成完成"));
    m_reportView->setPlainText(demoReportText());
    m_reportView->show();
    m_generateButton->show();
    m_generateButton->setEnabled(true);
}

void AiReportPage::updateLoadingDots()
{
    m_dotCount = (m_dotCount + 1) % 3;
    m_statusLabel->setText(
        QStringLiteral("AI 正在分析你的听歌偏好%1")
            .arg(QString(m_dotCount + 1, QLatin1Char('.'))));
}

} // namespace ui
