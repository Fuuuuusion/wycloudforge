#include "SongListView.h"

#include "ui/ThemeManager.h"

#include "core/LibraryService.h"
#include "core/SearchService.h"
#include "ui/CoverProvider.h"
#include "ui/SourceIcons.h"
#include "ui/SongListModel.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QFocusEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHideEvent>
#include <QIcon>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QtMath>

namespace ui {
namespace {

QColor textPrimaryColor() { return themeColor(ThemeColor::TextPrimary); }
QColor textSecondaryColor() { return themeColor(ThemeColor::TextSecondary); }
QColor textTertiaryColor() { return themeColor(ThemeColor::TextTertiary); }
QColor accentColor() { return themeColor(ThemeColor::Accent); }
QColor accentHoverColor() { return themeColor(ThemeColor::AccentHover); }
QColor accentPressedColor() { return themeColor(ThemeColor::AccentPressed); }
constexpr int kSongRowHeight = 112;
constexpr int kTopSafeAreaHeight = 42;
constexpr int kBottomSafeAreaHeight = 16;

QString combinedSongTitle(const core::Song &song)
{
    const QString title = song.title.trimmed();
    const QString artist = song.artist.trimmed();
    QString result = title;
    if (!title.isEmpty() && !artist.isEmpty())
        result += QStringLiteral(" - ");
    result += artist;
    if (result.isEmpty())
        result = QStringLiteral("未知歌曲");
    if (song.missing)
        result += QStringLiteral(" · 失效");
    return result;
}

QColor blendedColor(const QColor &from, const QColor &to, qreal progress)
{
    const qreal t = qBound<qreal>(0.0, progress, 1.0);
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                            from.greenF() + (to.greenF() - from.greenF()) * t,
                            from.blueF() + (to.blueF() - from.blueF()) * t,
                            from.alphaF() + (to.alphaF() - from.alphaF()) * t);
}
QBrush activeTextBrush(const QRectF &)
{
    return QBrush(accentColor());
}

QPixmap tintedIcon(const QString &path, int size, const QColor &color)
{
    QSvgRenderer renderer(path);
    if (!renderer.isValid())
        return {};
    const qreal dpr = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
    const int pixels = qMax(1, qRound(size * dpr));
    QPixmap result(pixels, pixels);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, pixels, pixels));
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(result.rect(), color);
    painter.end();
    result.setDevicePixelRatio(dpr);
    return result;
}

class BatchDeleteButton final : public QPushButton
{
public:
    explicit BatchDeleteButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setFixedSize(108, 30);
        setCursor(Qt::PointingHandCursor);
        setAccessibleName(QStringLiteral("按来源删除"));
        m_hoverAnimation.setDuration(200);
        connect(&m_hoverAnimation, &QVariantAnimation::valueChanged, this, [this] { update(); });
    }

protected:
    void enterEvent(QEnterEvent *event) override
    {
        animateHoverTo(1.0);
        QPushButton::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        animateHoverTo(0.0);
        QPushButton::leaveEvent(event);
    }

    void focusInEvent(QFocusEvent *event) override
    {
        m_showFocusRing = event->reason() == Qt::TabFocusReason
                          || event->reason() == Qt::BacktabFocusReason
                          || event->reason() == Qt::ShortcutFocusReason;
        QPushButton::focusInEvent(event);
        update();
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        m_showFocusRing = false;
        QPushButton::focusOutEvent(event);
        update();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        QPushButton::mousePressEvent(event);
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        QPushButton::mouseReleaseEvent(event);
        update();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal hover = hoverProgress();

        painter.setPen(Qt::NoPen);
        painter.setBrush(themeColor(ThemeColor::SurfaceAlt));
        painter.drawRoundedRect(QRectF(rect()), 15, 15);

        const qreal panelWidth = 28.0 + 15.0 * hover;
        const QRectF iconPanel(width() - panelWidth, 0, panelWidth, height());
        painter.setBrush(!isEnabled() ? themeColor(ThemeColor::SurfacePressed)
                         : isDown() ? accentPressedColor()
                         : blendedColor(themeColor(ThemeColor::SurfaceHover), accentHoverColor(), hover));
        painter.drawRoundedRect(iconPanel, 15, 15);

        painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 8));
        painter.setPen(!isEnabled() ? textTertiaryColor() : themeColor(ThemeColor::TextSecondary));
        const int textShift = qRound(2.0 * hover);
        painter.drawText(QRectF(8 - textShift, 0, width() - panelWidth - 6, height()),
                         Qt::AlignCenter, text());

        const QColor iconColor = !isEnabled() ? textTertiaryColor()
                                 : hover > 0.05 || isDown() ? Qt::white : textSecondaryColor();
        const QPixmap icon = tintedIcon(QStringLiteral(":/icons/icon-trash.svg"), 15, iconColor);
        const QPointF iconCenter = iconPanel.center() + QPointF(0, 2.0 * hover);
        painter.drawPixmap(QRectF(iconCenter.x() - 7.5, iconCenter.y() - 7.5, 15, 15).toRect(),
                           icon);

        if (hasFocus() && m_showFocusRing) {
            painter.setPen(QPen(textTertiaryColor(), 1.2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(rect()).adjusted(2, 2, -2, -2), 13, 13);
        }
    }

private:
    void animateHoverTo(qreal target)
    {
        const qreal current = hoverProgress();
        m_hoverAnimation.stop();
        m_hoverAnimation.setStartValue(current);
        m_hoverAnimation.setEndValue(target);
        m_hoverAnimation.start();
    }

    qreal hoverProgress() const
    {
        return m_hoverAnimation.state() == QAbstractAnimation::Running
            ? m_hoverAnimation.currentValue().toReal()
            : (underMouse() ? 1.0 : 0.0);
    }

    QVariantAnimation m_hoverAnimation;
    bool m_showFocusRing = false;
};

QString formatDuration(qint64 ms)
{
    const qint64 total = qMax<qint64>(0, ms) / 1000;
    return QStringLiteral("%1:%2").arg(total / 60, 2, 10, QLatin1Char('0')).arg(total % 60, 2, 10, QLatin1Char('0'));
}

void drawHighlightedText(QPainter &p, const QRectF &rect, const QString &text, const QString &query,
                         const QBrush &normal, Qt::Alignment align)
{
    p.save();
    p.setPen(QPen(normal, 1));
    const QFontMetrics fm = p.fontMetrics();
    QString elided = fm.elidedText(text, Qt::ElideRight, int(rect.width()));
    const QList<QPair<int, int>> ranges = core::SearchService::highlightRanges(elided, query);
    if (ranges.isEmpty()) {
        p.drawText(rect, int(align) | Qt::AlignVCenter, elided);
        p.restore();
        return;
    }
    QRectF base = rect;
    if (align & Qt::AlignRight)
        base.setLeft(base.right() - fm.horizontalAdvance(elided));
    int textPosition = 0;
    qreal drawX = base.left();
    for (const auto &range : ranges) {
        const QString before = elided.mid(textPosition, range.first - textPosition);
        const int beforeWidth = fm.horizontalAdvance(before);
        p.setPen(QPen(normal, 1));
        p.drawText(QRectF(drawX, base.top(), beforeWidth, base.height()),
                   Qt::AlignVCenter, before);
        drawX += beforeWidth;

        const QString match = elided.mid(range.first, range.second - range.first);
        const int matchWidth = fm.horizontalAdvance(match);
        const QRectF matchRect(drawX, base.top(), matchWidth, base.height());
        p.setPen(accentColor());
        p.drawText(matchRect, Qt::AlignVCenter, match);
        drawX += matchWidth;
        textPosition = range.second;
    }
    const QString after = elided.mid(textPosition);
    p.setPen(QPen(normal, 1));
    p.drawText(QRectF(drawX, base.top(), qMax<qreal>(0, base.right() - drawX), base.height()),
               Qt::AlignVCenter, after);
    p.restore();
}

