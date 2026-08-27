#include "CoverProvider.h"

#include <QFileInfo>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPainterPath>

namespace ui {

QHash<QString, QPixmap> CoverProvider::s_cache;

namespace {

QColor gradientColor(quint32 seed, int index)
{
    static const QColor palette[8][2] = {
        { QColor(0xEC, 0x41, 0x41), QColor(0xFF, 0x9A, 0x76) },
        { QColor(0xFF, 0x9A, 0x3D), QColor(0xFF, 0xD2, 0x8F) },
        { QColor(0x9B, 0x59, 0xB6), QColor(0xD6, 0x8B, 0xE6) },
        { QColor(0x2F, 0x80, 0xED), QColor(0x7F, 0xC8, 0xF8) },
        { QColor(0x11, 0x99, 0x8E), QColor(0x38, 0xEF, 0x7D) },
        { QColor(0xF9, 0x53, 0xC6), QColor(0xFF, 0x9C, 0xDB) },
        { QColor(0x3A, 0x3A, 0x52), QColor(0x6A, 0x6A, 0x8F) },
        { QColor(0xB8, 0x86, 0x0B), QColor(0xFF, 0xD7, 0x00) }
    };
    const auto &c = palette[seed % 8];
    return index == 0 ? c[0] : c[1];
}

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
    const quint32 h = qHash(seed);
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, size, size);
    g.setColorAt(0, gradientColor(h, 0));
    g.setColorAt(1, gradientColor(h, 1));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
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
        QPixmap raw(song.coverPath);
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

QPixmap CoverProvider::blur(const QPixmap &src, int radius, const QSize &size)
{
    if (src.isNull())
        return {};
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawPixmap(QRect(QPoint(0, 0), size), src);
    p.end();

    QPixmap pix = QPixmap::fromImage(result);
    QGraphicsScene scene;
    QGraphicsPixmapItem item(pix);
    scene.addItem(&item);
    QGraphicsBlurEffect effect;
    effect.setBlurRadius(radius);
    item.setGraphicsEffect(&effect);
    QImage blurred(size, QImage::Format_ARGB32_Premultiplied);
    blurred.fill(Qt::transparent);
    QPainter bp(&blurred);
    scene.render(&bp);
    bp.end();
    return QPixmap::fromImage(blurred);
}

void CoverProvider::clearCache()
{
    s_cache.clear();
}

} // namespace ui
