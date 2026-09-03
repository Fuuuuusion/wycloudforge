#include "PlayerService.h"

#include "core/LibraryService.h"
#include "core/MusicSourceRegistry.h"

#include <algorithm>
#include <numeric>

#include <QFileInfo>
#include <QDir>
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

    connect(&m_player, &QMediaPlayer::playbackStateChanged,
            this, &PlayerService::handlePlaybackStateChanged);
    connect(&m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        if (pos > 0)
            m_lastKnownPositionMs = pos;
        emit positionChanged(pos);
        maybeCacheCurrent(pos);
        if (m_playbackIntent && m_wasPlaying && isNearMediaEnd(2500)) {
            m_endStallToken = m_loadToken;
            m_endStallTimer.start();
        } else if (pos > 0 || !m_wasPlaying) {
            // FFmpeg may reset position to zero while draining an online stream.
            // Preserve an already armed end watchdog in that terminal transition.
            m_endStallTimer.stop();
            m_endStallToken = -1;
        }
    });
    connect(&m_player, &QMediaPlayer::durationChanged, this, [this](qint64 dur) {
        if (dur > 0)
            m_lastKnownDurationMs = dur;
        emit durationChanged(dur);
        applyPendingResume();
    });
    connect(&m_player, &QMediaPlayer::mediaStatusChanged,
            this, &PlayerService::handleMediaStatusChanged);
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &err) {
        if (!retryInvalidDownloadedFile() && !retryInvalidCache()
            && !retryInvalidRemoteSource())
            handleUnrecoverableError(err.isEmpty() ? QStringLiteral("播放失败") : err);
    });
    m_endStallTimer.setSingleShot(true);
    m_endStallTimer.setInterval(1200);
    connect(&m_endStallTimer, &QTimer::timeout, this, [this] {
        if (m_endStallToken != m_loadToken || !m_playbackIntent || m_internalStop)
            return;
        if (m_wasPlaying && isNearMediaEnd(1800))
            scheduleAdvanceAfterEndOfMedia();
    });
}

void PlayerService::handlePlaybackStateChanged(QMediaPlayer::PlaybackState state)
{
    if (state == QMediaPlayer::PlayingState) {
        m_pendingAutoPlay = false;
        // Treat an observed PlayingState as authoritative. This keeps natural-end
        // handling independent from whichever asynchronous source path called play().
        m_playbackIntent = true;
        m_wasPlaying = true;
        m_consecutiveFailures = 0;
    } else if (state == QMediaPlayer::PausedState) {
        // Some FFmpeg streams briefly report PausedState while draining at the
        // end. An explicit pause has already cleared m_playbackIntent.
        if (!m_playbackIntent) {
            m_wasPlaying = false;
            m_endStallTimer.stop();
            m_endStallToken = -1;
        } else if (m_wasPlaying && isNearMediaEnd(2500)) {
            m_endStallToken = m_loadToken;
            m_endStallTimer.start();
        }
    } else if (state == QMediaPlayer::StoppedState && m_wasPlaying
               && !m_internalStop && m_playbackIntent) {
        // Do not require duration()/position() here. FFmpeg can clear both before
        // announcing StoppedState for an exhausted HTTP stream. Internal source
        // changes clear m_wasPlaying before stopping the old backend.
        scheduleAdvanceAfterEndOfMedia();
    }
    emit playingChanged(state == QMediaPlayer::PlayingState);
}

void PlayerService::handleMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    if ((status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)
        && m_pendingAutoPlay) {
        applyPendingResume();
        m_player.play();
    }
    if (status == QMediaPlayer::EndOfMedia && m_playbackIntent && m_wasPlaying) {
        // EndOfMedia may repeat in one event-loop turn; the load token deduplicates
        // it and rejects events belonging to a source that the user already changed.
        scheduleAdvanceAfterEndOfMedia();
    }
    if (status == QMediaPlayer::InvalidMedia
        && !retryInvalidDownloadedFile() && !retryInvalidCache()
        && !retryInvalidRemoteSource()) {
        handleUnrecoverableError(QStringLiteral("媒体无法解码或播放源已失效"));
    }
}