class SongRowDelegate : public QStyledItemDelegate
{
public:
    explicit SongRowDelegate(SongListView *view)
        : QStyledItemDelegate(view)
        , m_view(view)
    {
        m_rowHoverAnimation.setDuration(200);
        m_rowHoverAnimation.setStartValue(0.0);
        m_rowHoverAnimation.setEndValue(1.0);
        connect(&m_rowHoverAnimation, &QVariantAnimation::valueChanged,
                view, [this] { updateRows({ m_hoverFromRow, m_hoverToRow }); });

        m_heartHoverAnimation.setDuration(200);
        m_heartHoverAnimation.setStartValue(0.0);
        m_heartHoverAnimation.setEndValue(1.0);
        connect(&m_heartHoverAnimation, &QVariantAnimation::valueChanged,
                view, [this] { updateHeartRows({ m_heartFromRow, m_heartToRow }); });

        m_heartStateAnimation.setDuration(200);
        m_heartStateAnimation.setStartValue(0.0);
        m_heartStateAnimation.setEndValue(1.0);
        connect(&m_heartStateAnimation, &QVariantAnimation::valueChanged,
                view, [this] {
            QSet<int> rows = m_heartAddedRows;
            rows.unite(m_heartRemovedRows);
            updateHeartRows(rows);
        });

        m_actionHoverAnimation.setDuration(200);
        m_actionHoverAnimation.setStartValue(0.0);
        m_actionHoverAnimation.setEndValue(1.0);
        connect(&m_actionHoverAnimation, &QVariantAnimation::valueChanged,
                view, [this] { updateActionRows({ m_actionFromRow, m_actionToRow }); });

        m_downloadCompletionAnimation.setDuration(200);
        m_downloadCompletionAnimation.setStartValue(0.0);
        m_downloadCompletionAnimation.setEndValue(1.0);
        connect(&m_downloadCompletionAnimation, &QVariantAnimation::valueChanged,
                view, [this] { updateActionRows(m_completionRows); });

        m_loadingTimer.setInterval(33);
        connect(&m_loadingTimer, &QTimer::timeout, view, [this] {
            m_loadingAngle = (m_loadingAngle + 12) % 360;
            if (m_view->isVisible() && !m_view->window()->isMinimized())
                updateActionRows(m_loadingRows);
        });

        m_playbackTransitionAnimation.setDuration(200);
        m_playbackTransitionAnimation.setStartValue(0.0);
        m_playbackTransitionAnimation.setEndValue(1.0);
        connect(&m_playbackTransitionAnimation, &QVariantAnimation::valueChanged,
                view, [this] { updatePlayingIndicator(); });

        m_equalizerTimer.setInterval(33);
        connect(&m_equalizerTimer, &QTimer::timeout, view, [this] {
            m_equalizerPhaseMs = (m_equalizerPhaseMs + 33) % 1000;
            if (m_viewVisible && m_view->isVisible() && !m_view->window()->isMinimized())
                updatePlayingIndicator();
        });
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &rawOption,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem option(rawOption);
        const bool firstRow = index.row() == 0;
        option.rect.setTop(rawOption.rect.top() + (firstRow ? kTopSafeAreaHeight : 0));
        option.rect.setHeight(kSongRowHeight);
        const bool playing = index.data(SongListModel::IsPlayingRole).toBool();
        const qreal hoverAmount = rowHoverAmount(index.row());
        const bool hover = hoverAmount > 0.01;
        const bool pressed = index.row() == m_pressedPlayRow;
        // Playing is represented only by the indicator in column 0.  Keeping it
        // separate from hover prevents a playing row from looking permanently
        // hovered after the pointer leaves the list.
        const bool active = hover;

        const int col = index.column();
        QRectF rect = option.rect.adjusted(0, 0, 0, -1);
        const QRectF rowRect(4, option.rect.top() + 2,
                             qMax(0, m_view->viewport()->width() - 8), option.rect.height() - 4);
        painter->save();
        painter->setPen(Qt::NoPen);
        if (pressed) {
            painter->setBrush(QColor(0, 0, 0, 36));
            painter->drawRoundedRect(rowRect, 8, 8);
            rect.translate(0, 1);
        }
        painter->restore();
        if (col == 0) {
            painter->save();
            const bool batch = index.data(SongListModel::BatchModeRole).toBool();
            if (batch) {
                const QRectF box(rect.center().x() - 8, rect.center().y() - 8, 16, 16);
                painter->setPen(QPen(index.data(SongListModel::SelectedRole).toBool()
                                         ? accentColor() : themeColor(ThemeColor::TextTertiary), 1.2));
                painter->setBrush(index.data(SongListModel::SelectedRole).toBool()
                                      ? accentColor() : Qt::NoBrush);
                painter->drawRoundedRect(box, 4, 4);
                if (index.data(SongListModel::SelectedRole).toBool()) {
                    painter->setPen(QPen(Qt::white, 1.6));
                    painter->drawLine(box.left() + 4, box.center().y(), box.left() + 7,
                                      box.bottom() - 4);
                    painter->drawLine(box.left() + 7, box.bottom() - 4, box.right() - 3,
                                      box.top() + 4);
                }
            } else if (playing) {
                const QPointF c = rect.center() + QPointF(1, 0);
                const qreal equalizer = equalizerAmount();
                QPainterPath path;
                path.moveTo(c.x() - 4, c.y() - 5);
                path.lineTo(c.x() - 4, c.y() + 5);
                path.lineTo(c.x() + 5, c.y());
                path.closeSubpath();
                painter->setPen(Qt::NoPen);
                painter->setBrush(accentColor());
                if (equalizer < 1.0) {
                    painter->save();
                    painter->setOpacity(1.0 - equalizer);
                    painter->translate(c);
                    const qreal scale = 1.0 - 0.12 * equalizer;
                    painter->scale(scale, scale);
                    painter->translate(-c);
                    painter->drawPath(path);
                    painter->restore();
                }
                if (equalizer > 0.0) {
                    painter->save();
                    painter->setOpacity(equalizer);
                    constexpr int phaseOffsets[] = { 0, 200, 400 };
                    for (int bar = 0; bar < 3; ++bar) {
                        const qreal phase = qreal((m_equalizerPhaseMs + phaseOffsets[bar]) % 1000)
                            / 1000.0;
                        const qreal wave = 0.5 + 0.5 * qSin(phase * 2.0 * M_PI);
                        const qreal height = 5.0 + 10.0 * wave;
                        const qreal x = c.x() - 5.0 + bar * 5.0;
                        painter->drawRoundedRect(QRectF(x - 1.25, c.y() + 7.5 - height,
                                                        2.5, height), 1.25, 1.25);
                    }
                    painter->restore();
                }
            }
            painter->restore();
            return;
        }

        const core::Song song = index.data(SongListModel::SongRole).value<core::Song>();
        const QString query = m_query;
        if (col == 1) {
            const int coverSize = rect.width() < 100 ? 68 : 80;
            const QRectF coverRect(rect.center().x() - coverSize / 2.0,
                                   rect.center().y() - coverSize / 2.0,
                                   coverSize, coverSize);
            const QPixmap cover = CoverProvider::coverFor(song, coverSize, 8);
            painter->drawPixmap(coverRect.toRect(), cover);
        } else if (col == 2) {
            const QRectF textRect = rect.adjusted(12, 0, -8, 0);
            painter->save();
            painter->setFont(m_titleFont);
            const QString titleText = combinedSongTitle(song);
            const QBrush titleBrush = active ? activeTextBrush(textRect)
                                             : QBrush(song.missing ? textTertiaryColor() : textPrimaryColor());
            drawHighlightedText(*painter, textRect, titleText, query, titleBrush, Qt::AlignLeft);
            painter->restore();
        } else if (col == 3) {
            bool available = false;
            const core::SourceId activeSource = static_cast<core::SourceId>(
                index.data(SongListModel::ActiveSourceRole).toInt());
            if (const auto *model = qobject_cast<const SongListModel *>(index.model())) {
                for (const SongSourceChoice &choice : model->sourceChoicesAt(index.row())) {
                    if (choice.source == activeSource) {
                        available = choice.available;
                        break;
                    }
                }
            }
            const QPixmap icon = sourceIcon(activeSource, available).pixmap(QSize(34, 34));
            painter->drawPixmap(QRectF(rect.center().x() - 17, rect.center().y() - 17,
                                       34, 34).toRect(), icon);
        } else if (col == 4) {
            painter->save();
            painter->setFont(m_baseFont);
            const QRectF textRect = rect.adjusted(10, 0, 0, 0);
            drawHighlightedText(*painter, textRect, song.album, query,
                                active ? activeTextBrush(textRect) : QBrush(textTertiaryColor()), Qt::AlignLeft);
            painter->restore();
        } else if (col == 5) {
            painter->save();
            painter->setFont(m_baseFont);
            painter->setPen(active ? QPen(activeTextBrush(rect), 1) : QPen(textTertiaryColor(), 1));
            painter->drawText(rect.adjusted(0, 0, -12, 0), Qt::AlignRight | Qt::AlignVCenter,
                              formatDuration(song.durationMs));
            painter->restore();
        } else if (col == 6) {
            const bool favorite = index.data(SongListModel::FavoriteRole).toBool();
            const qreal hoverAmount = heartHoverAmount(index.row());
            const bool pressed = index.row() == m_pressedHeartRow;
            QColor color = favorite ? accentColor() : blendedColor(textTertiaryColor(), accentHoverColor(), hoverAmount);
            if (pressed)
                color = accentPressedColor();
            qreal scale = pressed ? 0.92 : 1.0 + 0.06 * hoverAmount;
            if (m_heartStateAnimation.state() == QAbstractAnimation::Running) {
                const qreal t = m_heartStateAnimation.currentValue().toReal();
                if (m_heartAddedRows.contains(index.row()))
                    scale += 0.14 * qSin(M_PI * t);
                else if (m_heartRemovedRows.contains(index.row()))
                    scale -= 0.06 * qSin(M_PI * t);
            }
            const QPixmap heart = tintedIcon(favorite ? QStringLiteral(":/icons/icon-heart-fill.svg")
                                                       : QStringLiteral(":/icons/icon-heart.svg"),
                                             24, color);
            const qreal size = 24.0 * scale;
            painter->drawPixmap(QRectF(rect.center().x() - size / 2.0,
                                       rect.center().y() - size / 2.0, size, size).toRect(), heart);
        } else if (col == 7) {
            painter->save();
            const bool downloading = index.data(SongListModel::DownloadingRole).toBool();
            const bool deleteAction = song.isDownloaded()
                && (m_downloadMode == SongListView::DownloadAction
                    || m_downloadMode == SongListView::DeleteDownloadAction);
            if (downloading && m_downloadMode == SongListView::DownloadAction) {
                painter->setRenderHint(QPainter::Antialiasing);
                painter->setPen(QPen(accentHoverColor(), 2.2, Qt::SolidLine, Qt::RoundCap));
                const QRectF spinner(rect.center().x() - 10.5, rect.center().y() - 10.5, 21, 21);
                painter->drawArc(spinner, (90 - m_loadingAngle) * 16, -270 * 16);
            } else if (deleteAction) {
                const qreal completion = completionAmount(index.row());
                if (completion < 1.0) {
                    painter->save();
                    painter->setOpacity(1.0 - completion);
                    painter->setPen(QPen(accentHoverColor(), 2.2, Qt::SolidLine, Qt::RoundCap));
                    const QRectF spinner(rect.center().x() - 10.5, rect.center().y() - 10.5, 21, 21);
                    painter->drawArc(spinner, (90 - m_loadingAngle) * 16, -270 * 16);
                    painter->restore();
                }
                painter->save();
                painter->setOpacity(completion);
                const qreal hoverAmount = actionHoverAmount(index.row());
                const bool pressed = index.row() == m_pressedActionRow;
                const qreal buttonWidth = 30.0 + 42.0 * hoverAmount;
                const QRectF buttonRect(rect.center().x() - buttonWidth / 2.0,
                                        rect.center().y() - 15.0, buttonWidth, 30.0);
                painter->setPen(Qt::NoPen);
                painter->setBrush(pressed ? accentPressedColor()
                                  : blendedColor(themeColor(ThemeColor::SurfaceAlt),
                                                 accentHoverColor(), hoverAmount));
                painter->drawRoundedRect(buttonRect, 15, 15);

                const qreal iconCenterX = rect.center().x()
                    + (buttonRect.left() + 15.0 - rect.center().x()) * hoverAmount;
                const QColor iconColor = pressed || hoverAmount > 0.05 ? Qt::white : textSecondaryColor();
                const QPixmap trash = tintedIcon(QStringLiteral(":/icons/icon-trash.svg"), 20,
                                                  iconColor);
                const qreal iconCenterY = rect.center().y();
                painter->drawPixmap(QRectF(iconCenterX - 10.0, iconCenterY - 10.0, 20, 20).toRect(),
                                    trash);

                if (hoverAmount > 0.01) {
                    painter->setOpacity(completion * hoverAmount);
                    painter->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 8));
                    painter->setPen(Qt::white);
                    painter->drawText(QRectF(buttonRect.left() + 28, buttonRect.top(),
                                             buttonRect.width() - 31, buttonRect.height()),
                                      Qt::AlignCenter, QStringLiteral("删除"));
                }
                painter->restore();
            } else if (m_downloadMode == SongListView::DownloadAction && song.isOnline()) {
                const qreal hoverAmount = actionHoverAmount(index.row());
                const bool pressed = index.row() == m_pressedActionRow;
                const QColor color = pressed ? accentPressedColor()
                    : blendedColor(textSecondaryColor(), accentHoverColor(), hoverAmount);
                const QPointF center = rect.center();
                painter->setRenderHint(QPainter::Antialiasing);
                painter->setPen(QPen(color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter->drawLine(QPointF(center.x(), center.y() - 8),
                                  QPointF(center.x(), center.y() + 2));
                painter->drawLine(QPointF(center.x() - 4.5, center.y() - 2.5),
                                  QPointF(center.x(), center.y() + 2));
                painter->drawLine(QPointF(center.x() + 4.5, center.y() - 2.5),
                                  QPointF(center.x(), center.y() + 2));
                painter->drawLine(QPointF(center.x() - 8, center.y() + 8),
                                  QPointF(center.x() + 8, center.y() + 8));
                painter->drawLine(QPointF(center.x() - 8, center.y() + 8),
                                  QPointF(center.x() - 8, center.y() + 5));
                painter->drawLine(QPointF(center.x() + 8, center.y() + 8),
                                  QPointF(center.x() + 8, center.y() + 5));
            }
            painter->restore();
        }
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override
    {
        int height = kSongRowHeight;
        if (index.row() == 0)
            height += kTopSafeAreaHeight;
        if (index.model() && index.row() == index.model()->rowCount() - 1)
            height += kBottomSafeAreaHeight;
        return { 0, height };
    }

    void setQuery(const QString &query)
    {
        m_query = query;
    }

    void setHoverRow(int row)
    {
        if (m_hoverToRow == row) {
            m_hoverRow = row;
            return;
        }
        const int previousFrom = m_hoverFromRow;
        const int previousTo = m_hoverToRow;
        m_hoverFromRow = previousTo;
        m_hoverToRow = row;
        m_hoverRow = row;
        m_rowHoverAnimation.stop();
        m_rowHoverAnimation.start();
        updateRows({ previousFrom, previousTo, row });
    }

    void setPointerIndex(int row, int column)
    {
        setHoverRow(row);
        const int heartTargetRow = column == 6 ? row : -1;
        if (heartTargetRow != m_heartToRow) {
            const int previousRow = m_heartToRow;
            m_heartFromRow = previousRow;
            m_heartToRow = heartTargetRow;
            m_heartHoverAnimation.stop();
            m_heartHoverAnimation.start();
            updateHeartRows({ previousRow, heartTargetRow });
        }

        const int actionTargetRow = column == 7 && rowShowsAction(row) ? row : -1;
        if (actionTargetRow != m_actionToRow) {
            const int previousRow = m_actionToRow;
            m_actionFromRow = previousRow;
            m_actionToRow = actionTargetRow;
            m_actionHoverAnimation.stop();
            m_actionHoverAnimation.start();
            updateActionRows({ previousRow, actionTargetRow });
        }
    }

    void setPressedHeartRow(int row)
    {
        if (m_pressedHeartRow == row)
            return;
        const int previous = m_pressedHeartRow;
        m_pressedHeartRow = row;
        updateHeartRows({ previous, row });
    }

    void setPressedActionRow(int row)
    {
        if (m_pressedActionRow == row)
            return;
        const int previous = m_pressedActionRow;
        m_pressedActionRow = row;
        updateActionRows({ previous, row });
    }

    void setPressedPlayRow(int row)
    {
        if (m_pressedPlayRow == row)
            return;
        const int previous = m_pressedPlayRow;
        m_pressedPlayRow = row;
        updateRows({ previous, row });
    }

    void startFavoriteTransitions(const QSet<int> &addedRows, const QSet<int> &removedRows)
    {
        if (addedRows.isEmpty() && removedRows.isEmpty())
            return;
        m_heartAddedRows = addedRows;
        m_heartRemovedRows = removedRows;
        m_heartStateAnimation.stop();
        m_heartStateAnimation.start();
    }

    void setLoadingRows(const QSet<int> &rows)
    {
        QSet<int> changed = m_loadingRows;
        changed.unite(rows);
        m_loadingRows = rows;
        if (rows.isEmpty())
            m_loadingTimer.stop();
        else if (!m_loadingTimer.isActive())
            m_loadingTimer.start();
        updateActionRows(changed);
    }

    void startDownloadCompletion(const QSet<int> &rows)
    {
        if (rows.isEmpty())
            return;
        m_completionRows = rows;
        m_downloadCompletionAnimation.stop();
        m_downloadCompletionAnimation.start();
    }

    void setPlaybackActive(bool active)
    {
        if (m_playbackActive == active)
            return;
        m_playbackActive = active;
        m_playbackTransitionAnimation.stop();
        m_playbackTransitionAnimation.start();
        updateEqualizerTimer();
    }

    void restartPlaybackTransition()
    {
        if (!m_playbackActive)
            return;
        m_playbackTransitionAnimation.stop();
        m_playbackTransitionAnimation.start();
        updateEqualizerTimer();
    }

    void setViewVisible(bool visible)
    {
        m_viewVisible = visible;
        updateEqualizerTimer();
    }

    int hoverRow() const
    {
        return m_hoverRow;
    }

    void setDownloadMode(SongListView::DownloadActionMode mode)
    {
        const int previousActionRow = m_actionToRow;
        m_downloadMode = mode;
        m_actionHoverAnimation.stop();
        m_actionFromRow = -1;
        m_actionToRow = -1;
        m_pressedActionRow = -1;
        updateActionRows({ previousActionRow });
    }

private:
    qreal equalizerAmount() const
    {
        if (m_playbackTransitionAnimation.state() != QAbstractAnimation::Running)
            return m_playbackActive ? 1.0 : 0.0;
        const qreal progress = m_playbackTransitionAnimation.currentValue().toReal();
        return m_playbackActive ? progress : 1.0 - progress;
    }

    void updateEqualizerTimer()
    {
        if (m_playbackActive && m_viewVisible) {
            if (!m_equalizerTimer.isActive())
                m_equalizerTimer.start();
        } else {
            m_equalizerTimer.stop();
        }
        updatePlayingIndicator();
    }

    void updatePlayingIndicator() const
    {
        if (!m_view || !m_view->model())
            return;
        for (int row = 0; row < m_view->model()->rowCount(); ++row) {
            if (!m_view->model()->index(row, 0).data(SongListModel::IsPlayingRole).toBool())
                continue;
            m_view->viewport()->update(m_view->visualRect(m_view->model()->index(row, 0)));
            break;
        }
    }

    qreal rowHoverAmount(int row) const
    {
        if (m_rowHoverAnimation.state() != QAbstractAnimation::Running)
            return row == m_hoverToRow ? 1.0 : 0.0;
        const qreal progress = m_rowHoverAnimation.currentValue().toReal();
        if (row == m_hoverToRow)
            return progress;
        if (row == m_hoverFromRow)
            return 1.0 - progress;
        return 0.0;
    }

    qreal heartHoverAmount(int row) const
    {
        if (m_heartHoverAnimation.state() != QAbstractAnimation::Running)
            return row == m_heartToRow ? 1.0 : 0.0;
        const qreal progress = m_heartHoverAnimation.currentValue().toReal();
        if (row == m_heartToRow)
            return progress;
        if (row == m_heartFromRow)
            return 1.0 - progress;
        return 0.0;
    }

    qreal actionHoverAmount(int row) const
    {
        if (m_actionHoverAnimation.state() != QAbstractAnimation::Running)
            return row == m_actionToRow ? 1.0 : 0.0;
        const qreal progress = m_actionHoverAnimation.currentValue().toReal();
        if (row == m_actionToRow)
            return progress;
        if (row == m_actionFromRow)
            return 1.0 - progress;
        return 0.0;
    }

    qreal completionAmount(int row) const
    {
        if (!m_completionRows.contains(row))
            return 1.0;
        if (m_downloadCompletionAnimation.state() != QAbstractAnimation::Running)
            return 1.0;
        return m_downloadCompletionAnimation.currentValue().toReal();
    }

    bool rowShowsAction(int row) const
    {
        if (!m_view || !m_view->model() || row < 0 || row >= m_view->model()->rowCount())
            return false;
        const QModelIndex index = m_view->model()->index(row, 7);
        const core::Song song = index.data(SongListModel::SongRole).value<core::Song>();
        if (index.data(SongListModel::DownloadingRole).toBool())
            return false;
        if (m_downloadMode == SongListView::DeleteDownloadAction)
            return song.isDownloaded();
        return m_downloadMode == SongListView::DownloadAction && song.isOnline();
    }

    void updateHeartRows(const QSet<int> &rows) const
    {
        if (!m_view || !m_view->model())
            return;
        for (int row : rows) {
            if (row < 0 || row >= m_view->model()->rowCount())
                continue;
            m_view->viewport()->update(m_view->visualRect(m_view->model()->index(row, 6)));
        }
    }

    void updateRows(const QSet<int> &rows) const
    {
        if (!m_view || !m_view->model())
            return;
        for (int row : rows) {
            if (row < 0 || row >= m_view->model()->rowCount())
                continue;
            const QRect first = m_view->visualRect(m_view->model()->index(row, 0));
            const QRect last = m_view->visualRect(m_view->model()->index(row, 7));
            m_view->viewport()->update(first.united(last));
        }
    }

    void updateActionRows(const QSet<int> &rows) const
    {
        if (!m_view || !m_view->model())
            return;
        for (int row : rows) {
            if (row < 0 || row >= m_view->model()->rowCount())
                continue;
            m_view->viewport()->update(m_view->visualRect(m_view->model()->index(row, 7)));
        }
    }

    SongListView *m_view = nullptr;
    QFont m_baseFont = QFont(QStringLiteral("Microsoft YaHei UI"), 9);
    QFont m_titleFont = QFont(QStringLiteral("Microsoft YaHei UI"), 9);
    QString m_query;
    int m_hoverRow = -1;
    int m_hoverFromRow = -1;
    int m_hoverToRow = -1;
    int m_pressedPlayRow = -1;
    int m_heartFromRow = -1;
    int m_heartToRow = -1;
    int m_pressedHeartRow = -1;
    int m_actionFromRow = -1;
    int m_actionToRow = -1;
    int m_pressedActionRow = -1;
    int m_loadingAngle = 0;
    int m_equalizerPhaseMs = 0;
    QSet<int> m_heartAddedRows;
    QSet<int> m_heartRemovedRows;
    QSet<int> m_loadingRows;
    QSet<int> m_completionRows;
    QVariantAnimation m_rowHoverAnimation;
    QVariantAnimation m_heartHoverAnimation;
    QVariantAnimation m_heartStateAnimation;
    QVariantAnimation m_actionHoverAnimation;
    QVariantAnimation m_downloadCompletionAnimation;
    QVariantAnimation m_playbackTransitionAnimation;
    QTimer m_loadingTimer;
    QTimer m_equalizerTimer;
    bool m_playbackActive = false;
    bool m_viewVisible = false;
    SongListView::DownloadActionMode m_downloadMode = SongListView::DownloadAction;
};

} // namespace

