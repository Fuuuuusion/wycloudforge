#include "SongListModel.h"

#include <QColor>
#include <QFileInfo>
#include <QHash>

#include <limits>
#include <utility>

namespace ui {
namespace {

QString sourceName(core::SourceId source)
{
    switch (source) {
    case core::SourceId::Local: return QStringLiteral("本地音乐");
    case core::SourceId::Netease: return QStringLiteral("网易云音乐");
    case core::SourceId::QqMusic: return QStringLiteral("QQ 音乐");
    }
    return QStringLiteral("未知来源");
}

bool localFileAvailable(const core::Song &song)
{
    if (song.sourceId() != core::SourceId::Local)
        return song.isDownloaded() || song.isCached();
    const QFileInfo info(song.filePath);
    return !song.missing && info.isFile() && info.size() > 0;
}

bool choiceAvailable(const core::SearchResultItem &item)
{
    if (localFileAvailable(item.song))
        return true;
    if (item.source == core::SourceId::Local)
        return false;
    return item.playable && !item.song.missing;
}

bool betterSourceVariant(const core::SearchResultVariant &left,
                         const core::SearchResultVariant &right)
{
    const bool leftAvailable = choiceAvailable(left.item);
    const bool rightAvailable = choiceAvailable(right.item);
    if (leftAvailable != rightAvailable)
        return leftAvailable;
    if (left.localPriority != right.localPriority)
        return left.localPriority > right.localPriority;
    if (left.item.playable != right.item.playable)
        return left.item.playable;
    if (left.heatPercentile != right.heatPercentile)
        return left.heatPercentile > right.heatPercentile;
    const int leftRank = left.item.sourceRank >= 0
        ? left.item.sourceRank : std::numeric_limits<int>::max();
    const int rightRank = right.item.sourceRank >= 0
        ? right.item.sourceRank : std::numeric_limits<int>::max();
    if (leftRank != rightRank)
        return leftRank < rightRank;
    return left.item.stableIdentity() < right.item.stableIdentity();
}

QString defaultUnavailableReason(core::SourceId source, bool versionExists)
{
    if (!versionExists)
        return QStringLiteral("无此来源版本");
    if (source == core::SourceId::Local)
        return QStringLiteral("本地文件缺失");
    return QStringLiteral("来源当前不可用");
}

} // namespace

SongListModel::SongListModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int SongListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_songs.size();
}

int SongListModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 8; // 状态 / 封面 / 歌名-歌手 / 来源 / 专辑 / 时长 / 收藏 / 下载
}

QVariant SongListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_songs.size())
        return {};
    const core::Song &song = m_songs[index.row()];
    switch (role) {
    case IndexRole: return index.row();
    case TitleRole: return song.title;
    case ArtistRole: return song.artist;
    case AlbumRole: return song.album;
    case DurationRole: return song.durationMs;
    case IsPlayingRole: return song.id == m_playingId;
    case SourceRole: return song.source;
    case CachedRole: return song.isCached();
    case MissingRole: return song.missing;
    case DownloadedRole: return song.isDownloaded();
    case FavoriteRole: return m_favoriteIds.contains(song.id);
    case SelectedRole: return m_selectedIdentities.contains(rowIdentityAt(index.row()));
    case BatchModeRole: return m_batchMode;
    case DownloadingRole: return m_downloadingIdentities.contains(song.selectionIdentity());
    case StableIdentityRole: return rowIdentityAt(index.row());
    case ActiveSourceRole: return int(activeSourceAt(index.row()));
    case Qt::ToolTipRole: {
        if (index.column() == 2) {
            QString text = song.title;
            if (!song.artist.trimmed().isEmpty())
                text += QStringLiteral(" - %1").arg(song.artist);
            if (!song.album.trimmed().isEmpty())
                text += QStringLiteral("\n专辑：%1").arg(song.album);
            return text;
        }
        if (index.column() == 3) {
            const QList<SongSourceChoice> choices = sourceChoicesAt(index.row());
            for (const SongSourceChoice &choice : choices) {
                if (choice.source != activeSourceAt(index.row()))
                    continue;
                return choice.available
                    ? QStringLiteral("%1 · 点击展开来源").arg(sourceName(choice.source))
                    : QStringLiteral("%1 · %2").arg(sourceName(choice.source),
                                                       choice.unavailableReason);
            }
            return QStringLiteral("点击展开来源");
        }
        if (index.column() == 6)
            return m_favoriteIds.contains(song.id) ? QStringLiteral("取消喜欢")
                                                   : QStringLiteral("喜欢");
        if (index.column() == 7) {
            if (m_downloadingIdentities.contains(song.selectionIdentity()))
                return QStringLiteral("下载中");
            if (song.isDownloaded())
                return QStringLiteral("删除下载");
            if (song.isOnline())
                return QStringLiteral("下载");
        }
        if (song.isOnline()) {
            if (song.isDownloaded())
                return QStringLiteral("在线歌曲(已下载,可离线播放)");
            if (song.isCached())
                return QStringLiteral("在线歌曲(已缓存,可离线播放)");
            if (song.missing)
                return QStringLiteral("在线歌曲(已失效)");
            return QStringLiteral("在线歌曲(需网络)");
        }
        return song.missing ? QStringLiteral("本地歌曲(文件缺失)") : QStringLiteral("本地歌曲");
    }
    case SongRole: return QVariant::fromValue(song);
    default: return {};
    }
}

