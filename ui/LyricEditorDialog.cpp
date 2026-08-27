#include "LyricEditorDialog.h"

#include "core/LyricsLoader.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {

LyricEditorDialog::LyricEditorDialog(const core::Song &song, QWidget *parent)
    : QDialog(parent)
    , m_song(song)
{
    setWindowTitle(QStringLiteral("编辑歌词"));
    resize(520, 480);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(10);

    auto *hint = new QLabel(QStringLiteral("保存后写入:%1")
                                .arg(core::LyricsLoader::sidecarPathFor(song.filePath)), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#999;font-size:12px;"));
    layout->addWidget(hint);

    m_edit = new QPlainTextEdit(this);
    m_edit->setPlaceholderText(QStringLiteral("[00:00.00]第一句歌词\n[00:04.50]第二句歌词"));
    layout->addWidget(m_edit, 1);

    const auto lines = core::LyricsLoader::load(song);
    m_edit->setPlainText(core::LrcParser::toLrc(lines));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

bool LyricEditorDialog::editForSong(QWidget *parent, const core::Song &song)
{
    LyricEditorDialog dlg(song, parent);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    return core::LyricsLoader::saveSidecar(song, dlg.m_edit->toPlainText());
}

} // namespace ui