SongListView::SongListView(QWidget *parent)
    : QTableView(parent)
{
    m_model = new SongListModel(this);
    auto *delegate = new SongRowDelegate(this);
    setModel(m_model);
    setItemDelegate(delegate);

    setShowGrid(false);
    setFrameShape(QFrame::NoFrame);
    setSelectionMode(QAbstractItemView::NoSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setCursor(Qt::PointingHandCursor);
    verticalHeader()->setVisible(false);
    verticalHeader()->setDefaultSectionSize(kSongRowHeight);
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    horizontalHeader()->setVisible(false);
    horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(7, QHeaderView::Fixed);
    setColumnWidth(0, 56);
    setColumnWidth(1, 104);
    setColumnWidth(3, 72);
    setColumnWidth(4, 220);
    setColumnWidth(5, 90);
    setColumnWidth(6, 64);
    setColumnWidth(7, 86);
    viewport()->setAutoFillBackground(false);
    setThemedStyleSheet(this, QStringLiteral("QTableView{background:@pageBackground;border:none;}"));

    m_pointerGuardTimer = new QTimer(this);
    m_pointerGuardTimer->setInterval(80);
    connect(m_pointerGuardTimer, &QTimer::timeout, this,
            &SongListView::verifyPointerStillOverViewport);
    connect(verticalScrollBar(), &QScrollBar::valueChanged,
            this, &SongListView::closeSourcePicker);

    setViewportMargins(0, 0, 0, 0);
    m_batchBar = new QWidget(this);
    m_batchBar->setObjectName(QStringLiteral("batchBar"));
    setThemedStyleSheet(m_batchBar, QStringLiteral("#batchBar{background:@surface;border-radius:10px;}"));
    auto *bar = new QHBoxLayout(m_batchBar);
    bar->setContentsMargins(8, 3, 8, 3);
    bar->setSpacing(6);
    auto makeBarButton = [this](const QString &text) {
        auto *button = new QPushButton(text, m_batchBar);
        button->setCursor(Qt::PointingHandCursor);
        setThemedStyleSheet(button, QStringLiteral(
            "QPushButton{border:none;background:@surfaceAlt;color:@textSecondary;"
            "padding:5px 11px;border-radius:14px;font-size:12px;}"
            "QPushButton:hover{background:@accentSoft;color:@accent;}"
            "QPushButton:disabled{color:@disabledText;background:@surface;}"));
        return button;
    };
    m_batchToggle = makeBarButton(QStringLiteral("批量操作"));
    m_batchToggle->setObjectName(QStringLiteral("batchToggle"));
    m_batchToggle->setAccessibleName(QStringLiteral("批量操作"));
    m_selectionSummary = new QLabel(QStringLiteral("已选择 0 首"), m_batchBar);
    m_selectionSummary->setObjectName(QStringLiteral("batchSelectionSummary"));
    setThemedStyleSheet(m_selectionSummary, QStringLiteral("color:@textSecondary;font-size:12px;padding:0 4px;"));
    m_selectAll = makeBarButton(QStringLiteral("全选"));
    m_selectAll->setObjectName(QStringLiteral("batchSelectAll"));
    m_clearSelection = makeBarButton(QStringLiteral("清空选择"));
    m_clearSelection->setObjectName(QStringLiteral("batchClearSelection"));
    m_favoriteSelected = makeBarButton(QStringLiteral("批量收藏"));
    m_favoriteSelected->setObjectName(QStringLiteral("batchFavorite"));
    m_unfavoriteSelected = makeBarButton(QStringLiteral("批量取消收藏"));
    m_unfavoriteSelected->setObjectName(QStringLiteral("batchUnfavorite"));
    m_addSelected = makeBarButton(QStringLiteral("添加到歌单"));
    m_addSelected->setObjectName(QStringLiteral("batchAddToPlaylist"));
    m_downloadSelected = makeBarButton(QStringLiteral("批量下载"));
    m_downloadSelected->setObjectName(QStringLiteral("batchDownload"));
    m_deleteSelected = new BatchDeleteButton(m_batchBar);
    m_deleteSelected->setText(QStringLiteral("按来源删除"));
    m_deleteSelected->setObjectName(QStringLiteral("batchDelete"));
    m_exitBatch = makeBarButton(QStringLiteral("完成"));
    m_exitBatch->setObjectName(QStringLiteral("batchDone"));
    m_moreSelected = new QToolButton(m_batchBar);
    m_moreSelected->setObjectName(QStringLiteral("batchMore"));
    m_moreSelected->setText(QStringLiteral("更多"));
    m_moreSelected->setCursor(Qt::PointingHandCursor);
    m_moreSelected->setPopupMode(QToolButton::InstantPopup);
    setThemedStyleSheet(m_moreSelected, QStringLiteral(
        "QToolButton{border:none;background:@surfaceAlt;color:@textSecondary;"
        "padding:5px 11px;border-radius:14px;font-size:12px;}"
        "QToolButton:hover{background:@accentSoft;color:@accent;}"
        "QToolButton:disabled{color:@disabledText;background:@surface;}"));
    bar->addWidget(m_batchToggle);
    bar->addWidget(m_selectionSummary);
    bar->addWidget(m_selectAll);
    bar->addWidget(m_clearSelection);
    bar->addWidget(m_favoriteSelected);
    bar->addWidget(m_unfavoriteSelected);
    bar->addWidget(m_addSelected);
    bar->addWidget(m_downloadSelected);
    bar->addWidget(m_deleteSelected);
    bar->addWidget(m_moreSelected);
    bar->addStretch(1);
    bar->addWidget(m_exitBatch);
    m_batchPlaylistMenu = new QMenu(m_addSelected);
    m_addSelected->setMenu(m_batchPlaylistMenu);
    m_batchMoreMenu = new QMenu(m_moreSelected);
    m_moreClear = m_batchMoreMenu->addAction(QStringLiteral("清空选择"));
    m_moreFavorite = m_batchMoreMenu->addAction(QStringLiteral("批量收藏"));
    m_moreUnfavorite = m_batchMoreMenu->addAction(QStringLiteral("批量取消收藏"));
    m_moreDownload = m_batchMoreMenu->addAction(QStringLiteral("批量下载"));
    m_moreDelete = m_batchMoreMenu->addAction(QStringLiteral("按来源删除"));
    m_moreSelected->setMenu(m_batchMoreMenu);
    connect(m_batchToggle, &QPushButton::clicked, this, [this] { setBatchMode(!m_batchMode); });
    connect(m_exitBatch, &QPushButton::clicked, this, [this] { setBatchMode(false); });
    connect(m_selectAll, &QPushButton::clicked, this, [this] {
        m_selectedIdentities.clear();
        for (int row = 0; row < m_model->rowCount(); ++row)
            m_selectedIdentities.insert(m_model->rowIdentityAt(row));
        m_model->setSelectedIdentities(m_selectedIdentities);
        updateBatchButtons();
    });
    connect(m_clearSelection, &QPushButton::clicked, this, [this] {
        m_selectedIdentities.clear();
        m_model->setSelectedIdentities(m_selectedIdentities);
        updateBatchButtons();
    });
    connect(m_favoriteSelected, &QPushButton::clicked, this, [this] {
        emit batchFavoriteRequested(selectedCollectionActionSongs(), true);
    });
    connect(m_unfavoriteSelected, &QPushButton::clicked, this, [this] {
        emit batchFavoriteRequested(selectedCollectionActionSongs(), false);
    });
    connect(m_downloadSelected, &QPushButton::clicked, this, [this] {
        emit batchDownloadRequested(selectedSongs());
    });
    connect(m_deleteSelected, &QPushButton::clicked, this, [this] {
        emit batchDeleteRequested(selectedSongs());
    });
    connect(m_moreClear, &QAction::triggered, m_clearSelection, &QPushButton::click);
    connect(m_moreFavorite, &QAction::triggered, m_favoriteSelected, &QPushButton::click);
    connect(m_moreUnfavorite, &QAction::triggered, m_unfavoriteSelected, &QPushButton::click);
    connect(m_moreDownload, &QAction::triggered, m_downloadSelected, &QPushButton::click);
    connect(m_moreDelete, &QAction::triggered, m_deleteSelected, &QPushButton::click);
    setBatchMode(false);

    m_batchBar->installEventFilter(this);
    for (QWidget *child : m_batchBar->findChildren<QWidget *>())
        child->installEventFilter(this);

}

void SongListView::setSongs(const QList<core::Song> &songs, qint64 playingId)
{
    closeSourcePicker();
    if (m_batchMode) {
        QSet<QString> available;
        for (const core::Song &song : songs)
            available.insert(song.selectionIdentity());
        m_selectedIdentities.intersect(available);
    } else {
        m_selectedIdentities.clear();
    }
    m_model->setSongs(songs, playingId);
    m_model->setSelectedIdentities(m_selectedIdentities);
    setDownloadingIdentities(m_downloadingIdentities);
    updateSafeAreaRows();
    updateBatchButtons();
}

void SongListView::setSearchResultGroups(const QList<core::SearchResultGroup> &groups,
                                         qint64 playingId)
{
    closeSourcePicker();
    if (m_batchMode) {
        QSet<QString> available;
        for (const core::SearchResultGroup &group : groups)
            available.insert(group.identity);
        m_selectedIdentities.intersect(available);
    } else {
        m_selectedIdentities.clear();
    }
    m_model->setSearchResultGroups(groups, playingId);
    m_model->setSelectedIdentities(m_selectedIdentities);
    setDownloadingIdentities(m_downloadingIdentities);
    updateSafeAreaRows();
    updateBatchButtons();
}

void SongListView::updateSafeAreaRows()
{
    const int count = m_model ? m_model->rowCount() : 0;
    for (int row = 0; row < count; ++row)
        setRowHeight(row, kSongRowHeight);
    if (count == 1) {
        setRowHeight(0, kSongRowHeight + kTopSafeAreaHeight + kBottomSafeAreaHeight);
    } else if (count > 1) {
        setRowHeight(0, kSongRowHeight + kTopSafeAreaHeight);
        setRowHeight(count - 1, kSongRowHeight + kBottomSafeAreaHeight);
    }
}

bool SongListView::pointHitsSongContent(const QModelIndex &index, const QPoint &point) const
{
    if (!index.isValid())
        return false;
    QRect content = visualRect(index);
    if (index.row() == 0)
        content.setTop(content.top() + kTopSafeAreaHeight);
    content.setHeight(kSongRowHeight);
    return content.contains(point);
}

void SongListView::showSourcePicker(int row, const QRect &cellRect)
{
    closeSourcePicker();
    if (row < 0 || row >= m_model->rowCount())
        return;

    auto *popup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
    popup->setObjectName(QStringLiteral("sourcePickerPopup"));
    popup->setAttribute(Qt::WA_DeleteOnClose);
    setThemedStyleSheet(popup, QStringLiteral(
        "QFrame#sourcePickerPopup{background:@surface;border:none;border-radius:10px;}"
        "QToolButton{background:transparent;border:none;border-radius:8px;padding:0;}"
        "QToolButton:hover{background:@surfaceAlt;}"
        "QToolButton:pressed{background:@surfacePressed;}"));
    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    const QList<SongSourceChoice> choices = m_model->sourceChoicesAt(row);
    const core::SourceId activeSource = m_model->activeSourceAt(row);
    constexpr core::SourceId sources[] = {
        core::SourceId::Local, core::SourceId::Netease, core::SourceId::QqMusic
    };
    int activeIndex = 0;
    for (int sourceIndex = 0; sourceIndex < 3; ++sourceIndex) {
        const core::SourceId source = sources[sourceIndex];
        bool available = false;
        bool visible = true;
        bool guest = false;
        bool found = false;
        QString unavailableReason = QStringLiteral("无此来源版本");
        for (const SongSourceChoice &choice : choices) {
            if (choice.source != source)
                continue;
            found = true;
            available = choice.available;
            visible = choice.visible;
            guest = choice.guest;
            unavailableReason = choice.unavailableReason;
            break;
        }
        if (found && !visible)
            continue;
        if (source == activeSource)
            activeIndex = sourceIndex;

        auto *button = new QToolButton(popup);
        button->setObjectName(QStringLiteral("sourceChoice_%1").arg(int(source)));
        button->setFixedSize(48, 48);
        button->setIcon(sourceIcon(source, available));
        button->setIconSize(QSize(34, 34));
        button->setCursor(available ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
        button->setAccessibleName(sourceDisplayName(source));
        button->setToolTip(available
                               ? QStringLiteral("%1%2").arg(
                                     sourceDisplayName(source),
                                     guest ? QStringLiteral(" · 游客试听") : QString())
                                     : QStringLiteral("%1 · %2")
                                           .arg(sourceDisplayName(source), unavailableReason));
        layout->addWidget(button);
        connect(button, &QToolButton::clicked, this,
                [this, row, source, available, unavailableReason, button] {
            if (!available) {
                QToolTip::showText(button->mapToGlobal(QPoint(button->width(), 0)),
                                   unavailableReason, button);
                return;
            }
            QString error;
            if (!m_model->activateSource(row, source, &error)) {
                QToolTip::showText(button->mapToGlobal(QPoint(button->width(), 0)),
                                   error, button);
                return;
            }
            const core::Song song = m_model->songAt(row);
            closeSourcePicker();
            emit sourceActivated(row, song);
            emit playRequested(row);
        });
    }

    popup->adjustSize();
    const QPoint cellCenter = viewport()->mapToGlobal(cellRect.center());
    const int buttonStride = 56;
    QPoint topLeft(cellCenter.x() - popup->width() / 2,
                   cellCenter.y() - 8 - activeIndex * buttonStride - 24);
    if (QScreen *screen = QGuiApplication::screenAt(cellCenter)) {
        const QRect available = screen->availableGeometry();
        topLeft.setX(qBound(available.left(), topLeft.x(),
                            available.right() - popup->width() + 1));
        topLeft.setY(qBound(available.top(), topLeft.y(),
                            available.bottom() - popup->height() + 1));
    }
    m_sourcePopup = popup;
    m_sourcePopupRow = row;
    connect(popup, &QObject::destroyed, this, [this, popup] {
        if (m_sourcePopup == popup) {
            m_sourcePopup = nullptr;
            m_sourcePopupRow = -1;
        }
    });
    popup->move(topLeft);
    popup->show();
}

void SongListView::closeSourcePicker()
{
    if (!m_sourcePopup)
        return;
    QWidget *popup = m_sourcePopup;
    m_sourcePopup = nullptr;
    m_sourcePopupRow = -1;
    popup->close();
}

bool SongListView::updateSong(const core::Song &song)
{
    return m_model->updateSong(song);
}

QList<core::Song> SongListView::songs() const
{
    return m_model->songs();
}

void SongListView::setSourceAccessStates(
    const QHash<int, core::SourceAccessState> &states)
{
    m_model->setSourceAccessStates(states);
}

QList<core::Song> SongListView::memberSongsAt(int row) const
{
    return m_model->memberSongsAt(row);
}

void SongListView::setPlayingId(qint64 playingId)
{
    const bool changed = m_model->playingId() != playingId;
    m_model->setPlayingId(playingId);
    if (changed && m_playbackActive) {
        if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate()))
            delegate->restartPlaybackTransition();
    }
}

