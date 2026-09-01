#include "CoverProvider.h"

#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>

namespace ui {

QHash<QString, QPixmap> CoverProvider::s_cache;

namespace {

QPixmap rounded(const QPixmap &src, qreal radius)
{
    if (radius <= 0)
        return src;
    QPixmap out(src.size());
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, src.width(), src.height()), radius, radius);
    p.setClipPath(path);
    p.drawPixmap(0, 0, src);
    return out;
}

QPixmap scaledToCover(const QPixmap &src, int size)
{
    return src.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
}

} // namespace

QPixmap CoverProvider::placeholder(const QString &seed, int size, qreal radius)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xEC, 0x41, 0x41));
    p.drawRoundedRect(0, 0, size, size, radius, radius);
    p.setPen(Qt::white);
    QFont f(QStringLiteral("Microsoft YaHei UI"), qMax(10, size / 3), QFont::Bold);
    p.setFont(f);
    const QString ch = seed.isEmpty() ? QStringLiteral("乐") : seed.left(1);
    p.drawText(pm.rect(), Qt::AlignCenter, ch);
    return pm;
}

QPixmap CoverProvider::coverFor(const core::Song &song, int size, qreal radius)
{
    const QString key = QStringLiteral("cover|%1|%2|%3|%4").arg(song.coverPath, song.title).arg(size).arg(radius);
    const auto it = s_cache.constFind(key);
    if (it != s_cache.constEnd())
        return it.value();

    QPixmap result;
    if (!song.coverPath.isEmpty() && QFileInfo::exists(song.coverPath)) {
        QImageReader reader(song.coverPath);
        QSize decodedSize = reader.size();
        if (decodedSize.isValid()) {
            decodedSize.scale(size, size, Qt::KeepAspectRatioByExpanding);
            reader.setScaledSize(decodedSize);
        }
        const QPixmap raw = QPixmap::fromImage(reader.read());
        if (!raw.isNull())
            result = rounded(scaledToCover(raw, size), radius);
    }
    if (result.isNull()) {
        const QString seed = song.title.isEmpty() ? song.filePath : song.title + song.artist;
        result = placeholder(seed, size, radius);
    }
    if (s_cache.size() > 800)
        s_cache.clear();
    s_cache.insert(key, result);
    return result;
}

void CoverProvider::clearCache()
{
    s_cache.clear();
}

} // namespace ui
