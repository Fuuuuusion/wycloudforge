#include "MainWindow.h"

#include "core/LibraryService.h"
#include "core/SettingsService.h"

#include <QApplication>
#include <QFile>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("NeteaseClone"));
    QApplication::setApplicationName(QStringLiteral("NeteaseClone"));
    QApplication::setApplicationDisplayName(QStringLiteral("仿网易云播放器"));

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
        QTimer::singleShot(900, &window, [&window, path] {
            window.grab().save(path);
            QApplication::quit();
        });
    } else if (args.contains(QStringLiteral("--smoke"))) {
        QTimer::singleShot(1500, &app, &QApplication::quit);
    }

    return app.exec();
}