void SongListView::setPlaybackActive(bool active)
{
    if (m_playbackActive == active)
        return;
    m_playbackActive = active;
    if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate()))
        delegate->setPlaybackActive(active);
}

void SongListView::setRemovable(bool removable)
{
    m_removable = removable;
}

void SongListView::setHighlightQuery(const QString &query)
{
    if (auto *d = static_cast<SongRowDelegate *>(itemDelegate()))
        d->setQuery(query);
    viewport()->update();
}

void SongListView::setPlaylistMenuItems(const QList<QPair<int, QString>> &items)
{
    m_playlistItems = items;
    rebuildBatchPlaylistMenu();
}

void SongListView::setFavoriteIds(const QSet<qint64> &ids)
{
    QSet<int> addedRows;
    QSet<int> removedRows;
    const QList<core::Song> currentSongs = m_model->songs();
    if (m_favoriteStateInitialized) {
        for (int row = 0; row < currentSongs.size(); ++row) {
            const qint64 id = currentSongs.at(row).id;
            if (!m_favoriteIds.contains(id) && ids.contains(id))
                addedRows.insert(row);
            else if (m_favoriteIds.contains(id) && !ids.contains(id))
                removedRows.insert(row);
        }
    }
    m_favoriteIds = ids;
    m_favoriteStateInitialized = true;
    m_model->setFavoriteIds(ids);
    if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate()))
        delegate->startFavoriteTransitions(addedRows, removedRows);
}