void SongListModel::setSongs(const QList<core::Song> &songs, qint64 playingId)
{
    beginResetModel();
    m_songs = songs;
    m_rows.clear();
    m_rows.reserve(songs.size());
    for (const core::Song &song : songs) {
        SongSourceChoice choice;
        choice.source = song.sourceId();
        choice.song = song;
        choice.available = song.sourceId() == core::SourceId::Local
            ? localFileAvailable(song) : (localFileAvailable(song) || !song.missing);
        if (!choice.available)
            choice.unavailableReason = defaultUnavailableReason(choice.source, true);

        RowContext row;
        row.identity = song.selectionIdentity();
        row.choices.append(choice);
        row.activeSource = choice.source;
        m_rows.append(row);
    }
    m_playingId = playingId;
    endResetModel();
}

void SongListModel::setSearchResultGroups(const QList<core::SearchResultGroup> &groups,
                                          qint64 playingId)
{
    QHash<QString, core::SourceId> previousSources;
    for (const RowContext &row : std::as_const(m_rows))
        previousSources.insert(row.identity, row.activeSource);

    beginResetModel();
    m_songs.clear();
    m_rows.clear();
    m_songs.reserve(groups.size());
    m_rows.reserve(groups.size());

    constexpr core::SourceId priority[] = {
        core::SourceId::Local, core::SourceId::Netease, core::SourceId::QqMusic
    };
    for (const core::SearchResultGroup &group : groups) {
        RowContext row;
        row.identity = group.identity;
        QHash<int, const core::SearchResultVariant *> preferredBySource;
        for (const core::SearchResultVariant &variant : group.variants) {
            if (variant.item.type != core::SearchItemType::Song)
                continue;
            const int sourceKey = int(variant.item.source);
            const auto current = preferredBySource.constFind(sourceKey);
            if (current == preferredBySource.cend()
                || betterSourceVariant(variant, **current)) {
                preferredBySource.insert(sourceKey, &variant);
            }
        }
        for (core::SourceId source : priority) {
            const core::SearchResultVariant *variant = preferredBySource.value(int(source));
            if (!variant)
                continue;
            SongSourceChoice choice;
            choice.source = variant->item.source;
            choice.song = variant->item.song;
            choice.available = choiceAvailable(variant->item);
            if (!choice.available) {
                choice.unavailableReason = variant->item.availabilityError.trimmed();
                if (choice.unavailableReason.isEmpty())
                    choice.unavailableReason = defaultUnavailableReason(choice.source, true);
            }
            row.choices.append(choice);
        }
        if (row.choices.isEmpty())
            continue;

        auto choose = [&row](core::SourceId source, bool requireAvailable) {
            for (const SongSourceChoice &choice : std::as_const(row.choices)) {
                if (choice.source == source && (!requireAvailable || choice.available)) {
                    row.activeSource = source;
                    return true;
                }
            }
            return false;
        };

        bool selected = false;
        const auto previous = previousSources.constFind(row.identity);
        if (previous != previousSources.cend())
            selected = choose(previous.value(), true);
        for (core::SourceId source : priority) {
            if (!selected)
                selected = choose(source, true);
        }
        for (core::SourceId source : priority) {
            if (!selected)
                selected = choose(source, false);
        }

        core::Song activeSong;
        for (const SongSourceChoice &choice : std::as_const(row.choices)) {
            if (choice.source == row.activeSource) {
                activeSong = choice.song;
                break;
            }
        }
        if (row.identity.isEmpty())
            row.identity = activeSong.selectionIdentity();
        m_rows.append(row);
        m_songs.append(activeSong);
    }
    m_playingId = playingId;
    endResetModel();
}

