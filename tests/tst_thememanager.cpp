#include "core/SettingsService.h"
#include "ui/SvgIcon.h"
#include "ui/SettingsDialog.h"
#include "ui/ThemeManager.h"
#include "ui/ThemeSurface.h"

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>
#include <QWidget>
#include <QtTest>

using namespace core;
using namespace ui;

namespace {

QColor strongestPixel(const QPixmap &pixmap)
{
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    QColor strongest;
    int strongestAlpha = -1;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() > strongestAlpha) {
                strongest = pixel;
                strongestAlpha = pixel.alpha();
            }
        }
    }
    return strongest;
}

} // namespace

class ThemeManagerTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void modesPersistAndResolve();
    void semanticStyleSheetRendersCompletely();
    void themedWidgetsAndSurfaceFollowMode();
    void existingIconsFollowMode();
    void settingsDialogSwitchesImmediately();

private:
    QTemporaryDir m_settingsDir;
};

void ThemeManagerTest::initTestCase()
{
    QVERIFY(m_settingsDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("NeteaseCloneThemeTest"));
    QCoreApplication::setApplicationName(QStringLiteral("NeteaseCloneThemeTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    SettingsService::setThemeMode(int(ThemeMode::Light));
    ThemeManager::instance().initialize(qApp);
    QCOMPARE(ThemeManager::instance().mode(), ThemeMode::Light);
    QCOMPARE(ThemeManager::instance().resolvedMode(), ThemeMode::Light);
}

void ThemeManagerTest::modesPersistAndResolve()
{
    const QList<ThemeMode> modes = {
        ThemeMode::Dark,
        ThemeMode::Light,
        ThemeMode::FollowSystem,
    };
    for (ThemeMode mode : modes) {
        ThemeManager::instance().setMode(mode);
        QCOMPARE(ThemeManager::instance().mode(), mode);
        QCOMPARE(SettingsService::themeMode(-1), int(mode));
        if (mode == ThemeMode::FollowSystem) {
            QVERIFY(ThemeManager::instance().resolvedMode() == ThemeMode::Dark
                    || ThemeManager::instance().resolvedMode() == ThemeMode::Light);
        } else {
            QCOMPARE(ThemeManager::instance().resolvedMode(), mode);
        }
    }
}

void ThemeManagerTest::semanticStyleSheetRendersCompletely()
{
    QFile file(QStringLiteral(":/theme.qss"));
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString rendered = ThemeManager::instance().renderStyleSheet(
        QString::fromUtf8(file.readAll()));
    QVERIFY2(!rendered.contains(QLatin1Char('@')), qPrintable(rendered));
    QVERIFY(rendered.contains(ThemeManager::instance().color(ThemeColor::Accent)
                                  .name(QColor::HexRgb).toUpper()));
}

void ThemeManagerTest::themedWidgetsAndSurfaceFollowMode()
{
    QWidget widget;
    setThemedStyleSheet(&widget, QStringLiteral("QWidget{color:@textPrimary;background:@surface;}"));

    ThemeManager::instance().setMode(ThemeMode::Dark);
    QVERIFY(widget.styleSheet().contains(QStringLiteral("#E8E8E8")));
    QCOMPARE(ThemeManager::instance().color(ThemeColor::PageBackground), QColor("#0E0E14"));

    ThemeManager::instance().setMode(ThemeMode::Light);
    QVERIFY(widget.styleSheet().contains(QStringLiteral("#202126")));
    QCOMPARE(ThemeManager::instance().color(ThemeColor::PageBackground), QColor("#F7F7FA"));

    ThemeSurface surface;
    surface.resize(24, 24);
    surface.show();
    QApplication::processEvents();
    QCOMPARE(surface.grab().toImage().pixelColor(12, 12), QColor("#F7F7FA"));
}

void ThemeManagerTest::existingIconsFollowMode()
{
    const QIcon icon = makeSvgIcon(QStringLiteral(":/icons/icon-heart.svg"), 24);
    ThemeManager::instance().setMode(ThemeMode::Dark);
    const QColor dark = strongestPixel(icon.pixmap(QSize(24, 24)));
    ThemeManager::instance().setMode(ThemeMode::Light);
    const QColor light = strongestPixel(icon.pixmap(QSize(24, 24)));

    QCOMPARE(dark.rgb(), QColor("#9A9AA5").rgb());
    QCOMPARE(light.rgb(), QColor("#60636D").rgb());
    QVERIFY(dark.rgb() != light.rgb());
}

void ThemeManagerTest::settingsDialogSwitchesImmediately()
{
    SettingsDialog dialog(nullptr, nullptr, nullptr);
    auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("themeModeCombo"));
    QVERIFY(combo);
    QCOMPARE(combo->count(), 3);
    QCOMPARE(combo->itemText(0), QStringLiteral("跟随系统"));
    QCOMPARE(combo->itemText(1), QStringLiteral("深色"));
    QCOMPARE(combo->itemText(2), QStringLiteral("浅色"));

    const int darkIndex = combo->findData(int(ThemeMode::Dark));
    QVERIFY(darkIndex >= 0);
    combo->setCurrentIndex(darkIndex);
    QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, darkIndex)));
    QCOMPARE(ThemeManager::instance().mode(), ThemeMode::Dark);
    QCOMPARE(SettingsService::themeMode(-1), int(ThemeMode::Dark));
}

QTEST_MAIN(ThemeManagerTest)
#include "tst_thememanager.moc"