void SongListView::setDownloadingIdentities(const QSet<QString> &identities)
{
    QSet<int> loadingRows;
    QSet<int> completedRows;
    const QList<core::Song> currentSongs = m_model->songs();
    for (int row = 0; row < currentSongs.size(); ++row) {
        const core::Song &song = currentSongs.at(row);
        const QString identity = song.selectionIdentity();
        if (identities.contains(identity))
            loadingRows.insert(row);
        else if (m_downloadingIdentities.contains(identity) && song.isDownloaded())
            completedRows.insert(row);
    }
    m_downloadingIdentities = identities;
    m_model->setDownloadingIdentities(identities);
    if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate())) {
        delegate->setLoadingRows(loadingRows);
        delegate->startDownloadCompletion(completedRows);
    }
}

void SongListView::refreshLibraryState(core::LibraryService *library)
{
    if (!library)
        return;
    QList<core::Song> songs = m_model->songs();
    bool changed = false;
    for (core::Song &song : songs) {
        const core::Song stored = library->songById(song.id);
        if (stored.id <= 0)
            continue;
        if (song.cachePath != stored.cachePath || song.downloadPath != stored.downloadPath
            || song.coverPath != stored.coverPath || song.lyricPath != stored.lyricPath) {
            song.cachePath = stored.cachePath;
            song.downloadPath = stored.downloadPath;
            song.coverPath = stored.coverPath;
            song.lyricPath = stored.lyricPath;
            changed = true;
        }
    }
    if (changed)
        m_model->refreshSongs(songs);
}

