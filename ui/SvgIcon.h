#pragma once

#include "ui/ThemeManager.h"

#include <QIcon>
#include <QIconEngine>
#include <QImage>
#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

#include <utility>

namespace ui {

class ThemedSvgIconEngine final : public QIconEngine
{
public:
    explicit ThemedSvgIconEngine(QString path, int preferredSize)
        : m_path(std::move(path)), m_preferredSize(preferredSize)
    {
    }

    QIconEngine *clone() const override
    {
        return new ThemedSvgIconEngine(m_path, m_preferredSize);
    }

    QSize actualSize(const QSize &size, QIcon::Mode, QIcon::State) override
    {
        const int side = qMin(size.width(), size.height());
        return QSize(side, side);
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State) override
    {
        const qreal dpr = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
        const QSize logical = size.isValid() ? size : QSize(m_preferredSize, m_preferredSize);
        const QSize pixels(qMax(1, qRound(logical.width() * dpr)),
                           qMax(1, qRound(logical.height() * dpr)));
        QPixmap result(pixels);
        result.fill(Qt::transparent);
        QSvgRenderer renderer(m_path);
        if (!renderer.isValid())
            return result;
        QPainter painter(&result);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(pixels)));
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(result.rect(), themeColor(mode == QIcon::Disabled
                                                       ? ThemeColor::DisabledText
                                                       : ThemeColor::TextSecondary));
        painter.end();
        result.setDevicePixelRatio(dpr);
        return result;
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode,
               QIcon::State state) override
    {
        const QPixmap image = pixmap(rect.size(), mode, state);
        painter->drawPixmap(rect, image);
    }

private:
    QString m_path;
    int m_preferredSize = 18;
};

class ThemedRasterIconEngine final : public QIconEngine
{
public:
    explicit ThemedRasterIconEngine(QString path, ThemeColor role = ThemeColor::TextSecondary)
        : m_path(std::move(path)), m_role(role)
    {
    }

    QIconEngine *clone() const override { return new ThemedRasterIconEngine(m_path); }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State) override
    {
        QImage source(m_path);
        if (source.isNull())
            return {};
        source = source.convertToFormat(QImage::Format_ARGB32);
        QImage tinted(source.size(), QImage::Format_ARGB32);
        tinted.fill(Qt::transparent);
        const QColor color = themeColor(mode == QIcon::Disabled
                                            ? ThemeColor::DisabledText
                                            : m_role);
        for (int y = 0; y < source.height(); ++y) {
            QRgb *target = reinterpret_cast<QRgb *>(tinted.scanLine(y));
            for (int x = 0; x < source.width(); ++x)
                target[x] = qRgba(color.red(), color.green(), color.blue(),
                                  source.pixelColor(x, y).alpha());
        }
        return QPixmap::fromImage(tinted).scaled(size, Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation);
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode,
               QIcon::State state) override
    {
        painter->drawPixmap(rect, pixmap(rect.size(), mode, state));
    }

private:
    QString m_path;
    ThemeColor m_role = ThemeColor::TextSecondary;
};

// 使用动态 QIconEngine 渲染单色 SVG；主题切换后现有按钮无需重新设置图标。
inline QIcon makeSvgIcon(const QString &path, int size = 18)
{
    return QIcon(new ThemedSvgIconEngine(path, size));
}

inline QIcon makeThemedRasterIcon(const QString &path,
                                  ThemeColor role = ThemeColor::TextSecondary)
{
    return QIcon(new ThemedRasterIconEngine(path, role));
}

} // namespace ui
