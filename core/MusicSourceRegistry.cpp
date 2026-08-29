#include "MusicSourceRegistry.h"

#include <algorithm>

namespace core {

void MusicSourceRegistry::registerSource(MusicSource *source)
{
    if (source)
        m_sources.insert(int(source->sourceId()), source);
}

void MusicSourceRegistry::unregisterSource(SourceId sourceId)
{
    m_sources.remove(int(sourceId));
}

MusicSource *MusicSourceRegistry::source(SourceId sourceId) const
{
    return m_sources.value(int(sourceId), nullptr);
}

MusicSource *MusicSourceRegistry::sourceFor(const Song &song) const
{
    return source(song.sourceId());
}

QList<MusicSource *> MusicSourceRegistry::onlineSources() const
{
    QList<MusicSource *> result;
    for (MusicSource *source : m_sources) {
        if (source && source->sourceId() != SourceId::Local)
            result.append(source);
    }
    std::sort(result.begin(), result.end(), [](MusicSource *a, MusicSource *b) {
        return int(a->sourceId()) < int(b->sourceId());
    });
    return result;
}

} // namespace core
