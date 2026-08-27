#include "SongListView.h"

#include "core/SearchService.h"
#include "ui/CoverProvider.h"
#include "ui/SongListModel.h"

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>

namespace ui {
namespace {

const QColor kText(0xE8, 0xE8, 0xE8);
const QColor kText2(0x9A, 0x9A, 0xA5);
const QColor kText3(0x6E, 0x6E, 0x7A);
const QColor kHover(255, 255, 255, 16);
const QColor kPlaying(236, 65, 65, 36);
const QColor kPrimary(0xEC, 0x41, 0x41);

QString formatDuration(qint64 ms)
{
    const qint64 total = qMax<qint64>(0, ms) / 1000;
    return QStringLiteral("%1:%2").arg(total / 60, 2, 10, QLatin1Char('0')).arg(total % 60, 2, 10, QLatin1Char('0'));
}

void drawHighlightedText(QPainter &p, const QRectF &rect, const QString &text, const QString &query,
                         const QColor &normal, Qt::Alignment align)
{
    p.save();
    p.setPen(normal);
    const QFontMetrics fm = p.fontMetrics();
    QString elided = fm.elidedText(text, Qt::ElideRight, int(rect.width()));
    const QList<QPair<int, int>> ranges = core::SearchService::highlightRanges(elided, query);
    if (ranges.isEmpty()) {
        p.drawText(rect, int(align) | Qt::AlignVCenter, elided);
        p.restore();
        return;
    }
    const auto r = ranges.constFirst();
    const QString before = elided.left(r.first);
    const QString match = elided.mid(r.first, r.second - r.first);
    const QString after = elided.mid(r.second);
    const int wBefore = fm.horizontalAdvance(before);
    const int wMatch = fm.horizontalAdvance(match);
    QRectF base = rect;
    if (align & Qt::AlignRight)
        base.setLeft(base.right() - fm.horizontalAdvance(elided));
    p.drawText(QRectF(base.left(), base.top(), wBefore, base.height()), Qt::AlignVCenter, before);
    QRectF matchRect(base.left() + wBefore, base.top(), wMatch, base.height());
    p.fillRect(matchRect.adjusted(0, 2, 0, -2), QColor(236, 65, 65, 80));
    p.setPen(kPrimary);
    p.drawText(matchRect, Qt::AlignVCenter, match);
    p.setPen(normal);
    p.drawText(QRectF(matchRect.right(), base.top(), base.width() - wBefore - wMatch, base.height()),
               Qt::AlignVCenter, after);
    p.restore();
}

class SongRowDelegate : public QStyledItemDelegate
{
public:
    explicit SongRowDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool playing = index.data(SongListModel::IsPlayingRole).toBool();
        const bool hover = option.state & QStyle::State_MouseOver;
        QColor bg;
        if (playing)
            bg = kPlaying;
        else if (hover)
            bg = kHover;
        if (bg.isValid()) {
            painter->save();
            painter->fillRect(option.rect, bg);
            painter->restore();
        }

        const int col = index.column();
        QRectF rect = option.rect.adjusted(0, 0, 0, -1);
        if (col == 0) {
            painter->save();
            if (playing) {
                const QPointF c = rect.center() + QPointF(1, 0);
                QPainterPath path;
                path.moveTo(c.x() - 4, c.y() - 5);
                path.lineTo(c.x() - 4, c.y() + 5);
                path.lineTo(c.x() + 5, c.y());
                path.closeSubpath();
                painter->setPen(Qt::NoPen);
                painter->setBrush(kPrimary);
                painter->drawPath(path);
            } else {
                painter->setPen(kText3);
                painter->drawText(rect, Qt::AlignCenter, QString::number(index.row() + 1));
            }
            painter->restore();
            return;
        }

