#pragma once

#include "core/LrcParser.h"
#include "core/Song.h"

#include <QWidget>

namespace core {
class MusicSource;
}

class QButtonGroup;
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
    void setSourceProvider(core::MusicSource *source);
    void loadLyricsFor(const core::Song &song);
    void setPosition(qint64 ms);
    void setLyricFontSize(int px);
    core::Song currentSong() const { return m_song; }

signals:
    void seekRequested(qint64 ms);
    void editLyricsRequested();

private:
    void applyLyricMode();

    QLabel *m_cover = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_artist = nullptr;
    LyricWidget *m_lyric = nullptr;
    QPushButton *m_editBtn = nullptr;
    QButtonGroup *m_modeGroup = nullptr;
    core::MusicSource *m_source = nullptr;
    QList<core::LyricLine> m_lrc;
    QList<core::LyricLine> m_tlyrc;
    QList<core::LyricLine> m_romalrc;
    int m_lyricMode = 0;
    core::Song m_song;
    QPixmap m_coverPix;
};

} // namespace ui