void SongListView::setDownloadActionMode(DownloadActionMode mode)
{
    m_downloadMode = mode;
    if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate()))
        delegate->setDownloadMode(mode);
    m_deleteSelected->setText(mode == DeleteDownloadAction ? QStringLiteral("批量删除下载")
                                                            : QStringLiteral("按来源删除"));
    m_deleteSelected->setAccessibleName(m_deleteSelected->text());
    m_moreDelete->setText(m_deleteSelected->text());
    updateBatchButtons();
    viewport()->update();
}

QList<core::Song> SongListView::selectedSongs() const
{
    QList<core::Song> result;
    for (int row = 0; row < m_model->rowCount(); ++row)
        if (m_selectedIdentities.contains(m_model->rowIdentityAt(row)))
            result.append(m_model->songAt(row));
    return result;
}

void SongListView::setPlayingSong(const core::Song &song)
{
    const bool changed = m_model->playingId() != song.id;
    m_model->setPlayingSong(song);
    if (changed || !song.stableIdentity().isEmpty())
        viewport()->update();
}

QList<core::Song> SongListView::selectedMemberSongs() const
{
    QList<core::Song> result;
    QSet<QString> seen;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        if (!m_selectedIdentities.contains(m_model->rowIdentityAt(row)))
            continue;
        for (const core::Song &song : m_model->memberSongsAt(row)) {
            const QString identity = song.selectionIdentity();
            if (seen.contains(identity))
                continue;
            seen.insert(identity);
            result.append(song);
        }
    }
    return result;
}