void PlayerService::setPlaylist(const QList<Song> &songs, int startIndex)
{
    m_pendingResumePositionMs = -1;
    m_loadedSongIdentity.clear();
    m_lastKnownPositionMs = 0;
    m_lastKnownDurationMs = 0;
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

bool PlayerService::removeAt(int index)
{
    if (index < 0 || index >= m_playlist.size())
        return false;
    const bool removingCurrent = index == m_index;
    const bool resumePlayback = m_playbackIntent || isPlaying() || m_pendingAutoPlay;
    m_playlist.removeAt(index);
    if (m_playlist.isEmpty()) {
        clearPlaylist();
        return true;
    }

    if (!removingCurrent) {
        if (index < m_index)
            --m_index;
        buildShuffleOrder();
        alignShuffleToCurrent();
        m_history.clear();
        return true;
    }

    m_index = qMin(index, m_playlist.size() - 1);
    buildShuffleOrder();
    alignShuffleToCurrent();
    m_history.clear();
    loadCurrent(resumePlayback);
    return true;
}

void PlayerService::clearPlaylist()
{
    ++m_loadToken;
    m_endStallTimer.stop();
    m_endStallToken = -1;
    m_endOfMediaToken = -1;
    m_pendingAutoPlay = false;
    m_internalStop = true;
    m_player.stop();
    m_player.setSource(QUrl());
    m_internalStop = false;
    m_playlist.clear();
    m_history.clear();
    m_shuffleOrder.clear();
    m_shufflePos = 0;
    m_index = -1;
    m_currentUrl.clear();
    m_cacheSaved = false;
    m_usingCachedSource = false;
    m_usingDownloadedSource = false;
    m_cacheRetryAttempted = false;
    m_urlRetryAttempted = false;
    m_pendingResumePositionMs = -1;
    m_lastKnownPositionMs = 0;
    m_lastKnownDurationMs = 0;
    m_loadedSongIdentity.clear();
    m_wasPlaying = false;
    m_playbackIntent = false;
    m_consecutiveFailures = 0;
    emit songChanged(Song(), -1);
    emit positionChanged(0);
    emit durationChanged(0);
}

void PlayerService::playIndex(int index)
{
    if (index < 0 || index >= m_playlist.size())
        return;
    m_history.append(m_index >= 0 ? m_index : 0);
    m_pendingResumePositionMs = -1;
    m_index = index;
    alignShuffleToCurrent();
    loadCurrent(true);
}

void PlayerService::playPause()
{
    if (m_player.playbackState() == QMediaPlayer::PlayingState)
        pause();
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
        m_playbackIntent = false;
        return;
    }
    // 线上歌曲的播放地址可能仍在请求中;保留自动播放意图,待新媒体加载完成后再启动。
    m_pendingAutoPlay = true;
    m_playbackIntent = true;
    m_player.play();
}

void PlayerService::pause()
{
    m_pendingAutoPlay = false;
    m_playbackIntent = false;
    m_endStallTimer.stop();
    m_endStallToken = -1;
    m_wasPlaying = false;
    m_player.pause();
}

void PlayerService::next()
{
    if (m_playlist.isEmpty())
        return;
    m_history.append(m_index);
    m_pendingResumePositionMs = -1;
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
        m_pendingResumePositionMs = -1;
        loadCurrent(true);
        return;
    }
    m_index = (m_index - 1 + m_playlist.size()) % m_playlist.size();
    m_pendingResumePositionMs = -1;
    loadCurrent(true);
}

void PlayerService::seek(qint64 ms)
{
    m_lastKnownPositionMs = qMax<qint64>(0, ms);
    m_endStallTimer.stop();
    m_endStallToken = -1;
    m_player.setPosition(ms);
}

PlayerService::FileReleaseState PlayerService::releaseFileForRemoval(const QString &path)
{
    FileReleaseState state;
    if (path.isEmpty() || m_index < 0 || m_index >= m_playlist.size())
        return state;
    const QString currentPath = m_player.source().isLocalFile()
        ? QFileInfo(m_player.source().toLocalFile()).absoluteFilePath() : QString();
    if (currentPath.isEmpty()
        || QDir::cleanPath(currentPath).compare(
               QDir::cleanPath(QFileInfo(path).absoluteFilePath()), Qt::CaseInsensitive) != 0) {
        return state;
    }
    state.detached = true;
    state.wasPlaying = m_playbackIntent || isPlaying() || m_pendingAutoPlay;
    state.positionMs = qMax(m_player.position(), m_lastKnownPositionMs);
    state.songId = currentSong().id;
    ++m_loadToken;
    m_endStallTimer.stop();
    m_endStallToken = -1;
    m_pendingAutoPlay = false;
    m_internalStop = true;
    m_player.stop();
    m_player.setSource(QUrl());
    m_internalStop = false;
    return state;
}

