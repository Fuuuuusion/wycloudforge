#include "SearchCache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace core {
namespace {

QString number(qint64 value)
{
    return QString::number(value);
}

qint64 integer(const QJsonValue &value)
{
    return value.toVariant().toLongLong();
}

QJsonObject songToJson(const Song &song)
{
    return {
        { QStringLiteral("source"), song.source },
        { QStringLiteral("remoteId"), song.effectiveRemoteId() },
        { QStringLiteral("albumRemoteId"), song.effectiveAlbumRemoteId() },
        { QStringLiteral("artistRemoteId"), song.artistRemoteId },
        { QStringLiteral("title"), song.title },
        { QStringLiteral("artist"), song.artist },
        { QStringLiteral("album"), song.album },
        { QStringLiteral("durationMs"), number(song.durationMs) },
        { QStringLiteral("coverUrl"), song.coverUrl },
        { QStringLiteral("filePath"), song.filePath }
        ,{ QStringLiteral("accessRequirement"), int(song.accessRequirement) }
    };
}

Song songFromJson(const QJsonObject &object)
{
    Song song;
    song.source = object.value(QStringLiteral("source")).toInt();
    song.remoteId = object.value(QStringLiteral("remoteId")).toString();
    song.albumRemoteId = object.value(QStringLiteral("albumRemoteId")).toString();
    song.artistRemoteId = object.value(QStringLiteral("artistRemoteId")).toString();
    song.title = object.value(QStringLiteral("title")).toString();
    song.artist = object.value(QStringLiteral("artist")).toString();
    song.album = object.value(QStringLiteral("album")).toString();
    song.durationMs = integer(object.value(QStringLiteral("durationMs")));
    song.coverUrl = object.value(QStringLiteral("coverUrl")).toString();
    song.filePath = object.value(QStringLiteral("filePath")).toString();
    song.accessRequirement = static_cast<AccessRequirement>(qBound(
        int(AccessRequirement::Unknown),
        object.value(QStringLiteral("accessRequirement")).toInt(),
        int(AccessRequirement::Unavailable)));
    if (song.sourceId() == SourceId::Netease) {
        song.onlineId = song.remoteId.toLongLong();
        song.albumId = song.albumRemoteId.toLongLong();
    }
    return song;
}

QJsonObject itemToJson(const SearchResultItem &item)
{
    return {
        { QStringLiteral("type"), int(item.type) },
        { QStringLiteral("source"), int(item.source) },
        { QStringLiteral("remoteId"), item.remoteId },
        { QStringLiteral("title"), item.title },
        { QStringLiteral("subtitle"), item.subtitle },
        { QStringLiteral("artist"), item.artist },
        { QStringLiteral("album"), item.album },
        { QStringLiteral("coverUrl"), item.coverUrl },
        { QStringLiteral("durationMs"), number(item.durationMs) },
        { QStringLiteral("sourceRank"), item.sourceRank },
        { QStringLiteral("popularity"), item.popularity },
        { QStringLiteral("playable"), item.playable },
        { QStringLiteral("availabilityError"), item.availabilityError },
        { QStringLiteral("song"), songToJson(item.song) }
    };
}

SearchResultItem itemFromJson(const QJsonObject &object)
{
    SearchResultItem item;
    item.type = static_cast<SearchItemType>(object.value(QStringLiteral("type")).toInt());
    item.source = static_cast<SourceId>(object.value(QStringLiteral("source")).toInt());
    item.remoteId = object.value(QStringLiteral("remoteId")).toString();
    item.title = object.value(QStringLiteral("title")).toString();
    item.subtitle = object.value(QStringLiteral("subtitle")).toString();
    item.artist = object.value(QStringLiteral("artist")).toString();
    item.album = object.value(QStringLiteral("album")).toString();
    item.coverUrl = object.value(QStringLiteral("coverUrl")).toString();
    item.durationMs = integer(object.value(QStringLiteral("durationMs")));
    item.sourceRank = object.value(QStringLiteral("sourceRank")).toInt(-1);
    item.popularity = object.value(QStringLiteral("popularity")).toDouble(-1.0);
    item.playable = object.value(QStringLiteral("playable")).toBool(true);
    item.availabilityError = object.value(QStringLiteral("availabilityError")).toString();
    item.song = songFromJson(object.value(QStringLiteral("song")).toObject());
    return item;
}

QJsonObject suggestionToJson(const SearchSuggestion &suggestion)
{
    return {
        { QStringLiteral("source"), int(suggestion.source) },
        { QStringLiteral("type"), int(suggestion.type) },
        { QStringLiteral("text"), suggestion.text },
        { QStringLiteral("subtitle"), suggestion.subtitle },
        { QStringLiteral("remoteId"), suggestion.remoteId }
    };
}

SearchSuggestion suggestionFromJson(const QJsonObject &object)
{
    SearchSuggestion suggestion;
    suggestion.source = static_cast<SourceId>(object.value(QStringLiteral("source")).toInt());
    suggestion.type = static_cast<SearchItemType>(object.value(QStringLiteral("type")).toInt());
    suggestion.text = object.value(QStringLiteral("text")).toString();
    suggestion.subtitle = object.value(QStringLiteral("subtitle")).toString();
    suggestion.remoteId = object.value(QStringLiteral("remoteId")).toString();
    return suggestion;
}

QJsonObject hotTermToJson(const HotSearchTerm &term)
{
    return {
        { QStringLiteral("source"), int(term.source) },
        { QStringLiteral("text"), term.text },
        { QStringLiteral("description"), term.description },
        { QStringLiteral("score"), term.score },
        { QStringLiteral("rank"), term.rank }
    };
}

HotSearchTerm hotTermFromJson(const QJsonObject &object)
{
    HotSearchTerm term;
    term.source = static_cast<SourceId>(object.value(QStringLiteral("source")).toInt());
    term.text = object.value(QStringLiteral("text")).toString();
    term.description = object.value(QStringLiteral("description")).toString();
    term.score = object.value(QStringLiteral("score")).toDouble(-1.0);
    term.rank = object.value(QStringLiteral("rank")).toInt(-1);
    return term;
}

} // namespace

