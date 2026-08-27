#pragma once

#include "core/LrcParser.h"
#include "core/Song.h"

#include <QWidget>

class QLabel;
class QPushButton;

namespace ui {

class LyricWidget;

class PlayingPage : public QWidget
{
    Q_OBJECT
public:
    explicit PlayingPage(QWidget *parent = nullptr);

    void setSong(const core::Song &song, const QPixmap &cover);
    void setLyrics(const QList<core::LyricLine> &lines);
    void setPosition(qint64 ms);
    void setLyricFontSize(int px);
    core::Song currentSong() const { return m_song; }

signals:
    void seekRequested(qint64 ms);
    void editLyricsRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateBackdrop();

    QLabel *m_backdrop = nullptr;
    QLabel *m_cover = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_artist = nullptr;
    LyricWidget *m_lyric = nullptr;
    QPushButton *m_editBtn = nullptr;
    core::Song m_song;
    QPixmap m_coverPix;
};

} // namespace ui
