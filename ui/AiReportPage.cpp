#include "ui/AiReportPage.h"

#include "ui/ThemeManager.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

namespace ui {
namespace {

const QList<QString> &demoReports()
{
    static const QList<QString> reports = {
        QStringLiteral(
            "AI 听歌报告\n"
            "\n"
            "从这段时期的播放记录来看，你的听歌世界以华语流行和抒情歌曲为主，旋律亲切、节奏适中，"
            "歌词大多围绕成长、想念、告别和内心独白展开。你很少长时间停留在单一情绪里，"
            "而是在温柔、轻快的曲风之间自然切换，说明你既需要陪伴感，也愿意从音乐里找回一点能量。\n"
            "\n"
            "你的播放习惯并不杂乱。重复出现的歌手大多拥有鲜明的叙事感，像周杰伦、孙燕姿、"
            "邓紫棋和林俊杰，他们的作品在编曲上重视声音层次，副歌又容易被记住。"
            "这种选择说明你在意一首歌能不能在短短几分钟里讲清一段心情，而不是只追求高音或节奏冲击。\n"
            "\n"
            "从时间上看，你的收听更偏向夜晚和通勤时段。夜晚的你更愿意听慢歌，歌词和旋律会成为情绪的出口；"
            "白天则会被节奏明快的作品带动，让注意力更集中。整体而言，你的曲库像一座被认真整理过的情绪档案，"
            "适合放松、学习、散步，也适合在想要安静陪伴时慢慢播放。\n"
            "\n"
            "推荐歌曲：\n"
            "1. 《晴天》 - 周杰伦\n"
            "2. 《遇见》 - 孙燕姿\n"
            "3. 《光年之外》 - 邓紫棋\n"
            "4. 《江南》 - 林俊杰\n"
            "5. 《平凡之路》 - 朴树\n"
            "6. 《起风了》 - 买辣椒也用券\n"
            "7. 《慢慢喜欢你》 - 莫文蔚\n"
            "8. 《旅行中忘记》 - 袁娅维\n"),
        QStringLiteral(
            "AI 听歌报告\n"
            "\n"
            "最近这段时间，你更倾向于选择旋律有记忆点、歌词有画面的华语流行歌曲。"
            "相比纯器乐或追求编曲复杂度的作品，你更容易被“一段故事加一个副歌”的写法吸引。"
            "你喜欢的歌通常不是瞬间爆发出来的情绪，而是先铺垫场景，再在副歌里慢慢释放，"
            "这种结构会让你的听歌过程更像一次完整的情绪旅行。\n"
            "\n"
            "你的歌单里有不少适合循环播放的作品。它们不一定是最热门的歌曲，却总能在某个恰当的时刻出现："
            "或是放学回家的路上，或是深夜一个人发呆的时候。重复播放背后，其实是你对熟悉旋律的依赖，"
            "这种依赖不是单调，而是你在用音乐确认自己的状态。\n"
            "\n"
            "从整体口味来看，你偏爱细腻、克制、略带故事感的表达。蔡健雅、林俊杰、陈奕迅和邓紫棋都属于"
            "这种类型：唱腔有辨识度，编曲不喧宾夺主，情绪却非常饱满。如果你接下来想尝试一些新风格，"
            "可以从城市民谣、轻摇滚或带节奏感的 R&B 入手，它们会保留你熟悉的旋律感，同时带来一点新鲜空气。\n"
            "\n"
            "推荐歌曲：\n"
            "1. 《空白格》 - 蔡健雅\n"
            "2. 《修炼爱情》 - 林俊杰\n"
            "3. 《十年》 - 陈奕迅\n"
            "4. 《泡沫》 - 邓紫棋\n"
            "5. 《平凡的一天》 - 毛不易\n"
            "6. 《被风吹过的夏天》 - 林俊杰/金莎\n"
            "7. 《不再联系》 - 夏天Alex\n"
            "8. 《慢慢来》 - 阿肆\n"),
        QStringLiteral(
            "AI 听歌报告\n"
            "\n"
            "如果把你最近播放的歌单打开，会发现它不是随机生长的，而是一条有明确情绪走向的曲线。"
            "多数歌曲都带着柔和的华语流行底色，旋律容易跟着哼唱，歌词又能在不经意间戳中某个瞬间。"
            "你并不排斥高亢或热烈的表达，但真正让你反复聆听的，往往是那些安静、真诚、留有呼吸感的作品。\n"
            "\n"
            "你的收听习惯也反映出一种稳定的生活节奏。工作或学习的间隙，你会用轻快的歌曲调节状态；"
            "到了夜晚，你更愿意把音量调低，让慢歌填满房间。你的歌单因此同时承担了陪伴、治愈和记录心情的作用，"
            "它不像一个被精心打理的收藏夹，更像一本不断更新的日记。\n"
            "\n"
            "从风格上来说，你的偏好集中在“华语流行 + 抒情 + 轻叙事”这个区间。周杰伦的旋律想象力、"
            "孙燕姿的直接坦诚、朴树的疏离感和袁娅维的细腻声音，都构成你音乐品味的一部分。"
            "如果你希望在保留这种风格的同时拓宽边界，可以试试更轻快的城市流行、带吉他质感的独立民谣，"
            "以及节奏舒缓的 R&B，它们会让你在熟悉的氛围里发现新的情绪层次。\n"
            "\n"
            "推荐歌曲：\n"
            "1. 《东南西北》 - 孙燕姿\n"
            "2. 《七里香》 - 周杰伦\n"
            "3. 《生如夏花》 - 朴树\n"
            "4. 《说散就散》 - 袁娅维\n"
            "5. 《如果没有你》 - 莫文蔚\n"
            "6. 《最好的我们》 - 陈粒\n"
            "7. 《你曾是少年》 - S.H.E\n"
            "8. 《背对背拥抱》 - 林俊杰\n")
    };
    return reports;
}

QString nextDemoReport()
{
    static int index = 0;
    const QList<QString> &reports = demoReports();
    const QString selected = reports.at(index % reports.size());
    ++index;
    return selected;
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
        QStringLiteral("AI 正在根据你的播放记录生成听歌报告与推荐歌曲。"), this);
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
        beginStream();
    });

    m_streamTimer = new QTimer(this);
    m_streamTimer->setInterval(m_streamIntervalMs);
    connect(m_streamTimer, &QTimer::timeout, this, [this] {
        streamTick();
    });
}

