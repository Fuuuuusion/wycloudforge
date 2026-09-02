#pragma once

#include <QWidget>

class QPushButton;

namespace ui {

class AccountSettingsButton;

class SidebarFooter : public QWidget
{
    Q_OBJECT
public:
    explicit SidebarFooter(QWidget *parent = nullptr);

    AccountSettingsButton *settingsButton() const { return m_settingsButton; }
    QPushButton *refreshButton() const { return m_refreshButton; }
    QPushButton *downloadButton() const { return m_downloadButton; }
    void setDownloadStatus(bool downloading, bool queued, bool hasDownloads);

signals:
    void settingsClicked();
    void refreshClicked();
    void downloadClicked();

private:
    AccountSettingsButton *m_settingsButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_downloadButton = nullptr;
};

} // namespace ui
