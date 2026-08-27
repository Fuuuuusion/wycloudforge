#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPlainTextEdit;

namespace core {
class PlaylistController;
}

namespace ui {

class PlaylistEditDialog : public QDialog
{
    Q_OBJECT
public:
    PlaylistEditDialog(core::PlaylistController *playlists, int playlistId,
                       const QString &name, const QString &description,
                       const QString &coverPath, QWidget *parent = nullptr);

    QString coverPath() const { return m_coverPath; }

protected:
    void accept() override;

private:
    void chooseCover();

    core::PlaylistController *m_playlists = nullptr;
    int m_playlistId = -1;
    QLineEdit *m_name = nullptr;
    QPlainTextEdit *m_desc = nullptr;
    QLabel *m_coverLabel = nullptr;
    QString m_coverPath;
};

} // namespace ui
