#pragma once

#include "core/SearchTypes.h"

#include <QString>
#include <QStringList>

namespace core {

class SearchCache
{
public:
    explicit SearchCache(const QString &rootPath = {});
    static void setDefaultRootPathOverride(const QString &rootPath);

    QString rootPath() const { return m_rootPath; }

    bool loadResponse(const SearchRequest &request, SourceId source,
                      SearchResponse *response, bool *fresh = nullptr,
                      qint64 ttlMs = 30 * 60 * 1000) const;
    bool storeResponse(const SearchRequest &request, const SearchResponse &response) const;

    bool loadSuggestions(SourceId source, const QString &query,
                         QList<SearchSuggestion> *suggestions, bool *fresh = nullptr,
                         qint64 ttlMs = 10 * 60 * 1000) const;
    bool storeSuggestions(SourceId source, const QString &query,
                          const QList<SearchSuggestion> &suggestions) const;

    bool loadHotTerms(SourceId source, QList<HotSearchTerm> *terms,
                      bool *fresh = nullptr, qint64 ttlMs = 5 * 60 * 1000) const;
    bool storeHotTerms(SourceId source, const QList<HotSearchTerm> &terms) const;

    QStringList history(int limit = 20) const;
    void addHistory(const QString &query, int limit = 20) const;
    void clearHistory() const;

    // 搜索响应属于可丢弃缓存；搜索历史保存在 QSettings 中，不受这两个接口影响。
    void payloadUsage(qint64 *bytes, int *files) const;
    QStringList clearPayloads() const;

    static QString normalizedQuery(const QString &query);

private:
    QString entryPath(const QString &kind, const QString &key) const;
    bool readPayload(const QString &path, QByteArray *payload, bool *fresh,
                     qint64 ttlMs) const;
    bool writePayload(const QString &path, const QByteArray &payload) const;

    QString m_rootPath;
    static QString s_defaultRootPathOverride;
};

} // namespace core
