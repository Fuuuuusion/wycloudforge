#include "ThemeManager.h"

#include "core/SettingsService.h"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QStyleHints>
#include <QWidget>

namespace ui {
namespace {

struct ThemePalette
{
    QColor pageBackground;
    QColor surface;
    QColor surfaceAlt;
    QColor surfaceHover;
    QColor surfacePressed;
    QColor accent;
    QColor accentHover;
    QColor accentPressed;
    QColor accentSoft;
    QColor textPrimary;
    QColor textSecondary;
    QColor textTertiary;
    QColor disabledText;
    QColor progressTrack;
    QColor success;
    QColor focus;
    QColor textOnAccent;
};

ThemePalette darkPalette()
{
    return {
        QColor(QStringLiteral("#0E0E14")), QColor(QStringLiteral("#16161E")),
        QColor(QStringLiteral("#1B1B24")), QColor(QStringLiteral("#2A2A36")),
        QColor(QStringLiteral("#24242E")), QColor(QStringLiteral("#EC4141")),
        QColor(QStringLiteral("#FF5A5A")), QColor(QStringLiteral("#D63838")),
        QColor(QStringLiteral("#3A2024")), QColor(QStringLiteral("#E8E8E8")),
        QColor(QStringLiteral("#9A9AA5")), QColor(QStringLiteral("#6E6E7A")),
        QColor(QStringLiteral("#55555F")), QColor(QStringLiteral("#33333F")),
        QColor(QStringLiteral("#8FCB9B")), QColor(QStringLiteral("#B8B8C4")),
        QColor(QStringLiteral("#FFFFFF")),
    };
}

ThemePalette lightPalette()
{
    return {
        QColor(QStringLiteral("#F7F7FA")), QColor(QStringLiteral("#FFFFFF")),
        QColor(QStringLiteral("#F0F1F5")), QColor(QStringLiteral("#ECEEF3")),
        QColor(QStringLiteral("#E1E4EA")), QColor(QStringLiteral("#D9363E")),
        QColor(QStringLiteral("#E24952")), QColor(QStringLiteral("#C82E36")),
        QColor(QStringLiteral("#F8E3E5")), QColor(QStringLiteral("#202126")),
        QColor(QStringLiteral("#60636D")), QColor(QStringLiteral("#8A8E99")),
        QColor(QStringLiteral("#A8ACB6")), QColor(QStringLiteral("#D9DCE3")),
        QColor(QStringLiteral("#39824A")), QColor(QStringLiteral("#747985")),
        QColor(QStringLiteral("#FFFFFF")),
    };
}

QString cssColor(const QColor &color)
{
    return color.name(QColor::HexRgb).toUpper();
}

} // namespace

ThemeManager &ThemeManager::instance()
{
    static ThemeManager manager;
    return manager;
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
}

void ThemeManager::initialize(QApplication *application)
{
    if (!application || m_application == application)
        return;
    m_application = application;
    m_application->setStyle(QStringLiteral("Fusion"));

    QFile file(QStringLiteral(":/theme.qss"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        m_styleSheetTemplate = QString::fromUtf8(file.readAll());

    const int stored = core::SettingsService::themeMode(int(ThemeMode::Dark));
    m_mode = stored >= int(ThemeMode::FollowSystem) && stored <= int(ThemeMode::Light)
        ? static_cast<ThemeMode>(stored) : ThemeMode::Dark;

    connect(m_application->styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this](Qt::ColorScheme) {
        if (m_mode == ThemeMode::FollowSystem)
            apply(false);
    });
    apply(false);
}

void ThemeManager::setMode(ThemeMode mode)
{
    if (mode < ThemeMode::FollowSystem || mode > ThemeMode::Light)
        mode = ThemeMode::Dark;
    if (m_mode == mode && m_resolvedMode == resolveMode(mode))
        return;
    m_mode = mode;
    apply(true);
}

ThemeMode ThemeManager::resolveMode(ThemeMode requested) const
{
    if (requested != ThemeMode::FollowSystem || !m_application)
        return requested == ThemeMode::Light ? ThemeMode::Light : ThemeMode::Dark;
    return m_application->styleHints()->colorScheme() == Qt::ColorScheme::Light
        ? ThemeMode::Light : ThemeMode::Dark;
}

QColor ThemeManager::color(ThemeColor role) const
{
    const ThemePalette palette = isDark() ? darkPalette() : lightPalette();
    switch (role) {
    case ThemeColor::PageBackground: return palette.pageBackground;
    case ThemeColor::Surface: return palette.surface;
    case ThemeColor::SurfaceAlt: return palette.surfaceAlt;
    case ThemeColor::SurfaceHover: return palette.surfaceHover;
    case ThemeColor::SurfacePressed: return palette.surfacePressed;
    case ThemeColor::Accent: return palette.accent;
    case ThemeColor::AccentHover: return palette.accentHover;
    case ThemeColor::AccentPressed: return palette.accentPressed;
    case ThemeColor::AccentSoft: return palette.accentSoft;
    case ThemeColor::TextPrimary: return palette.textPrimary;
    case ThemeColor::TextSecondary: return palette.textSecondary;
    case ThemeColor::TextTertiary: return palette.textTertiary;
    case ThemeColor::DisabledText: return palette.disabledText;
    case ThemeColor::ProgressTrack: return palette.progressTrack;
    case ThemeColor::Success: return palette.success;
    case ThemeColor::Focus: return palette.focus;
    case ThemeColor::TextOnAccent: return palette.textOnAccent;
    }
    return palette.textPrimary;
}

QString ThemeManager::renderStyleSheet(QString styleTemplate) const
{
    static const QPair<const char *, ThemeColor> tokens[] = {
        { "@pageBackground", ThemeColor::PageBackground },
        { "@surfaceHover", ThemeColor::SurfaceHover },
        { "@surfacePressed", ThemeColor::SurfacePressed },
        { "@surfaceAlt", ThemeColor::SurfaceAlt },
        { "@surface", ThemeColor::Surface },
        { "@accentHover", ThemeColor::AccentHover },
        { "@accentPressed", ThemeColor::AccentPressed },
        { "@accentSoft", ThemeColor::AccentSoft },
        { "@accent", ThemeColor::Accent },
        { "@textPrimary", ThemeColor::TextPrimary },
        { "@textSecondary", ThemeColor::TextSecondary },
        { "@textTertiary", ThemeColor::TextTertiary },
        { "@disabledText", ThemeColor::DisabledText },
        { "@progressTrack", ThemeColor::ProgressTrack },
        { "@success", ThemeColor::Success },
        { "@focus", ThemeColor::Focus },
        { "@textOnAccent", ThemeColor::TextOnAccent },
    };
    for (const auto &token : tokens)
        styleTemplate.replace(QLatin1String(token.first), cssColor(color(token.second)));
    return styleTemplate;
}

void ThemeManager::apply(bool persist)
{
    if (!m_application)
        return;
    m_resolvedMode = resolveMode(m_mode);
    const ThemePalette theme = isDark() ? darkPalette() : lightPalette();

    QPalette palette;
    palette.setColor(QPalette::Window, theme.pageBackground);
    palette.setColor(QPalette::WindowText, theme.textPrimary);
    palette.setColor(QPalette::Base, theme.surface);
    palette.setColor(QPalette::AlternateBase, theme.surfaceAlt);
    palette.setColor(QPalette::Text, theme.textPrimary);
    palette.setColor(QPalette::Button, theme.surfaceAlt);
    palette.setColor(QPalette::ButtonText, theme.textPrimary);
    palette.setColor(QPalette::Light, theme.surfaceHover);
    palette.setColor(QPalette::Midlight, theme.surfacePressed);
    palette.setColor(QPalette::Dark, theme.accentSoft);
    palette.setColor(QPalette::Mid, theme.disabledText);
    palette.setColor(QPalette::Shadow, theme.textSecondary);
    palette.setColor(QPalette::Highlight, theme.accent);
    palette.setColor(QPalette::HighlightedText, theme.textOnAccent);
    palette.setColor(QPalette::Link, theme.accentHover);
    palette.setColor(QPalette::LinkVisited, theme.accentPressed);
    palette.setColor(QPalette::ToolTipBase, theme.surface);
    palette.setColor(QPalette::ToolTipText, theme.textPrimary);
    palette.setColor(QPalette::PlaceholderText, theme.textTertiary);
    palette.setColor(QPalette::Accent, theme.accent);
    palette.setColor(QPalette::Disabled, QPalette::Text, theme.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, theme.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, theme.disabledText);
    m_application->setPalette(palette);
    m_application->setProperty("theme", isDark() ? "dark" : "light");
    reloadApplicationStyleSheet();
    if (persist)
        core::SettingsService::setThemeMode(int(m_mode));
    emit themeChanged(m_mode, m_resolvedMode);
}

void ThemeManager::reloadApplicationStyleSheet()
{
    if (!m_application)
        return;
    m_application->setStyleSheet(QString());
    m_application->setStyleSheet(renderStyleSheet(m_styleSheetTemplate));
    const auto widgets = m_application->allWidgets();
    for (QWidget *widget : widgets) {
        const QVariant themeTemplate = widget->property("netecloneThemeStyleTemplate");
        if (themeTemplate.isValid())
            widget->setStyleSheet(renderStyleSheet(themeTemplate.toString()));
    }
}

QColor themeColor(ThemeColor role)
{
    return ThemeManager::instance().color(role);
}

void setThemedStyleSheet(QWidget *widget, const QString &styleTemplate)
{
    if (!widget)
        return;
    widget->setProperty("netecloneThemeStyleTemplate", styleTemplate);
    widget->setStyleSheet(ThemeManager::instance().renderStyleSheet(styleTemplate));
}

} // namespace ui