QList<core::Song> SongListView::selectedCollectionActionSongs() const
{
    return m_mergedCollectionActions ? selectedMemberSongs() : selectedSongs();
}

int SongListView::hoveredRow() const
{
    if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate()))
        return delegate->hoverRow();
    return -1;
}

void SongListView::setBatchMode(bool enabled)
{
    m_batchMode = enabled;
    m_batchBar->setVisible(true);
    setViewportMargins(0, 0, 0, 0);
    m_model->setBatchMode(enabled);
    if (!enabled) {
        m_selectedIdentities.clear();
        m_model->setSelectedIdentities(m_selectedIdentities);
    }
    updateBatchButtons();
    viewport()->update();
}

void SongListView::updateBatchButtons()
{
    const QList<core::Song> selection = selectedSongs();
    const bool hasSelection = !selection.isEmpty();
    bool hasDownloadable = false;
    for (const core::Song &song : selection) {
        if (song.isOnline() && !song.isDownloaded()) {
            hasDownloadable = true;
            break;
        }
    }
    m_selectionSummary->setText(QStringLiteral("已选择 %1 首").arg(selection.size()));
    m_selectAll->setEnabled(m_model->rowCount() > 0);
    m_clearSelection->setEnabled(hasSelection);
    m_favoriteSelected->setEnabled(hasSelection);
    m_unfavoriteSelected->setEnabled(hasSelection);
    // 即使当前还没有已有歌单，也可以从菜单创建新歌单。
    m_addSelected->setEnabled(hasSelection);
    m_downloadSelected->setEnabled(hasDownloadable);
    m_deleteSelected->setEnabled(hasSelection);
    m_moreClear->setEnabled(hasSelection);
    m_moreFavorite->setEnabled(hasSelection);
    m_moreUnfavorite->setEnabled(hasSelection);
    m_moreDownload->setEnabled(hasDownloadable);
    m_moreDelete->setEnabled(hasSelection);
    updateBatchLayout();
}

void SongListView::updateBatchLayout()
{
    const bool compact = width() < 1050;
    const bool veryCompact = width() < 760;
    m_batchBar->setVisible(true);
    m_batchToggle->setVisible(!m_batchMode);
    m_selectionSummary->setVisible(m_batchMode);
    m_selectAll->setVisible(m_batchMode);
    m_clearSelection->setVisible(m_batchMode && !veryCompact);
    m_favoriteSelected->setVisible(m_batchMode && !veryCompact);
    m_unfavoriteSelected->setVisible(m_batchMode && !compact);
    m_addSelected->setVisible(m_batchMode);
    m_downloadSelected->setVisible(m_batchMode && !compact && m_downloadMode == DownloadAction);
    m_deleteSelected->setVisible(m_batchMode && !compact);
    m_moreSelected->setVisible(m_batchMode && compact);
    m_exitBatch->setVisible(m_batchMode);
    m_moreClear->setVisible(veryCompact);
    m_moreFavorite->setVisible(veryCompact);
    m_moreUnfavorite->setVisible(compact);
    m_moreDownload->setVisible(compact && m_downloadMode == DownloadAction);
    m_moreDelete->setVisible(compact);
}

void SongListView::rebuildBatchPlaylistMenu()
{
    m_batchPlaylistMenu->clear();
    QAction *newPlaylist = m_batchPlaylistMenu->addAction(QStringLiteral("新建歌单…"));
    connect(newPlaylist, &QAction::triggered, this, [this] {
        emit batchCreatePlaylistRequested(selectedCollectionActionSongs());
    });
    if (!m_playlistItems.isEmpty())
        m_batchPlaylistMenu->addSeparator();
    for (const auto &item : m_playlistItems) {
        QAction *action = m_batchPlaylistMenu->addAction(item.second);
        connect(action, &QAction::triggered, this, [this, id = item.first] {
            emit batchAddToPlaylistRequested(selectedCollectionActionSongs(), id);
        });
    }
}

