#pragma once

#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;
class QSlider;

namespace ui {

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    QStringList folders() const;
    int lyricFontSize() const;

signals:
    void rescanRequested();

private:
    QListWidget *m_folderList = nullptr;
    QSlider *m_fontSlider = nullptr;
    QLabel *m_fontValue = nullptr;
};

} // namespace ui

