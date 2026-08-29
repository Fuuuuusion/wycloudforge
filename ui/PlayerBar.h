#pragma once

#include <QElapsedTimer>
#include <QPixmap>
#include <QWidget>

#include "core/Song.h"

class QLabel;
class QPaintEvent;
class QPushButton;
class ProgressSlider;

namespace ui {

class PlayerBar : public QWidget
{
    Q_OBJECT
public:
    explicit PlayerBar(QWidget *parent = nullptr);

    void setSong(const core::Song &song, bool favorite);
    void setPlaybackError(const QString &message);
    void setPlaying(bool playing);
    void setPosition(qint64 ms);
    void setDuration(qint64 ms);
    void setVolume(int volume);
    void setMuted(bool muted);
    void setMode(int mode);
    // 设定"背后内容源"(通常为内容区 body),用于抓取真实像素做毛玻璃
    void setBackdropSource(QWidget *widget);

signals:
    void playPauseClicked();
    void prevClicked();
    void nextClicked();
    void modeClicked();
    void seekRequested(qint64 ms);
    void volumeChanged(int volume);
    void muteToggled(bool muted);
    void heartToggled(bool favorite);
    void downloadRequested();
    void lyricsClicked();
    void playlistClicked();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updatePlayIcon();
    void updateVolumeIcon();
    void updateTimeLabel();

    QLabel *m_cover = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_artist = nullptr;
    QLabel *m_sourceBadge = nullptr;
    QLabel *m_timeCur = nullptr;
    QLabel *m_timeTotal = nullptr;
    QPushButton *m_heartBtn = nullptr;
    QPushButton *m_downloadBtn = nullptr;
    QPushButton *m_modeBtn = nullptr;
    QPushButton *m_prevBtn = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QPushButton *m_muteBtn = nullptr;
    QPushButton *m_lyricsBtn = nullptr;
    QPushButton *m_queueBtn = nullptr;
    ProgressSlider *m_progress = nullptr;
    ProgressSlider *m_volume = nullptr;

    core::Song m_song;
    bool m_playing = false;
    bool m_favorite = false;
    bool m_muted = false;
    int m_mode = 0;
    int m_volumeValue = 70;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    QElapsedTimer m_clock;
    QPixmap m_backdrop;
    bool m_backdropValid = false;
    qint64 m_backdropMs = 0;
    QWidget *m_backdropSource = nullptr;
};

} // namespace ui