void SongListModel::refreshSongs(const QList<core::Song> &songs)
{
    if (songs.size() != m_songs.size()) {
        setSongs(songs, m_playingId);
        return;
    }
    m_songs = songs;
    for (int row = 0; row < m_songs.size() && row < m_rows.size(); ++row) {
        for (SongSourceChoice &choice : m_rows[row].choices) {
            if (choice.source != m_rows.at(row).activeSource)
                continue;
            choice.song = m_songs.at(row);
            choice.available = choice.source == core::SourceId::Local
                ? localFileAvailable(choice.song)
                : (localFileAvailable(choice.song) || !choice.song.missing);
            if (choice.available)
                choice.unavailableReason.clear();
            break;
        }
    }
    if (!m_songs.isEmpty())
        emit dataChanged(index(0, 0), index(m_songs.size() - 1, columnCount() - 1));
}

bool SongListModel::updateSong(const core::Song &song)
{
    const QString identity = song.selectionIdentity();
    for (int row = 0; row < m_songs.size(); ++row) {
        bool changed = false;
        if (m_songs.at(row).selectionIdentity() == identity) {
            m_songs[row] = song;
            changed = true;
        }
        if (row < m_rows.size()) {
            for (SongSourceChoice &choice : m_rows[row].choices) {
                if (choice.song.selectionIdentity() != identity)
                    continue;
                choice.song = song;
                choice.available = choice.source == core::SourceId::Local
                    ? localFileAvailable(song) : (localFileAvailable(song) || !song.missing);
                if (choice.available)
                    choice.unavailableReason.clear();
                changed = true;
            }
        }
        if (changed) {
            emit dataChanged(index(row, 0), index(row, columnCount() - 1));
            return true;
        }
    }
    return false;
}

core::Song SongListModel::songAt(int row) const
{
    if (row < 0 || row >= m_songs.size())
        return {};
    return m_songs[row];
}

QString SongListModel::rowIdentityAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return m_rows.at(row).identity;
}

QList<SongSourceChoice> SongListModel::sourceChoicesAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return m_rows.at(row).choices;
}

core::SourceId SongListModel::activeSourceAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return core::SourceId::Local;
    return m_rows.at(row).activeSource;
}

bool SongListModel::activateSource(int row, core::SourceId source, QString *error)
{
    if (row < 0 || row >= m_rows.size() || row >= m_songs.size()) {
        if (error)
            *error = QStringLiteral("歌曲不存在");
        return false;
    }
    RowContext &context = m_rows[row];
    for (const SongSourceChoice &choice : std::as_const(context.choices)) {
        if (choice.source != source)
            continue;
        if (!choice.available) {
            if (error)
                *error = choice.unavailableReason.isEmpty()
                    ? defaultUnavailableReason(source, true) : choice.unavailableReason;
            return false;
        }
        context.activeSource = source;
        m_songs[row] = choice.song;
        emit dataChanged(index(row, 0), index(row, columnCount() - 1));
        return true;
    }
    if (error)
        *error = defaultUnavailableReason(source, false);
    return false;
}

void SongListModel::setPlayingId(qint64 playingId)
{
    if (m_playingId == playingId)
        return;
    m_playingId = playingId;
    if (!m_songs.isEmpty())
        emit dataChanged(index(0, 0), index(m_songs.size() - 1, columnCount() - 1));
}

void SongListModel::setFavoriteIds(const QSet<qint64> &ids)
{
    if (m_favoriteIds == ids)
        return;
    m_favoriteIds = ids;
    if (!m_songs.isEmpty())
        emit dataChanged(index(0, 0), index(m_songs.size() - 1, columnCount() - 1));
}

void SongListModel::setBatchMode(bool enabled)
{
    if (m_batchMode == enabled)
        return;
    m_batchMode = enabled;
    if (!enabled)
        m_selectedIdentities.clear();
    if (!m_songs.isEmpty())
        emit dataChanged(index(0, 0), index(m_songs.size() - 1, 0));
}

void SongListModel::setSelectedIdentities(const QSet<QString> &identities)
{
    if (m_selectedIdentities == identities)
        return;
    m_selectedIdentities = identities;
    if (!m_songs.isEmpty())
        emit dataChanged(index(0, 0), index(m_songs.size() - 1, 0));
}

void SongListModel::setDownloadingIdentities(const QSet<QString> &identities)
{
    if (m_downloadingIdentities == identities)
        return;
    m_downloadingIdentities = identities;
    if (!m_songs.isEmpty())
        emit dataChanged(index(0, 7), index(m_songs.size() - 1, 7), { DownloadingRole });
}

} // namespace ui
