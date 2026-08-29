#pragma once

#include "core/MusicSource.h"

#include <QHash>
#include <QList>

namespace core {

class MusicSourceRegistry
{
public:
    void registerSource(MusicSource *source);
    void unregisterSource(SourceId sourceId);
    MusicSource *source(SourceId sourceId) const;
    MusicSource *sourceFor(const Song &song) const;
    QList<MusicSource *> onlineSources() const;

private:
    QHash<int, MusicSource *> m_sources;
};

} // namespace core
