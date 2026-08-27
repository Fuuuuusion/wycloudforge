#include "TitleBar.h"

#include "ui/SvgIcon.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace ui {

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("titleBar");
    setFixedHeight(48);

    auto *brand = new QLabel(this);
    brand->setFixedSize(30, 30);
    brand->setPixmap(makeSvgIcon(QStringLiteral(":/icons/logo.svg")).pixmap(30, 30));

    auto *brandName = new QLabel(QStringLiteral("仿网易云播放器"), this);
    brandName->setObjectName("brandName");

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName("searchEdit");
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索音乐、歌手、专辑"));
    m_searchEdit->setFixedSize(340, 30);
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->addAction(makeSvgIcon(QStringLiteral(":/icons/icon-search.svg")), QLineEdit::LeadingPosition);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this] {
        emit searchRequested(m_searchEdit->text().trimmed());
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (text.trimmed().isEmpty())
            emit searchRequested(QString());
    });

    auto makeButton = [this](const QString &icon, const QString &tip, const char *propValue) {
        auto *btn = new QPushButton(this);
        btn->setProperty("class", propValue);
        btn->setIcon(QIcon(icon));
        btn->setIconSize(QSize(18, 18));
        btn->setFixedSize(42, 32);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
    };

    m_settingsBtn = makeButton(QStringLiteral(":/icons/icon-settings.svg"), QStringLiteral("设置"), "winBtn");
    m_minBtn = makeButton(QStringLiteral(":/icons/icon-min.svg"), QStringLiteral("最小化"), "winBtn");
    m_maxBtn = makeButton(QStringLiteral(":/icons/icon-max.svg"), QStringLiteral("最大化"), "winBtn");
    m_closeBtn = makeButton(QStringLiteral(":/icons/icon-close.svg"), QStringLiteral("关闭"), "winClose");

    connect(m_settingsBtn, &QPushButton::clicked, this, &TitleBar::settingsClicked);
    connect(m_minBtn, &QPushButton::clicked, this, &TitleBar::minimizeClicked);
    connect(m_maxBtn, &QPushButton::clicked, this, &TitleBar::maximizeClicked);
    connect(m_closeBtn, &QPushButton::clicked, this, &TitleBar::closeClicked);

    auto *searchBox = new QWidget(this);
    auto *searchLayout = new QHBoxLayout(searchBox);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->addWidget(m_searchEdit);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 0, 8, 0);
    layout->setSpacing(9);
    layout->addWidget(brand);
    layout->addWidget(brandName);
    layout->addStretch(1);
    layout->addWidget(searchBox, 0, Qt::AlignCenter);
    layout->addStretch(1);
    layout->addWidget(m_settingsBtn);
    layout->addWidget(m_minBtn);
    layout->addWidget(m_maxBtn);
    layout->addWidget(m_closeBtn);
}

void TitleBar::setMaximizedState(bool maximized)
{
    m_maxBtn->setIcon(QIcon(maximized ? QStringLiteral(":/icons/icon-restore.svg")
                                      : QStringLiteral(":/icons/icon-max.svg")));
    m_maxBtn->setToolTip(maximized ? QStringLiteral("还原") : QStringLiteral("最大化"));
}

QRect TitleBar::windowButtonRect(int index) const
{
    QPushButton *btn = nullptr;
    switch (index) {
    case SettingsBtn: btn = m_settingsBtn; break;
    case MinimizeBtn: btn = m_minBtn; break;
    case MaximizeBtn: btn = m_maxBtn; break;
    case CloseBtn: btn = m_closeBtn; break;
    default: return {};
    }
    if (!btn)
        return {};
    return QRect(btn->mapTo(this, QPoint(0, 0)), btn->size());
}

} // namespace ui
