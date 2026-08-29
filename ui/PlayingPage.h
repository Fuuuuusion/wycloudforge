#pragma once

#include "core/LrcParser.h"
#include "core/Song.h"

#include <QWidget>

namespace core {
class MusicSource;
class MusicSourceRegistry;
class LibraryService;
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
    void setSourceRegistry(core::MusicSourceRegistry *registry);
    void setLibrary(core::LibraryService *library) { m_library = library; }
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
    core::MusicSourceRegistry *m_registry = nullptr;
    core::LibraryService *m_library = nullptr;
    QList<core::LyricLine> m_lrc;
    QList<core::LyricLine> m_tlyrc;
    QList<core::LyricLine> m_romalrc;
    int m_lyricMode = 0;
    quint64 m_lyricRequestGeneration = 0;
    core::Song m_song;
    QPixmap m_coverPix;
};

} // namespace ui