QString SearchCache::s_defaultRootPathOverride;

SearchCache::SearchCache(const QString &rootPath)
    : m_rootPath(rootPath.isEmpty()
                     ? (s_defaultRootPathOverride.isEmpty()
                            ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                  + QStringLiteral("/search-v1")
                            : s_defaultRootPathOverride)
                     : rootPath)
{
}

void SearchCache::setDefaultRootPathOverride(const QString &rootPath)
{
    s_defaultRootPathOverride = rootPath;
}

QString SearchCache::normalizedQuery(const QString &query)
{
    return query.normalized(QString::NormalizationForm_KC).toCaseFolded().simplified();
}

QString SearchCache::entryPath(const QString &kind, const QString &key) const
{
    const QByteArray digest = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256)
                                  .toHex();
    return m_rootPath + QLatin1Char('/') + kind + QLatin1Char('/')
        + QString::fromLatin1(digest) + QStringLiteral(".json");
}

bool SearchCache::readPayload(const QString &path, QByteArray *payload, bool *fresh,
                              qint64 ttlMs) const
{
    if (fresh)
        *fresh = false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    const QJsonObject envelope = document.object();
    if (error.error != QJsonParseError::NoError || envelope.isEmpty())
        return false;
    const QByteArray encoded = envelope.value(QStringLiteral("payload")).toString().toLatin1();
    const QByteArray decoded = QByteArray::fromBase64(encoded);
    if (decoded.isEmpty() && !encoded.isEmpty())
        return false;
    if (payload)
        *payload = decoded;
    const qint64 createdMs = integer(envelope.value(QStringLiteral("createdMs")));
    if (fresh) {
        const qint64 age = QDateTime::currentMSecsSinceEpoch() - createdMs;
        *fresh = createdMs > 0 && age >= 0 && age <= qMax<qint64>(0, ttlMs);
    }
    return true;
}

bool SearchCache::writePayload(const QString &path, const QByteArray &payload) const
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    const QJsonObject envelope{
        { QStringLiteral("version"), 1 },
        { QStringLiteral("createdMs"), number(QDateTime::currentMSecsSinceEpoch()) },
        { QStringLiteral("payload"), QString::fromLatin1(payload.toBase64()) }
    };
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(QJsonDocument(envelope).toJson(QJsonDocument::Compact)) < 0)
        return false;
    return file.commit();
}

bool SearchCache::loadResponse(const SearchRequest &request, SourceId source,
                               SearchResponse *response, bool *fresh, qint64 ttlMs) const
{
    const QString key = QStringLiteral("%1|%2|%3|%4|%5")
                            .arg(int(source)).arg(int(request.category))
                            .arg(request.offset).arg(request.limit)
                            .arg(normalizedQuery(request.keywords));
    QByteArray payload;
    if (!readPayload(entryPath(QStringLiteral("results"), key), &payload, fresh, ttlMs))
        return false;
    QJsonParseError error;
    const QJsonObject object = QJsonDocument::fromJson(payload, &error).object();
    if (error.error != QJsonParseError::NoError || object.isEmpty())
        return false;
    SearchResponse value;
    value.source = source;
    value.category = request.category;
    value.offset = object.value(QStringLiteral("offset")).toInt(request.offset);
    value.hasMore = object.value(QStringLiteral("hasMore")).toBool();
    value.generation = request.generation;
    const QJsonArray items = object.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &item : items) {
        const SearchResultItem parsed = itemFromJson(item.toObject());
        if (!parsed.stableIdentity().isEmpty())
            value.items.append(parsed);
    }
    if (response)
        *response = value;
    return true;
}

bool SearchCache::storeResponse(const SearchRequest &request,
                                const SearchResponse &response) const
{
    QJsonArray items;
    for (const SearchResultItem &item : response.items)
        items.append(itemToJson(item));
    const QJsonObject object{
        { QStringLiteral("offset"), response.offset },
        { QStringLiteral("hasMore"), response.hasMore },
        { QStringLiteral("items"), items }
    };
    const QString key = QStringLiteral("%1|%2|%3|%4|%5")
                            .arg(int(response.source)).arg(int(request.category))
                            .arg(request.offset).arg(request.limit)
                            .arg(normalizedQuery(request.keywords));
    return writePayload(entryPath(QStringLiteral("results"), key),
                        QJsonDocument(object).toJson(QJsonDocument::Compact));
}

