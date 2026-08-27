#include "MainWindow.h"

#include "core/LibraryService.h"
#include "core/SettingsService.h"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("NeteaseClone"));
    QApplication::setApplicationName(QStringLiteral("NeteaseClone"));
    QApplication::setApplicationDisplayName(QStringLiteral("仿网易云播放器"));

    // Fusion 样式:与 QSS 完全兼容,避免 Windows 原生样式导致的"灰框"回退
    app.setStyle(QStringLiteral("Fusion"));
    QPalette pal;
    pal.setColor(QPalette::Window, QColor(0x0E, 0x0E, 0x14));
    pal.setColor(QPalette::WindowText, QColor(0xE8, 0xE8, 0xE8));
    pal.setColor(QPalette::Base, QColor(0x16, 0x16, 0x1E));
    pal.setColor(QPalette::AlternateBase, QColor(0x1B, 0x1B, 0x24));
    pal.setColor(QPalette::Text, QColor(0xE8, 0xE8, 0xE8));
    pal.setColor(QPalette::Button, QColor(0x1B, 0x1B, 0x24));
    pal.setColor(QPalette::ButtonText, QColor(0xE8, 0xE8, 0xE8));
    pal.setColor(QPalette::Highlight, QColor(0xEC, 0x41, 0x41));
    pal.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
    pal.setColor(QPalette::ToolTipBase, QColor(0x1B, 0x1B, 0x24));
    pal.setColor(QPalette::ToolTipText, QColor(0xE8, 0xE8, 0xE8));
    pal.setColor(QPalette::PlaceholderText, QColor(0x6E, 0x6E, 0x7A));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor(0x55, 0x55, 0x5F));
    app.setPalette(pal);

    QFont font(QStringLiteral("Microsoft YaHei UI"), 9);
    app.setFont(font);

    QFile qss(QStringLiteral(":/theme.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    const QStringList args = app.arguments();
    QStringList folders = core::SettingsService::musicFolders();
    int startPage = -1;
    bool hasFolderArg = false;
    for (int i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == QLatin1String("--folder") && !folders.contains(args[i + 1])) {
            folders.append(args[i + 1]);
            hasFolderArg = true;
        }
        else if (args[i] == QLatin1String("--page"))
            startPage = args[i + 1].toInt();
        else if (args[i] == QLatin1String("--song")) {
            core::SettingsService::saveLastSong(args[i + 1], 0);
            core::SettingsService::setLastSongOverride(args[i + 1], 0);
        }
        else if (args[i] == QLatin1String("--db"))
            core::LibraryService::setDatabasePathOverride(args[i + 1]);
    }
    if (hasFolderArg)
        core::SettingsService::setFoldersOverride(folders);
    core::SettingsService::setMusicFolders(folders);

    MainWindow window;
    window.show();

    const int shotIdx = args.indexOf(QStringLiteral("--screenshot"));
    if (shotIdx >= 0 && shotIdx + 1 < args.size()) {
        const QString path = args.at(shotIdx + 1);
        if (startPage >= 0)
            QTimer::singleShot(150, &window, [&window, startPage] {
                window.openPageForTesting(startPage);
            });
        QTimer::singleShot(1800, &window, [&window, path] {
            window.grab().save(path);
            QApplication::quit();
        });
    } else if (args.contains(QStringLiteral("--smoke"))) {
        QTimer::singleShot(1500, &app, &QApplication::quit);
    }

    return app.exec();
}