        const core::Song song = index.data(SongListModel::SongRole).value<core::Song>();
        const QString query = m_query;
        if (col == 1) {
            const QRectF coverRect(rect.left() + 6, rect.center().y() - 18, 36, 36);
            const QPixmap cover = CoverProvider::coverFor(song, 36, 4);
            painter->drawPixmap(coverRect.toRect(), cover);
            qreal textX = rect.left() + 50;
            if (song.isOnline()) {
                static const QIcon cloud(QStringLiteral(":/icons/icon-cloud.svg"));
                const QRectF iconRect(rect.left() + 48, rect.center().y() - 7, 14, 14);
                cloud.paint(painter, iconRect.toRect());
                if (song.isCached()) {
                    static const QIcon check(QStringLiteral(":/icons/icon-check.svg"));
                    check.paint(painter, QRect(rect.left() + 57, rect.center().y() - 11, 9, 9));
                }
                textX = rect.left() + 68;
            }
            QRectF textRect(textX, rect.top(), rect.width() - (textX - rect.left()) - 6, rect.height());
            painter->save();
            painter->setFont(m_titleFont);
            const QString titleText = song.missing ? song.title + QStringLiteral(" · 失效") : song.title;
            drawHighlightedText(*painter, textRect, titleText, query, playing ? kPrimary : (song.missing ? kText3 : kText), Qt::AlignLeft);
            painter->restore();
        } else if (col == 2) {
            painter->save();
            painter->setFont(m_baseFont);
            drawHighlightedText(*painter, rect.adjusted(10, 0, 0, 0), song.artist, query, kText2, Qt::AlignLeft);
            painter->restore();
        } else if (col == 3) {
            painter->save();
            painter->setFont(m_baseFont);
            drawHighlightedText(*painter, rect.adjusted(10, 0, 0, 0), song.album, query, kText3, Qt::AlignLeft);
            painter->restore();
        } else {
            painter->save();
            painter->setFont(m_baseFont);
            painter->setPen(kText3);
            painter->drawText(rect.adjusted(0, 0, -12, 0), Qt::AlignRight | Qt::AlignVCenter,
                              formatDuration(song.durationMs));
            painter->restore();
        }
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return { 0, 48 };
    }

    void setQuery(const QString &query)
    {
        m_query = query;
    }

private:
    QFont m_baseFont = QFont(QStringLiteral("Microsoft YaHei UI"), 9);
    QFont m_titleFont = QFont(QStringLiteral("Microsoft YaHei UI"), 9);
    QString m_query;
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
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setCursor(Qt::PointingHandCursor);
    verticalHeader()->setVisible(false);
    horizontalHeader()->setVisible(false);
    horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    setColumnWidth(0, 56);
    setColumnWidth(2, 200);
    setColumnWidth(3, 180);
    setColumnWidth(4, 70);
    setStyleSheet(QStringLiteral("QTableView{background:transparent;border:none;}"));

    connect(this, &QTableView::doubleClicked, this, [this](const QModelIndex &idx) {
        if (idx.isValid())
            emit playRequested(idx.row());
    });
}

void SongListView::setSongs(const QList<core::Song> &songs, qint64 playingId)
{
    m_model->setSongs(songs, playingId);
}

QList<core::Song> SongListView::songs() const
{
    return m_model->songs();
}

void SongListView::setPlayingId(qint64 playingId)
{
    m_model->setPlayingId(playingId);
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
}

void SongListView::contextMenuEvent(QContextMenuEvent *event)
{
    const QModelIndex idx = indexAt(event->pos());
    if (!idx.isValid())
        return;
    m_contextRow = idx.row();

    delete m_menu;
    m_menu = new QMenu(this);
    m_menu->addAction(QStringLiteral("播放"), this, [this] { emit playRequested(m_contextRow); });
    m_menu->addAction(QStringLiteral("喜欢"), this, [this] { emit heartRequested(m_contextRow); });

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
    m_menu->addAction(QStringLiteral("从音乐库删除"), this, [this] { emit deleteFromLibraryRequested(m_contextRow); });
    m_menu->exec(event->globalPos());
}

} // namespace ui
