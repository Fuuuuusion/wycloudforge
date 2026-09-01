#include "MainWindow.h"

#include "core/LibraryService.h"
#include "core/SearchCache.h"
#include "core/SettingsService.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QtCore/qtenvironmentvariables.h>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

#ifdef Q_OS_WIN
struct WindowSearch
{
    DWORD processId = 0;
    HWND window = nullptr;
};

BOOL CALLBACK findProcessWindow(HWND window, LPARAM parameter)
{
    auto *search = reinterpret_cast<WindowSearch *>(parameter);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != search->processId || GetWindow(window, GW_OWNER) != nullptr
        || !IsWindowVisible(window) || GetWindowTextLengthW(window) <= 0)
        return TRUE;
    search->window = window;
    return FALSE;
}

void activateExistingInstance(qint64 processId)
{
    if (processId <= 0)
        return;
    WindowSearch search;
    search.processId = DWORD(processId);
    EnumWindows(findProcessWindow, reinterpret_cast<LPARAM>(&search));
    if (!search.window)
        return;
    ShowWindowAsync(search.window, IsIconic(search.window) ? SW_RESTORE : SW_SHOW);
    BringWindowToTop(search.window);
    SetForegroundWindow(search.window);
}
#else
void activateExistingInstance(qint64)
{
}
#endif

} // namespace

int main(int argc, char *argv[])
{
    // 固定使用 FFmpeg 多媒体后端:WMF 后端不支持 ogg/vorbis,而 FFmpeg 兼容 mp3/flac/wav/m4a/aac/ogg
    qputenv("QT_MEDIA_BACKEND", QByteArrayLiteral("ffmpeg"));

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("NeteaseClone"));
    QApplication::setApplicationName(QStringLiteral("NeteaseClone"));
    QApplication::setApplicationDisplayName(QStringLiteral("仿网易云播放器"));

    // 尽可能早地完成单实例判断。第二次启动无需加载主题、字体和播放器界面，
    // 只负责唤醒已有窗口后立即退出。
    const QStringList args = app.arguments();
    const bool isolatedTestRun = args.contains(QStringLiteral("--db"));
    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appDataDir);
    if (isolatedTestRun) {
        QString settingsDir = appDataDir;
        const int settingsIndex = args.indexOf(QStringLiteral("--settings-dir"));
        if (settingsIndex >= 0 && settingsIndex + 1 < args.size())
            settingsDir = QDir::cleanPath(args.at(settingsIndex + 1));
        QDir().mkpath(settingsDir);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir);
        core::SettingsService::setRecommendCachePathOverride(
            settingsDir + QStringLiteral("/recommend.json"));
        core::SearchCache::setDefaultRootPathOverride(
            settingsDir + QStringLiteral("/search-v1"));
    }
    QLockFile instanceLock(appDataDir + QStringLiteral("/NeteaseClone.lock"));
    if (!isolatedTestRun && !instanceLock.tryLock(100)) {
        qint64 ownerProcessId = 0;
        QString ownerHost;
        QString ownerApplication;
        if (instanceLock.getLockInfo(&ownerProcessId, &ownerHost, &ownerApplication))
            activateExistingInstance(ownerProcessId);
        return 0;
    }

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

    QStringList folders = isolatedTestRun ? QStringList()
                                          : core::SettingsService::musicFolders();
    int startPage = -1;
    bool hasFolderArg = false;
    QSize requestedSize;
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
        else if (args[i] == QLatin1String("--size")) {
            const QStringList dimensions = args[i + 1].toLower().split(QLatin1Char('x'));
            if (dimensions.size() == 2) {
                const int width = dimensions.at(0).toInt();
                const int height = dimensions.at(1).toInt();
                if (width > 0 && height > 0)
                    requestedSize = QSize(width, height);
            }
        }
    }
    if (hasFolderArg || isolatedTestRun)
        core::SettingsService::setFoldersOverride(folders);
    core::SettingsService::setMusicFolders(folders);

    MainWindow window;
    if (requestedSize.isValid())
        window.resize(requestedSize);
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
