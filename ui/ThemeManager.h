#pragma once

#include <QColor>
#include <QObject>
#include <QString>

class QApplication;
class QWidget;

namespace ui {

enum class ThemeMode : int
{
    FollowSystem = 0,
    Dark = 1,
    Light = 2,
};

enum class ThemeColor : int
{
    PageBackground,
    Surface,
    SurfaceAlt,
    SurfaceHover,
    SurfacePressed,
    Accent,
    AccentHover,
    AccentPressed,
    AccentSoft,
    TextPrimary,
    TextSecondary,
    TextTertiary,
    DisabledText,
    ProgressTrack,
    Success,
    Focus,
    TextOnAccent,
};

class ThemeManager final : public QObject
{
    Q_OBJECT
public:
    static ThemeManager &instance();

    void initialize(QApplication *application);
    ThemeMode mode() const { return m_mode; }
    ThemeMode resolvedMode() const { return m_resolvedMode; }
    bool isDark() const { return m_resolvedMode == ThemeMode::Dark; }

    void setMode(ThemeMode mode);
    QColor color(ThemeColor role) const;
    QString renderStyleSheet(QString styleTemplate) const;

signals:
    void themeChanged(ui::ThemeMode mode, ui::ThemeMode resolvedMode);

private:
    explicit ThemeManager(QObject *parent = nullptr);
    ThemeMode resolveMode(ThemeMode requested) const;
    void apply(bool persist);
    void reloadApplicationStyleSheet();

    QApplication *m_application = nullptr;
    ThemeMode m_mode = ThemeMode::Dark;
    ThemeMode m_resolvedMode = ThemeMode::Dark;
    QString m_styleSheetTemplate;
};

QColor themeColor(ThemeColor role);
void setThemedStyleSheet(QWidget *widget, const QString &styleTemplate);

} // namespace ui

Q_DECLARE_METATYPE(ui::ThemeMode)