void PlayerService::synchronizeSong(const Song &song)
{
    synchronizeSong(song, FileReleaseState{});
}

void PlayerService::synchronizeSong(const Song &song, const FileReleaseState &resume)
{
    bool currentUpdated = false;
    for (int i = 0; i < m_playlist.size(); ++i) {
        if (m_playlist.at(i).id != song.id)
            continue;
        m_playlist[i] = song;
        currentUpdated = currentUpdated || i == m_index;
    }
    if (!currentUpdated)
        return;
    if (resume.detached && resume.songId == song.id) {
        m_pendingResumePositionMs = resume.positionMs;
        loadCurrent(resume.wasPlaying);
    } else {
        emit songChanged(m_playlist[m_index], m_index);
    }
}

bool PlayerService::removeSongById(qint64 songId)
{
    if (songId <= 0 || m_playlist.isEmpty())
        return false;
    const bool resumePlayback = m_playbackIntent || isPlaying() || m_pendingAutoPlay;
    const int oldIndex = m_index;
    bool removedCurrent = false;
    bool removed = false;
    for (int i = m_playlist.size() - 1; i >= 0; --i) {
        if (m_playlist.at(i).id != songId)
            continue;
        removedCurrent = removedCurrent || i == oldIndex;
        m_playlist.removeAt(i);
        removed = true;
        if (i < m_index)
            --m_index;
    }
    if (!removed)
        return false;
    if (m_playlist.isEmpty()) {
        clearPlaylist();
        return true;
    }
    m_index = qBound(0, m_index, m_playlist.size() - 1);
    buildShuffleOrder();
    alignShuffleToCurrent();
    m_history.clear();
    if (removedCurrent)
        loadCurrent(resumePlayback);
    return true;
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
    Song &pendingSong = m_playlist[m_index];
    if (pendingSong.isOnline() && pendingSong.id <= 0
        && pendingSong.hasRemoteIdentity() && m_lib) {
        const qint64 storedId = m_lib->upsertOnlineSong(pendingSong);
        if (storedId > 0) {
            const Song stored = m_lib->songById(storedId);
            if (stored.id > 0)
                pendingSong = stored;
            else
                pendingSong.id = storedId;
        }
    }
    const Song &song = m_playlist[m_index];
    const QString identity = song.selectionIdentity();
    if (identity != m_loadedSongIdentity || m_pendingResumePositionMs < 0) {
        m_loadedSongIdentity = identity;
        m_lastKnownPositionMs = 0;
        m_lastKnownDurationMs = 0;
    }
    ++m_loadToken;
    m_endStallTimer.stop();
    m_endStallToken = -1;
    m_endOfMediaToken = -1;
    if (resetCacheRetry)
        m_cacheRetryAttempted = false;
    if (resetCacheRetry)
        m_urlRetryAttempted = false;
    m_currentUrl.clear();
    m_pendingAutoPlay = autoPlay;
    m_playbackIntent = autoPlay;
    m_usingCachedSource = false;
    m_usingDownloadedSource = false;
    // Clear natural-end eligibility before stopping the old backend. Some Qt
    // backends deliver StoppedState after stop()/setSource() has returned.
    m_wasPlaying = false;
    m_internalStop = true;
    m_player.stop();
    m_player.setSource(QUrl());
    m_internalStop = false;

    if (song.isOnline()) {
        MusicSource *source = sourceFor(song);
        // 下载文件和播放缓存都不包含歌曲元数据，在线歌曲的封面必须单独
        // 缓存并写回 songs.cover_path，才能在重启后继续显示。
        saveCover(song);
        m_cacheSaved = false;
        QString downloaded = song.downloadPath;
        if (downloaded.isEmpty() && m_lib)
            downloaded = m_lib->downloadPathFor(song.id);
        const bool skipDownloaded = m_skipDownloadedOnce;
        m_skipDownloadedOnce = false;
        if (allowCached && !skipDownloaded && !downloaded.isEmpty() && QFileInfo::exists(downloaded)
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
        if (!source || !song.hasRemoteIdentity()) {
            m_pendingAutoPlay = false;
            emit songChanged(song, m_index);
            handleUnrecoverableError(QStringLiteral("歌曲没有可用的缓存或在线播放源"));
            return;
        }
        const int token = m_loadToken;
        source->songUrls(QList<Song>{ song },
                           [this, token](const QJsonArray &arr) {
                               if (token != m_loadToken)
                                   return;
                               QString url;
                               QString addressError;
                               if (!arr.isEmpty())
                                   url = arr.first().toObject().value(QStringLiteral("url")).toString();
                               if (!arr.isEmpty())
                                   addressError = arr.first().toObject().value(QStringLiteral("error")).toString();
                               if (url.isEmpty()) {
                                   if (!m_urlRetryAttempted) {
                                       m_urlRetryAttempted = true;
                                       const bool autoPlay = m_pendingAutoPlay;
                                       QTimer::singleShot(0, this, [this, autoPlay] {
                                           loadCurrent(autoPlay, false, false);
                                       });
                                       return;
                                   }
                                   m_pendingAutoPlay = false;
                                   handleUnrecoverableError(addressError.isEmpty()
                                       ? QStringLiteral("歌曲不可用(可能受版权/VIP、地区或 DRM 限制)")
                                       : addressError);
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
                               if (!m_urlRetryAttempted) {
                                   m_urlRetryAttempted = true;
                                   const bool autoPlay = m_pendingAutoPlay;
                                   QTimer::singleShot(0, this, [this, autoPlay] {
                                       loadCurrent(autoPlay, false, false);
                                   });
                                   return;
                               }
                               m_pendingAutoPlay = false;
                               handleUnrecoverableError(
                                   QStringLiteral("获取播放地址失败:%1").arg(err));
                           });
        emit songChanged(song, m_index);
        return;
    }

    if (song.filePath.isEmpty() || !QFileInfo::exists(song.filePath)) {
        m_pendingAutoPlay = false;
        emit songChanged(song, m_index);
        handleUnrecoverableError(QStringLiteral("本地音频文件不存在"));
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
    const bool autoPlay = m_playbackIntent || m_pendingAutoPlay
        || m_player.playbackState() == QMediaPlayer::PlayingState;
    m_pendingResumePositionMs = qMax(m_player.position(), m_lastKnownPositionMs);
    m_usingDownloadedSource = false;
    m_skipDownloadedOnce = true;
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
    const bool autoPlay = m_playbackIntent || m_pendingAutoPlay
        || m_player.playbackState() == QMediaPlayer::PlayingState;
    m_pendingResumePositionMs = qMax(m_player.position(), m_lastKnownPositionMs);
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

bool PlayerService::retryInvalidRemoteSource()
{
    if (m_urlRetryAttempted || m_currentUrl.isEmpty() || m_usingCachedSource
        || m_usingDownloadedSource || m_index < 0 || m_index >= m_playlist.size()
        || !m_playlist[m_index].hasRemoteIdentity())
        return false;
    m_urlRetryAttempted = true;
    const bool autoPlay = m_playbackIntent || m_pendingAutoPlay
        || m_player.playbackState() == QMediaPlayer::PlayingState;
    m_pendingResumePositionMs = qMax(m_player.position(), m_lastKnownPositionMs);
    QTimer::singleShot(0, this, [this, autoPlay] {
        loadCurrent(autoPlay, false, false);
    });
    return true;
}

void PlayerService::maybeCacheCurrent(qint64 positionMs)
{
    if (m_cacheSaved || !m_lib || m_currentUrl.isEmpty())
        return;
    const Song song = currentSong();
    if (!song.isOnline())
        return;
    MusicSource *source = sourceFor(song);
    if (!source)
        return;
    const qint64 dur = m_player.duration();
    if (positionMs < 30000 && (dur <= 0 || positionMs < dur * 0.3))
        return;
    m_cacheSaved = true;
    const QString path = m_lib->cacheFilePathFor(song);
    const QUrl url(m_currentUrl);
    const qint64 id = song.id;
    source->downloadToFile(url, path, [this, id, path](bool ok) {
        if (ok && m_lib)
            m_lib->setSongCached(id, path, QFileInfo(path).size());
    });
}

void PlayerService::saveCover(const Song &song)
{
    if (!m_lib || !song.isOnline() || song.id <= 0)
        return;
    MusicSource *source = sourceFor(song);
    if (!source)
        return;

    const Song stored = m_lib->songById(song.id);
    const Song target = stored.id > 0 ? stored : song;
    if (!target.coverPath.isEmpty() && QFileInfo::exists(target.coverPath)
        && QFileInfo(target.coverPath).size() > 0)
        return;

    const QString coverUrl = target.coverUrl.isEmpty() ? song.coverUrl : target.coverUrl;
    if (coverUrl.isEmpty())
        return;
    const QString path = m_lib->songCoverCachePath(target);
    if (path.isEmpty())
        return;
    if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
        m_lib->setSongCoverPath(target.id, path);
        return;
    }

    if (m_coverSaveInFlight.contains(target.id))
        return;
    m_coverSaveInFlight.insert(target.id);
    const qint64 id = target.id;
    source->downloadToFile(QUrl(coverUrl), path, [this, id, path](bool ok) {
        m_coverSaveInFlight.remove(id);
        if (ok && m_lib)
            m_lib->setSongCoverPath(id, path);
    });
}

MusicSource *PlayerService::sourceFor(const Song &song) const
{
    if (m_registry) {
        if (MusicSource *source = m_registry->sourceFor(song))
            return source;
    }
    return m_source;
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
        // Reload the same logical song. Online URLs are short-lived, so repeating
        // must resolve the source again instead of replaying an exhausted stream.
        m_pendingResumePositionMs = -1;
        m_lastKnownPositionMs = 0;
        loadCurrent(true);
        return;
    }

    // Order 是列表循环：按原列表顺序切到下一首，到末尾回到第一首。
    // Shuffle 则由 next() 使用已经与当前歌曲对齐的随机序列。
    next();
}

void PlayerService::scheduleAdvanceAfterEndOfMedia()
{
    const int token = m_loadToken;
    if (m_endOfMediaToken == token)
        return;
    m_endOfMediaToken = token;
    // Give error/status signals from the same backend transition one short turn
    // to run first. Their retry/skip path changes the load token and cancels this.
    QTimer::singleShot(25, this, [this, token] {
        if (m_endOfMediaToken != token)
            return;
        m_endOfMediaToken = -1;
        if (token != m_loadToken || !m_playbackIntent || !m_wasPlaying || m_internalStop)
            return;
        // Invalid media is advanced by handleUnrecoverableError(), which also
        // limits consecutive failures and avoids retrying one bad song forever.
        if (m_player.mediaStatus() == QMediaPlayer::InvalidMedia)
            return;
        m_wasPlaying = false;
        advanceAfterEndOfMedia();
    });
}

bool PlayerService::isNearMediaEnd(qint64 toleranceMs) const
{
    qint64 durationMs = m_lastKnownDurationMs;
    if (durationMs <= 0)
        durationMs = m_player.duration();
    if (durationMs <= 0)
        durationMs = currentSong().durationMs;
    if (durationMs <= 0)
        return false;
    const qint64 positionMs = qMax(m_player.position(), m_lastKnownPositionMs);
    return positionMs >= qMax<qint64>(0, durationMs - toleranceMs);
}

void PlayerService::handleUnrecoverableError(const QString &message)
{
    emit errorOccurred(message);
    m_pendingAutoPlay = false;
    if (m_mode == RepeatOne || m_playlist.size() <= 1) {
        m_playbackIntent = false;
        return;
    }
    if (++m_consecutiveFailures >= m_playlist.size()) {
        m_playbackIntent = false;
        emit errorOccurred(QStringLiteral("播放列表中没有可继续播放的歌曲"));
        return;
    }
    const int token = m_loadToken;
    QTimer::singleShot(0, this, [this, token] {
        if (token == m_loadToken)
            next();
    });
}

void PlayerService::applyPendingResume()
{
    if (m_pendingResumePositionMs <= 0 || m_player.duration() <= 0)
        return;
    const qint64 resume = qMin(m_pendingResumePositionMs,
                               qMax<qint64>(0, m_player.duration() - 500));
    m_pendingResumePositionMs = -1;
    if (resume > 0)
        m_player.setPosition(resume);
}

} // namespace core