void AiReportPage::startDemo()
{
    m_loadingTimer->stop();
    m_demoTimer->stop();
    m_streamTimer->stop();
    m_dotCount = 0;
    m_streamPosition = 0;
    m_selectedReport = nextDemoReport();
    m_statusLabel->setText(QStringLiteral("正在根据历史听歌总结…"));
    m_statusLabel->show();
    m_reportView->hide();
    m_generateButton->hide();
    m_loadingTimer->start(350);
    m_demoTimer->start(m_demoDelayMs > 0 ? 1200 : 0);
}

void AiReportPage::setDemoDelayMsForTesting(int ms)
{
    m_demoDelayMs = qMax(0, ms);
}

void AiReportPage::completeForTesting()
{
    finishDemo();
}

void AiReportPage::beginStream()
{
    m_loadingTimer->stop();
    m_statusLabel->setText(QStringLiteral("正在根据历史听歌总结…"));
    m_streamPosition = 0;
    m_reportView->setPlainText(QString());
    m_reportView->show();
    m_streamTimer->start(m_streamIntervalMs);
}

void AiReportPage::streamTick()
{
    m_streamPosition = qMin(m_selectedReport.size(),
                            m_streamPosition + m_streamChunkSize);
    m_reportView->setPlainText(m_selectedReport.left(m_streamPosition));
    if (QScrollBar *bar = m_reportView->verticalScrollBar())
        bar->setValue(bar->maximum());
    if (m_streamPosition >= m_selectedReport.size())
        finishDemo();
}

void AiReportPage::finishDemo()
{
    m_loadingTimer->stop();
    m_demoTimer->stop();
    m_streamTimer->stop();
    m_statusLabel->setText(QStringLiteral("报告生成完成"));
    m_reportView->setPlainText(m_selectedReport);
    m_reportView->show();
    m_generateButton->show();
    m_generateButton->setEnabled(true);
}

void AiReportPage::updateLoadingDots()
{
    m_dotCount = (m_dotCount + 1) % 3;
    m_statusLabel->setText(
        QStringLiteral("正在根据历史听歌总结%1")
            .arg(QString(m_dotCount + 1, QLatin1Char('.'))));
}

} // namespace ui
