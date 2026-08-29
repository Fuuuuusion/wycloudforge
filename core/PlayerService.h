#pragma once

#include "core/MusicSource.h"
#include "core/Song.h"

#include <QAudioOutput>
#include <QJsonArray>
#include <QMediaPlayer>
#include <QMediaDevices>
#include <QObject>
#include <QVector>

namespace core {

class PlayerService : public QObject
{
    Q_OBJECT
public:
    enum PlayMode { Order = 0, RepeatOne = 1, Shuffle = 2 };
    Q_ENUM(PlayMode)

    explicit PlayerService(QObject *parent = nullptr);

    void setSourceProvider(MusicSource *source) { m_source = source; }
    void setLibrary(class LibraryService *library) { m_lib = library; }

    void setPlaylist(const QList<Song> &songs, int startIndex = -1);
    void playIndex(int index);
    void playPause();
    void play();
    void pause();
    void next();
    void prev();
    void seek(qint64 ms);

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

private:
    void loadCurrent(bool autoPlay, bool allowCached = true, bool resetCacheRetry = true);
    bool ensureAudioOutput();
    bool retryInvalidDownloadedFile();
    bool retryInvalidCache();
    void buildShuffleOrder();
    void maybeCacheCurrent(qint64 positionMs);

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
    class LibraryService *m_lib = nullptr;
    QString m_currentUrl;
    bool m_cacheSaved = false;
    int m_loadToken = 0;
    bool m_pendingAutoPlay = false;
    bool m_usingCachedSource = false;
    bool m_cacheRetryAttempted = false;
    bool m_usingDownloadedSource = false;
};

} // namespace core