bool SearchCache::loadSuggestions(SourceId source, const QString &query,
                                  QList<SearchSuggestion> *suggestions, bool *fresh,
                                  qint64 ttlMs) const
{
    QByteArray payload;
    const QString key = QStringLiteral("%1|%2").arg(int(source)).arg(normalizedQuery(query));
    if (!readPayload(entryPath(QStringLiteral("suggestions"), key), &payload, fresh, ttlMs))
        return false;
    QJsonParseError error;
    const QJsonArray array = QJsonDocument::fromJson(payload, &error).array();
    if (error.error != QJsonParseError::NoError)
        return false;
    QList<SearchSuggestion> values;
    for (const QJsonValue &value : array) {
        const SearchSuggestion suggestion = suggestionFromJson(value.toObject());
        if (!suggestion.text.trimmed().isEmpty())
            values.append(suggestion);
    }
    if (suggestions)
        *suggestions = values;
    return true;
}

bool SearchCache::storeSuggestions(SourceId source, const QString &query,
                                   const QList<SearchSuggestion> &suggestions) const
{
    QJsonArray array;
    for (const SearchSuggestion &suggestion : suggestions)
        array.append(suggestionToJson(suggestion));
    const QString key = QStringLiteral("%1|%2").arg(int(source)).arg(normalizedQuery(query));
    return writePayload(entryPath(QStringLiteral("suggestions"), key),
                        QJsonDocument(array).toJson(QJsonDocument::Compact));
}

bool SearchCache::loadHotTerms(SourceId source, QList<HotSearchTerm> *terms,
                               bool *fresh, qint64 ttlMs) const
{
    QByteArray payload;
    if (!readPayload(entryPath(QStringLiteral("hot"), QString::number(int(source))),
                     &payload, fresh, ttlMs)) {
        return false;
    }
    QJsonParseError error;
    const QJsonArray array = QJsonDocument::fromJson(payload, &error).array();
    if (error.error != QJsonParseError::NoError)
        return false;
    QList<HotSearchTerm> values;
    for (const QJsonValue &value : array) {
        const HotSearchTerm term = hotTermFromJson(value.toObject());
        if (!term.text.trimmed().isEmpty())
            values.append(term);
    }
    if (terms)
        *terms = values;
    return true;
}

bool SearchCache::storeHotTerms(SourceId source, const QList<HotSearchTerm> &terms) const
{
    QJsonArray array;
    for (const HotSearchTerm &term : terms)
        array.append(hotTermToJson(term));
    return writePayload(entryPath(QStringLiteral("hot"), QString::number(int(source))),
                        QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QStringList SearchCache::history(int limit) const
{
    QStringList values = QSettings().value(QStringLiteral("search/history")).toStringList();
    if (limit >= 0 && values.size() > limit)
        values = values.mid(0, limit);
    return values;
}

void SearchCache::addHistory(const QString &query, int limit) const
{
    const QString text = query.trimmed();
    if (text.isEmpty())
        return;
    QStringList values = history(-1);
    const QString normalized = normalizedQuery(text);
    for (auto it = values.begin(); it != values.end();) {
        if (normalizedQuery(*it) == normalized)
            it = values.erase(it);
        else
            ++it;
    }
    values.prepend(text);
    if (limit >= 0 && values.size() > limit)
        values = values.mid(0, limit);
    QSettings().setValue(QStringLiteral("search/history"), values);
}

void SearchCache::clearHistory() const
{
    QSettings().remove(QStringLiteral("search/history"));
}

void SearchCache::payloadUsage(qint64 *bytes, int *files) const
{
    qint64 totalBytes = 0;
    int totalFiles = 0;
    const QStringList kinds = {
        QStringLiteral("results"),
        QStringLiteral("suggestions"),
        QStringLiteral("hot")
    };
    for (const QString &kind : kinds) {
        const QDir directory(QDir(m_rootPath).filePath(kind));
        const QFileInfoList entries = directory.entryInfoList(
            QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo &entry : entries) {
            totalBytes += qMax<qint64>(0, entry.size());
            ++totalFiles;
        }
    }
    if (bytes)
        *bytes = totalBytes;
    if (files)
        *files = totalFiles;
}

QStringList SearchCache::clearPayloads() const
{
    QStringList failures;
    const QStringList kinds = {
        QStringLiteral("results"),
        QStringLiteral("suggestions"),
        QStringLiteral("hot")
    };
    for (const QString &kind : kinds) {
        QDir directory(QDir(m_rootPath).filePath(kind));
        const QFileInfoList entries = directory.entryInfoList(
            QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo &entry : entries) {
            if (!QFile::remove(entry.absoluteFilePath()))
                failures.append(entry.absoluteFilePath());
        }
        QDir(m_rootPath).rmdir(kind);
    }
    return failures;
}

} // namespace core