void SongListView::toggleRowSelection(int row)
{
    if (row < 0 || row >= m_model->rowCount())
        return;
    const QString identity = m_model->rowIdentityAt(row);
    if (m_selectedIdentities.contains(identity))
        m_selectedIdentities.remove(identity);
    else
        m_selectedIdentities.insert(identity);
    m_model->setSelectedIdentities(m_selectedIdentities);
    updateBatchButtons();
}

void SongListView::mouseMoveEvent(QMouseEvent *event)
{
    const QModelIndex index = indexAt(event->pos());
    const bool overSong = pointHitsSongContent(index, event->pos());
    const int row = overSong ? index.row() : -1;
    if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate()))
        delegate->setPointerIndex(row, overSong ? index.column() : -1);
    if (row >= 0)
        m_pointerGuardTimer->start();
    else
        m_pointerGuardTimer->stop();
    QTableView::mouseMoveEvent(event);
}

bool SongListView::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_batchBar || (m_batchBar && m_batchBar->isAncestorOf(qobject_cast<QWidget *>(watched))))
        && (event->type() == QEvent::Enter || event->type() == QEvent::HoverEnter)) {
        resetPointerState();
    }
    return QTableView::eventFilter(watched, event);
}

bool SongListView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::Leave || event->type() == QEvent::Hide)
        resetPointerState();
    return QTableView::viewportEvent(event);
}

void SongListView::resetPointerState()
{
    if (m_pointerGuardTimer)
        m_pointerGuardTimer->stop();
    m_pendingPlayRow = -1;
    if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate())) {
        delegate->setPointerIndex(-1, -1);
        delegate->setPressedHeartRow(-1);
        delegate->setPressedActionRow(-1);
        delegate->setPressedPlayRow(-1);
    }
    // A rapid A -> B -> leave sequence can interrupt the row transition before
    // its old source row receives the final frame. Repaint the visible viewport
    // once on pointer reset so no cached red frame survives the transition.
    viewport()->update();
}

void SongListView::verifyPointerStillOverViewport()
{
    if (hoveredRow() < 0) {
        m_pointerGuardTimer->stop();
        return;
    }

    const QPoint globalPos = QCursor::pos();
    QWidget *underPointer = QApplication::widgetAt(globalPos);
    const bool overViewportWidget = underPointer
        && (underPointer == viewport() || viewport()->isAncestorOf(underPointer));
    const QRect globalViewportRect(viewport()->mapToGlobal(QPoint(0, 0)), viewport()->size());
    if (!isVisible() || !globalViewportRect.contains(globalPos)
        || (underPointer && !overViewportWidget)) {
        resetPointerState();
    }
}

void SongListView::leaveEvent(QEvent *event)
{
    resetPointerState();
    QTableView::leaveEvent(event);
}

void SongListView::mousePressEvent(QMouseEvent *event)
{
    m_pendingPlayRow = -1;
    if (event->button() == Qt::LeftButton) {
        const QModelIndex idx = indexAt(event->pos());
        if (pointHitsSongContent(idx, event->pos())) {
            if (m_batchMode && idx.column() == 0) {
                toggleRowSelection(idx.row());
                return;
            }
            if (idx.column() == 3 && !m_batchMode) {
                showSourcePicker(idx.row(), visualRect(idx));
                event->accept();
                return;
            }
            if (idx.column() == 6) {
                if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate()))
                    delegate->setPressedHeartRow(idx.row());
                emit heartRequested(idx.row());
                return;
            }
            if (idx.column() == 7) {
                const core::Song song = m_model->songAt(idx.row());
                if (idx.data(SongListModel::DownloadingRole).toBool())
                    return;
                const bool deleteAction = song.isDownloaded()
                    && (m_downloadMode == DeleteDownloadAction || m_downloadMode == DownloadAction);
                if (deleteAction) {
                    if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate()))
                        delegate->setPressedActionRow(idx.row());
                    emit deleteDownloadRequested(idx.row());
                } else if (m_downloadMode == DownloadAction && song.isOnline() && !song.isDownloaded()) {
                    if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate()))
                        delegate->setPressedActionRow(idx.row());
                    emit downloadRequested(idx.row());
                }
                return;
            }
            const bool playColumn = idx.column() == 1 || idx.column() == 2
                || idx.column() == 4 || idx.column() == 5;
            if (!m_batchMode && playColumn) {
                m_pendingPlayRow = idx.row();
                if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate()))
                    delegate->setPressedPlayRow(idx.row());
                event->accept();
                return;
            }
        }
    }
    QTableView::mousePressEvent(event);
}

void SongListView::mouseReleaseEvent(QMouseEvent *event)
{
    const int pendingRow = m_pendingPlayRow;
    m_pendingPlayRow = -1;
    if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate())) {
        delegate->setPressedHeartRow(-1);
        delegate->setPressedActionRow(-1);
        delegate->setPressedPlayRow(-1);
    }
    if (event->button() == Qt::LeftButton && pendingRow >= 0 && !m_batchMode) {
        const QModelIndex released = indexAt(event->pos());
        const bool playColumn = released.column() == 1 || released.column() == 2
            || released.column() == 4 || released.column() == 5;
        if (pointHitsSongContent(released, event->pos()) && released.row() == pendingRow
            && playColumn) {
            emit playRequested(pendingRow);
            event->accept();
            return;
        }
    }
    QTableView::mouseReleaseEvent(event);
}

void SongListView::mouseDoubleClickEvent(QMouseEvent *event)
{
    m_pendingPlayRow = -1;
    if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate()))
        delegate->setPressedPlayRow(-1);
    event->accept();
}

void SongListView::resizeEvent(QResizeEvent *event)
{
    closeSourcePicker();
    QTableView::resizeEvent(event);
    const int availableWidth = viewport()->width();
    setColumnHidden(4, availableWidth < 760);
    setColumnWidth(1, availableWidth < 900 ? 88 : 104);
    setColumnWidth(4, availableWidth < 1050 ? 140 : 220);
    setColumnWidth(5, availableWidth < 900 ? 70 : 90);
    if (m_batchBar) {
        m_batchBar->setGeometry(0, 0, width(), 38);
        updateBatchLayout();
    }
}

void SongListView::showEvent(QShowEvent *event)
{
    QTableView::showEvent(event);
    if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate()))
        delegate->setViewVisible(true);
}

void SongListView::hideEvent(QHideEvent *event)
{
    closeSourcePicker();
    resetPointerState();
    if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate()))
        delegate->setViewVisible(false);
    QTableView::hideEvent(event);
}

void SongListView::contextMenuEvent(QContextMenuEvent *event)
{
    const QModelIndex idx = indexAt(event->pos());
    if (!pointHitsSongContent(idx, event->pos()))
        return;
    m_contextRow = idx.row();

    delete m_menu;
    m_menu = new QMenu(this);
    m_menu->addAction(QStringLiteral("播放"), this, [this] { emit playRequested(m_contextRow); });
    m_menu->addAction(QStringLiteral("喜欢"), this, [this] { emit heartRequested(m_contextRow); });

    const core::Song contextSong = m_model->songAt(m_contextRow);
    if (contextSong.isOnline() && !contextSong.isDownloaded()) {
        m_menu->addAction(QStringLiteral("下载到本地"), this, [this] { emit downloadRequested(m_contextRow); });
    }

    if (!m_playlistItems.isEmpty()) {
        auto *addMenu = m_menu->addMenu(QStringLiteral("添加到歌单"));
        for (const auto &item : m_playlistItems) {
            addMenu->addAction(item.second, this, [this, id = item.first] {
                emit addToPlaylistRequested(m_contextRow, id);
            });
        }
    }
    if (m_removable)
        m_menu->addAction(QStringLiteral("从歌单移除"), this, [this] { emit removeFromPlaylistRequested(m_contextRow); });
    const QString deleteText = !contextSong.isOnline() ? QStringLiteral("删除本地导入歌曲")
        : contextSong.isDownloaded() ? QStringLiteral("删除下载")
        : contextSong.isCached() ? QStringLiteral("删除缓存") : QStringLiteral("移除在线记录");
    m_menu->addAction(deleteText, this, [this] { emit deleteFromLibraryRequested(m_contextRow); });
    m_menu->exec(event->globalPos());
}

} // namespace ui
