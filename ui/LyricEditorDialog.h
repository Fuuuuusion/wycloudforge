#pragma once

#include "core/Song.h"

#include <QDialog>

class QPlainTextEdit;

namespace ui {

class LyricEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LyricEditorDialog(const core::Song &song, QWidget *parent = nullptr);

    // 便捷入口:成功保存返回 true
    static bool editForSong(QWidget *parent, const core::Song &song);

private:
    core::Song m_song;
    QPlainTextEdit *m_edit = nullptr;
};

} // namespace ui

