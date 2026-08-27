#include "PlayerService.h"

#include <algorithm>
#include <numeric>

#include <QRandomGenerator>
#include <QUrl>

namespace core {

PlayerService::PlayerService(QObject *parent)
    : QObject(parent)
{
    m_audio.setVolume(0.7);
    m_player.setAudioOutput(&m_audio);

    connect(&m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        emit playingChanged(state == QMediaPlayer::PlayingState);
    });
    connect(&m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        emit positionChanged(pos);
    });
    connect(&m_player, &QMediaPlayer::durationChanged, this, [this](qint64 dur) {
        emit durationChanged(dur);
    });
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            if (m_mode == RepeatOne)
                m_player.setPosition(0);
            next();
        }
    });
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &err) {
        emit errorOccurred(err);
    });
}

void PlayerService::setPlaylist(const QList<Song> &songs, int startIndex)
{
    m_playlist = songs;
    buildShuffleOrder();
    m_history.clear();

    if (startIndex < 0 && m_index >= 0 && m_index < m_playlist.size()) {
        startIndex = m_index;
    }
    m_index = qBound(0, startIndex, qMax(0, m_playlist.size() - 1));
    if (m_playlist.isEmpty()) {
        m_player.stop();
        m_index = -1;
        return;
    }
    loadCurrent(false);
}

void PlayerService::playIndex(int index)
{
    if (index < 0 || index >= m_playlist.size())
        return;
    m_history.append(m_index >= 0 ? m_index : 0);
    m_index = index;
    loadCurrent(true);
}

void PlayerService::playPause()
{
    if (m_player.playbackState() == QMediaPlayer::PlayingState)
        m_player.pause();
    else
        play();
}

void PlayerService::play()
{
    if (m_playlist.isEmpty() || m_index < 0) {
        if (!m_playlist.isEmpty()) {
            m_index = 0;
            loadCurrent(true);
        }
        return;
    }
    m_player.play();
}

void PlayerService::pause()
{
    m_player.pause();
}

void PlayerService::next()
{
    if (m_playlist.isEmpty())
        return;
    m_history.append(m_index);
    int nextIndex = -1;
    if (m_mode == Shuffle && m_shuffleOrder.size() == m_playlist.size()) {
        m_shufflePos = (m_shufflePos + 1) % m_shuffleOrder.size();
        nextIndex = m_shuffleOrder[m_shufflePos];
    } else {
        nextIndex = (m_index + 1) % m_playlist.size();
    }
    m_index = nextIndex;
    loadCurrent(true);
}

void PlayerService::prev()
{
    if (m_playlist.isEmpty())
        return;
    if (m_player.position() > 3000) {
        m_player.setPosition(0);
        return;
    }
    if (!m_history.isEmpty()) {
        m_index = m_history.takeLast();
        loadCurrent(true);
        return;
    }
    m_index = (m_index - 1 + m_playlist.size()) % m_playlist.size();
    loadCurrent(true);
}

void PlayerService::seek(qint64 ms)
{
    m_player.setPosition(ms);
}

void PlayerService::setVolume(int volume)
{
    m_audio.setVolume(qBound(0, volume, 100) / 100.0);
    emit volumeChanged(volume);
}

int PlayerService::volume() const
{
    return qRound(m_audio.volume() * 100);
}

void PlayerService::setMuted(bool muted)
{
    m_audio.setMuted(muted);
    emit mutedChanged(muted);
}

bool PlayerService::muted() const
{
    return m_audio.isMuted();
}

void PlayerService::setMode(PlayMode mode)
{
    m_mode = mode;
    emit modeChanged(mode);
}

Song PlayerService::currentSong() const
{
    if (m_index < 0 || m_index >= m_playlist.size())
        return {};
    return m_playlist[m_index];
}

bool PlayerService::isPlaying() const
{
    return m_player.playbackState() == QMediaPlayer::PlayingState;
}

qint64 PlayerService::position() const
{
    return m_player.position();
}

qint64 PlayerService::duration() const
{
    return m_player.duration();
}

void PlayerService::loadCurrent(bool autoPlay)
{
    if (m_index < 0 || m_index >= m_playlist.size())
        return;
    const Song &song = m_playlist[m_index];
    m_player.setSource(QUrl::fromLocalFile(song.filePath));
    if (autoPlay)
        m_player.play();
    emit songChanged(song, m_index);
}

void PlayerService::buildShuffleOrder()
{
    m_shuffleOrder.resize(m_playlist.size());
    std::iota(m_shuffleOrder.begin(), m_shuffleOrder.end(), 0);
    for (int i = m_shuffleOrder.size() - 1; i > 0; --i) {
        const int j = int(QRandomGenerator::global()->bounded(quint32(i + 1)));
        std::swap(m_shuffleOrder[i], m_shuffleOrder[j]);
    }
    m_shufflePos = 0;
}

} // namespace core
