#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;

namespace core {
class ApiService;
class LibraryService;
class NeteaseApiClient;
}

namespace ui {

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(core::ApiService *apiService, core::NeteaseApiClient *apiClient,
                            core::LibraryService *library, QWidget *parent = nullptr);

    QStringList folders() const;
    int lyricFontSize() const;

signals:
    void rescanRequested();
    void databaseReloadRequested();

private:
    void updateCacheStats();

    QListWidget *m_folderList = nullptr;
    QSlider *m_fontSlider = nullptr;
    QLabel *m_fontValue = nullptr;
    QLineEdit *m_apiBaseEdit = nullptr;
    QCheckBox *m_autoStartCheck = nullptr;
    QSpinBox *m_cacheCountSpin = nullptr;
    QSpinBox *m_cacheMBSpin = nullptr;
    QLabel *m_onlineStatus = nullptr;
    QLabel *m_cacheStats = nullptr;
    QLabel *m_loginLabel = nullptr;
    core::ApiService *m_apiService = nullptr;
    core::NeteaseApiClient *m_apiClient = nullptr;
    core::LibraryService *m_library = nullptr;
};

} // namespace ui
