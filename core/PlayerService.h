#pragma once

#include "core/MusicSource.h"
#include "core/Song.h"

#include <QAudioOutput>
#include <QJsonArray>
#include <QMediaPlayer>
#include <QMediaDevices>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVector>

namespace core {

class PlayerService : public QObject
{
    Q_OBJECT
public:
    enum PlayMode { Order = 0, RepeatOne = 1, Shuffle = 2 };
    Q_ENUM(PlayMode)

    explicit PlayerService(QObject *parent = nullptr);

    struct FileReleaseState
    {
        bool detached = false;
        bool wasPlaying = false;
        qint64 positionMs = 0;
        qint64 songId = -1;
    };

    void setSourceProvider(MusicSource *source) { m_source = source; }
    void setSourceRegistry(class MusicSourceRegistry *registry) { m_registry = registry; }
    void setLibrary(class LibraryService *library) { m_lib = library; }

    void setPlaylist(const QList<Song> &songs, int startIndex = -1);
    bool removeAt(int index);
    void clearPlaylist();
    void playIndex(int index);
    void playPause();
    void play();
    void pause();
    void next();
    void prev();
    void seek(qint64 ms);
    FileReleaseState releaseFileForRemoval(const QString &path);
    void synchronizeSong(const Song &song);
    void synchronizeSong(const Song &song, const FileReleaseState &resume);
    bool removeSongById(qint64 songId);

    void setVolume(int volume);
    int volume() const;
    void setMuted(bool muted);
    bool muted() const;

    void setMode(PlayMode mode);
    PlayMode mode() const { return m_mode; }

    QList<Song> playlist() const { return m_playlist; }
    Song currentSong() const;
    int currentIndex() const { return m_index; }
    bool isPlaying() const;
    qint64 position() const;
    qint64 duration() const;

signals:
    void songChanged(const Song &song, int index);
    void playingChanged(bool playing);
    void positionChanged(qint64 ms);
    void durationChanged(qint64 ms);
    void modeChanged(PlayMode mode);
    void volumeChanged(int volume);
    void mutedChanged(bool muted);
    void errorOccurred(const QString &message);

private slots:
    void handlePlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void handleMediaStatusChanged(QMediaPlayer::MediaStatus status);

private:
    void loadCurrent(bool autoPlay, bool allowCached = true, bool resetCacheRetry = true);
    bool ensureAudioOutput();
    bool retryInvalidDownloadedFile();
    bool retryInvalidCache();
    bool retryInvalidRemoteSource();
    void advanceAfterEndOfMedia();
    void scheduleAdvanceAfterEndOfMedia();
    bool isNearMediaEnd(qint64 toleranceMs) const;
    void handleUnrecoverableError(const QString &message);
    void applyPendingResume();
    void buildShuffleOrder();
    void alignShuffleToCurrent();
    void maybeCacheCurrent(qint64 positionMs);
    void saveCover(const Song &song);
    MusicSource *sourceFor(const Song &song) const;

    QMediaPlayer m_player;
    QAudioOutput m_audio;
    QMediaDevices m_mediaDevices;
    QList<Song> m_playlist;
    int m_index = -1;
    PlayMode m_mode = Order;
    QVector<int> m_shuffleOrder;
    int m_shufflePos = 0;
    QVector<int> m_history;
    MusicSource *m_source = nullptr;
    class MusicSourceRegistry *m_registry = nullptr;
    class LibraryService *m_lib = nullptr;
    QString m_currentUrl;
    bool m_cacheSaved = false;
    int m_loadToken = 0;
    bool m_pendingAutoPlay = false;
    bool m_usingCachedSource = false;
    bool m_cacheRetryAttempted = false;
    bool m_urlRetryAttempted = false;
    bool m_usingDownloadedSource = false;
    bool m_skipDownloadedOnce = false;
    bool m_internalStop = false;
    bool m_wasPlaying = false;
    bool m_playbackIntent = false;
    qint64 m_pendingResumePositionMs = -1;
    qint64 m_lastKnownPositionMs = 0;
    qint64 m_lastKnownDurationMs = 0;
    QString m_loadedSongIdentity;
    int m_consecutiveFailures = 0;
    QSet<qint64> m_coverSaveInFlight;
    int m_endOfMediaToken = -1;
    QTimer m_endStallTimer;
    int m_endStallToken = -1;
};

} // namespace core
