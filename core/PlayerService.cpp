#include "PlayerService.h"

#include "core/LibraryService.h"

#include <algorithm>
#include <numeric>

#include <QFileInfo>
#include <QAudioDevice>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>

namespace core {

PlayerService::PlayerService(QObject *parent)
    : QObject(parent)
{
    m_audio.setVolume(0.7);
    m_player.setAudioOutput(&m_audio);
    ensureAudioOutput();
    connect(&m_mediaDevices, &QMediaDevices::audioOutputsChanged, this, [this] {
        ensureAudioOutput();
    });

    connect(&m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::PlayingState)
            m_pendingAutoPlay = false;
        emit playingChanged(state == QMediaPlayer::PlayingState);
    });
    connect(&m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        emit positionChanged(pos);
        maybeCacheCurrent(pos);
    });
    connect(&m_player, &QMediaPlayer::durationChanged, this, [this](qint64 dur) {
        emit durationChanged(dur);
    });
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if ((status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)
            && m_pendingAutoPlay) {
            m_player.play();
        }
        if (status == QMediaPlayer::EndOfMedia) {
            // 结束状态可能在同一轮事件循环内重复通知。延迟到播放器完成状态切换后
            // 再处理，并用加载 token 丢弃用户手动切歌产生的过期任务。
            const int token = m_loadToken;
            if (m_endOfMediaToken == token)
                return;
            m_endOfMediaToken = token;
            QTimer::singleShot(0, this, [this, token] {
                if (m_endOfMediaToken != token)
                    return;
                m_endOfMediaToken = -1;
                if (token != m_loadToken)
                    return;
                advanceAfterEndOfMedia();
            });
        }
        if (status == QMediaPlayer::InvalidMedia
            && !retryInvalidDownloadedFile() && !retryInvalidCache())
            emit errorOccurred(QStringLiteral("媒体无法解码或播放源已失效"));
    });
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &err) {
        if (!retryInvalidDownloadedFile() && !retryInvalidCache())
            emit errorOccurred(err.isEmpty() ? QStringLiteral("播放失败") : err);
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
    alignShuffleToCurrent();
    if (m_playlist.isEmpty()) {
        m_player.stop();
        m_pendingAutoPlay = false;
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
    alignShuffleToCurrent();
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
    if (!ensureAudioOutput()) {
        m_pendingAutoPlay = false;
        return;
    }
    // 线上歌曲的播放地址可能仍在请求中;保留自动播放意图,待新媒体加载完成后再启动。
    m_pendingAutoPlay = true;
    m_player.play();
}

void PlayerService::pause()
{
    m_pendingAutoPlay = false;
    m_player.pause();
}

void PlayerService::next()
{
    if (m_playlist.isEmpty())
        return;
    m_history.append(m_index);
    int nextIndex = -1;
    if (m_mode == Shuffle && m_shuffleOrder.size() == m_playlist.size()) {
        alignShuffleToCurrent();
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
    if (m_mode == Shuffle)
        alignShuffleToCurrent();
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

void PlayerService::loadCurrent(bool autoPlay, bool allowCached, bool resetCacheRetry)
{
    if (m_index < 0 || m_index >= m_playlist.size())
        return;
    const Song &song = m_playlist[m_index];
    ++m_loadToken;
    if (resetCacheRetry)
        m_cacheRetryAttempted = false;
    m_currentUrl.clear();
    m_pendingAutoPlay = autoPlay;
    m_usingCachedSource = false;
    m_usingDownloadedSource = false;
    m_player.stop();
    m_player.setSource(QUrl());

    if (song.isOnline()) {
        m_cacheSaved = false;
        QString downloaded = song.downloadPath;
        if (downloaded.isEmpty() && m_lib)
            downloaded = m_lib->downloadPathFor(song.id);
        if (allowCached && !downloaded.isEmpty() && QFileInfo::exists(downloaded)
            && QFileInfo(downloaded).size() > 0) {
            m_cacheSaved = true;
            m_usingDownloadedSource = true;
            m_playlist[m_index].downloadPath = downloaded;
            m_player.setSource(QUrl::fromLocalFile(downloaded));
            if (m_pendingAutoPlay && ensureAudioOutput())
                m_player.play();
            emit songChanged(m_playlist[m_index], m_index);
            return;
        }
        QString cached = song.cachePath;
        if (cached.isEmpty() && m_lib)
            cached = m_lib->cachePathFor(song.id);
        if (allowCached && !cached.isEmpty() && QFileInfo::exists(cached)
            && QFileInfo(cached).size() > 0) {
            m_cacheSaved = true;
            m_usingCachedSource = true;
            m_playlist[m_index].cachePath = cached;
            m_player.setSource(QUrl::fromLocalFile(cached));
            if (m_pendingAutoPlay && ensureAudioOutput())
                m_player.play();
            emit songChanged(m_playlist[m_index], m_index);
            return;
        }
        if (!m_source || song.onlineId <= 0) {
            m_pendingAutoPlay = false;
            emit songChanged(song, m_index);
            emit errorOccurred(QStringLiteral("歌曲没有可用的缓存或在线播放源"));
            return;
        }
        const int token = m_loadToken;
        m_source->songUrls({ song.onlineId },
                           [this, token](const QJsonArray &arr) {
                               if (token != m_loadToken)
                                   return;
                               QString url;
                               if (!arr.isEmpty())
                                   url = arr.first().toObject().value(QStringLiteral("url")).toString();
                               if (url.isEmpty()) {
                                   m_pendingAutoPlay = false;
                                   emit errorOccurred(QStringLiteral("歌曲不可用(可能受版权/VIP 限制)"));
                                   QTimer::singleShot(0, this, [this] { next(); });
                                   return;
                               }
                                m_currentUrl = url;
                                m_player.setSource(QUrl(url));
                                if (m_pendingAutoPlay && ensureAudioOutput())
                                    m_player.play();
                           },
                           [this, token](const QString &err) {
                               if (token != m_loadToken)
                                   return;
                               m_pendingAutoPlay = false;
                               emit errorOccurred(QStringLiteral("获取播放地址失败:%1").arg(err));
                               QTimer::singleShot(0, this, [this] { next(); });
                           });
        emit songChanged(song, m_index);
        return;
    }

    if (song.filePath.isEmpty() || !QFileInfo::exists(song.filePath)) {
        m_pendingAutoPlay = false;
        emit songChanged(song, m_index);
        emit errorOccurred(QStringLiteral("本地音频文件不存在"));
        return;
    }
    m_player.setSource(QUrl::fromLocalFile(song.filePath));
    if (m_pendingAutoPlay && ensureAudioOutput())
        m_player.play();
    emit songChanged(song, m_index);
}

bool PlayerService::ensureAudioOutput()
{
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        emit errorOccurred(QStringLiteral("未检测到可用的音频输出设备"));
        return false;
    }
    if (m_audio.device().isNull() || m_audio.device().id() != device.id())
        m_audio.setDevice(device);
    return true;
}

bool PlayerService::retryInvalidDownloadedFile()
{
    if (!m_usingDownloadedSource || m_index < 0 || m_index >= m_playlist.size())
        return false;
    const bool autoPlay = m_pendingAutoPlay || m_player.playbackState() == QMediaPlayer::PlayingState;
    const qint64 songId = m_playlist[m_index].id;
    m_usingDownloadedSource = false;
    m_playlist[m_index].downloadPath.clear();
    if (m_lib)
        m_lib->removeSongDownload(songId);
    QTimer::singleShot(0, this, [this, autoPlay] {
        loadCurrent(autoPlay, true, false);
    });
    return true;
}

bool PlayerService::retryInvalidCache()
{
    if (!m_usingCachedSource || m_cacheRetryAttempted || m_index < 0 || m_index >= m_playlist.size())
        return false;
    m_cacheRetryAttempted = true;
    const bool autoPlay = m_pendingAutoPlay || m_player.playbackState() == QMediaPlayer::PlayingState;
    const qint64 songId = m_playlist[m_index].id;
    m_usingCachedSource = false;
    m_playlist[m_index].cachePath.clear();
    if (m_lib)
        m_lib->invalidateSongCache(songId);
    QTimer::singleShot(0, this, [this, autoPlay] {
        loadCurrent(autoPlay, false, false);
    });
    return true;
}

void PlayerService::maybeCacheCurrent(qint64 positionMs)
{
    if (m_cacheSaved || !m_source || !m_lib || m_currentUrl.isEmpty())
        return;
    const Song song = currentSong();
    if (!song.isOnline())
        return;
    const qint64 dur = m_player.duration();
    if (positionMs < 30000 && (dur <= 0 || positionMs < dur * 0.3))
        return;
    m_cacheSaved = true;
    const QString path = m_lib->cacheFilePathFor(song);
    const QUrl url(m_currentUrl);
    const qint64 id = song.id;
    m_source->downloadToFile(url, path, [this, id, path](bool ok) {
        if (ok && m_lib)
            m_lib->setSongCached(id, path, QFileInfo(path).size());
    });
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

void PlayerService::alignShuffleToCurrent()
{
    if (m_playlist.isEmpty() || m_shuffleOrder.size() != m_playlist.size())
        return;
    const int pos = m_shuffleOrder.indexOf(m_index);
    if (pos >= 0)
        m_shufflePos = pos;
}

void PlayerService::advanceAfterEndOfMedia()
{
    if (m_playlist.isEmpty() || m_index < 0 || m_index >= m_playlist.size())
        return;

    if (m_mode == RepeatOne) {
        if (!ensureAudioOutput()) {
            m_pendingAutoPlay = false;
            return;
        }
        m_pendingAutoPlay = true;
        m_player.setPosition(0);
        m_player.play();
        return;
    }

    // Order 是列表循环：按原列表顺序切到下一首，到末尾回到第一首。
    // Shuffle 则由 next() 使用已经与当前歌曲对齐的随机序列。
    next();
}

} // namespace core
