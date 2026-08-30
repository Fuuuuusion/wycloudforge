#include "SongListView.h"

#include "core/LibraryService.h"
#include "core/SearchService.h"
#include "ui/CoverProvider.h"
#include "ui/SongListModel.h"
#include "ui/SvgIcon.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QIcon>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

namespace ui {
namespace {

const QColor kText(0xE8, 0xE8, 0xE8);
const QColor kText2(0x9A, 0x9A, 0xA5);
const QColor kText3(0x6E, 0x6E, 0x7A);
const QColor kPrimary(0xEC, 0x41, 0x41);
QBrush activeTextBrush(const QRectF &)
{
    return QBrush(kPrimary);
}

QPixmap tintedIcon(const QString &path, int size, const QColor &color)
{
    const QPixmap source = makeSvgIcon(path, size).pixmap(size, size);
    if (source.isNull())
        return {};
    QPixmap result(source.size());
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.drawPixmap(0, 0, source);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(result.rect(), color);
    return result;
}

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
    p.setPen(QPen(normal, 1));
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
        const bool hover = index.row() == m_hoverRow;
        const bool active = playing || hover;

        const int col = index.column();
        QRectF rect = option.rect.adjusted(0, 0, 0, -1);
        if (hover)
            rect.translate(0, -2);
        if (col == 0) {
            painter->save();
            const bool batch = index.data(SongListModel::BatchModeRole).toBool();
            if (batch) {
                const QRectF box(rect.center().x() - 8, rect.center().y() - 8, 16, 16);
                painter->setPen(QPen(index.data(SongListModel::SelectedRole).toBool()
                                         ? kPrimary : QColor(0x6E, 0x6E, 0x7A), 1.2));
                painter->setBrush(index.data(SongListModel::SelectedRole).toBool()
                                      ? kPrimary : Qt::NoBrush);
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
                QPainterPath path;
                path.moveTo(c.x() - 4, c.y() - 5);
                path.lineTo(c.x() - 4, c.y() + 5);
                path.lineTo(c.x() + 5, c.y());
                path.closeSubpath();
                painter->setPen(Qt::NoPen);
                painter->setBrush(kPrimary);
                painter->drawPath(path);
            } else {
                painter->setPen(active ? QPen(activeTextBrush(rect), 1) : QPen(kText3, 1));
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
                const QRectF iconRect(rect.left() + 48, rect.center().y() - 7, 14, 14);
                const QPixmap cloud = tintedIcon(QStringLiteral(":/icons/icon-cloud.svg"), 14,
                                                 active ? kPrimary : QColor(0xB8, 0xB8, 0xC4));
                painter->drawPixmap(iconRect.toRect(), cloud);
                if (song.isCached()) {
                    const QPixmap check = tintedIcon(QStringLiteral(":/icons/icon-check.svg"), 9,
                                                     active ? kPrimary : QColor(0xB8, 0xB8, 0xC4));
                    painter->drawPixmap(QRect(rect.left() + 57, rect.center().y() - 11, 9, 9), check);
                }
                textX = rect.left() + 68;
            }
            QRectF textRect(textX, rect.top(), rect.width() - (textX - rect.left()) - 6, rect.height());
            painter->save();
            painter->setFont(m_titleFont);
            const QString titleText = song.missing ? song.title + QStringLiteral(" · 失效") : song.title;
            const QBrush titleBrush = active ? activeTextBrush(textRect)
                                             : QBrush(song.missing ? kText3 : kText);
            drawHighlightedText(*painter, textRect, titleText, query, titleBrush, Qt::AlignLeft);
            painter->restore();
        } else if (col == 2) {
            painter->save();
            painter->setFont(m_baseFont);
            const QRectF textRect = rect.adjusted(10, 0, 0, 0);
            drawHighlightedText(*painter, textRect, song.artist, query,
                                active ? activeTextBrush(textRect) : QBrush(kText2), Qt::AlignLeft);
            painter->restore();
        } else if (col == 3) {
            painter->save();
            painter->setFont(m_baseFont);
            const QRectF textRect = rect.adjusted(10, 0, 0, 0);
            drawHighlightedText(*painter, textRect, song.album, query,
                                active ? activeTextBrush(textRect) : QBrush(kText3), Qt::AlignLeft);
            painter->restore();
        } else if (col == 4) {
            painter->save();
            painter->setFont(m_baseFont);
            painter->setPen(active ? QPen(activeTextBrush(rect), 1) : QPen(kText3, 1));
            painter->drawText(rect.adjusted(0, 0, -12, 0), Qt::AlignRight | Qt::AlignVCenter,
                              formatDuration(song.durationMs));
            painter->restore();
        } else if (col == 5) {
            const bool favorite = index.data(SongListModel::FavoriteRole).toBool();
            const QPixmap heart = tintedIcon(favorite ? QStringLiteral(":/icons/icon-heart-fill.svg")
                                                       : QStringLiteral(":/icons/icon-heart.svg"),
                                             18, favorite ? kPrimary : kText3);
            painter->drawPixmap(QRectF(rect.center().x() - 9, rect.center().y() - 9, 18, 18).toRect(), heart);
        } else if (col == 6) {
            painter->save();
            painter->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 8));
            if (m_downloadMode == SongListView::DeleteDownloadAction && song.isDownloaded()) {
                painter->setPen(kPrimary);
                painter->drawText(rect, Qt::AlignCenter, QStringLiteral("删除下载"));
            } else if (m_downloadMode == SongListView::DownloadAction && song.isOnline()) {
                painter->setPen(song.isDownloaded() ? kText3 : kPrimary);
                painter->drawText(rect, Qt::AlignCenter,
                                  song.isDownloaded() ? QStringLiteral("已下载") : QStringLiteral("未下载"));
            }
            painter->restore();
        }
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return { 0, 64 };
    }

    void setQuery(const QString &query)
    {
        m_query = query;
    }

    void setHoverRow(int row)
    {
        m_hoverRow = row;
    }

    int hoverRow() const
    {
        return m_hoverRow;
    }

    void setDownloadMode(SongListView::DownloadActionMode mode)
    {
        m_downloadMode = mode;
    }

private:
    QFont m_baseFont = QFont(QStringLiteral("Microsoft YaHei UI"), 9);
    QFont m_titleFont = QFont(QStringLiteral("Microsoft YaHei UI"), 9);
    QString m_query;
    int m_hoverRow = -1;
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
    verticalHeader()->setDefaultSectionSize(64);
    horizontalHeader()->setVisible(false);
    horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);
    setColumnWidth(0, 56);
    setColumnWidth(2, 200);
    setColumnWidth(3, 180);
    setColumnWidth(4, 70);
    setColumnWidth(5, 44);
    setColumnWidth(6, 86);
    viewport()->setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("QTableView{background:#0E0E14;border:none;}"));

    setViewportMargins(0, 42, 0, 0);
    m_batchBar = new QWidget(this);
    m_batchBar->setObjectName(QStringLiteral("batchBar"));
    m_batchBar->setStyleSheet(QStringLiteral("#batchBar{background:#16161E;border-radius:10px;}"));
    auto *bar = new QHBoxLayout(m_batchBar);
    bar->setContentsMargins(8, 3, 8, 3);
    bar->setSpacing(6);
    auto makeBarButton = [this](const QString &text) {
        auto *button = new QPushButton(text, m_batchBar);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral(
            "QPushButton{border:none;background:#1B1B24;color:#C8C8D0;"
            "padding:5px 11px;border-radius:14px;font-size:12px;}"
            "QPushButton:hover{background:#3A2024;color:#EC4141;}"
            "QPushButton:disabled{color:#555563;background:#16161E;}"));
        return button;
    };
    m_batchToggle = makeBarButton(QStringLiteral("批量操作"));
    m_batchToggle->setObjectName(QStringLiteral("batchToggle"));
    m_batchToggle->setAccessibleName(QStringLiteral("批量操作"));
    m_selectionSummary = new QLabel(QStringLiteral("已选择 0 首"), m_batchBar);
    m_selectionSummary->setObjectName(QStringLiteral("batchSelectionSummary"));
    m_selectionSummary->setStyleSheet(QStringLiteral("color:#C8C8D0;font-size:12px;padding:0 4px;"));
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
    m_deleteSelected = makeBarButton(QStringLiteral("按来源删除"));
    m_deleteSelected->setObjectName(QStringLiteral("batchDelete"));
    m_exitBatch = makeBarButton(QStringLiteral("完成"));
    m_exitBatch->setObjectName(QStringLiteral("batchDone"));
    m_moreSelected = new QToolButton(m_batchBar);
    m_moreSelected->setObjectName(QStringLiteral("batchMore"));
    m_moreSelected->setText(QStringLiteral("更多"));
    m_moreSelected->setCursor(Qt::PointingHandCursor);
    m_moreSelected->setPopupMode(QToolButton::InstantPopup);
    m_moreSelected->setStyleSheet(QStringLiteral(
        "QToolButton{border:none;background:#1B1B24;color:#C8C8D0;"
        "padding:5px 11px;border-radius:14px;font-size:12px;}"
        "QToolButton:hover{background:#3A2024;color:#EC4141;}"
        "QToolButton:disabled{color:#555563;background:#16161E;}"));
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
        for (const core::Song &song : m_model->songs())
            m_selectedIdentities.insert(song.selectionIdentity());
        m_model->setSelectedIdentities(m_selectedIdentities);
        updateBatchButtons();
    });
    connect(m_clearSelection, &QPushButton::clicked, this, [this] {
        m_selectedIdentities.clear();
        m_model->setSelectedIdentities(m_selectedIdentities);
        updateBatchButtons();
    });
    connect(m_favoriteSelected, &QPushButton::clicked, this, [this] {
        emit batchFavoriteRequested(selectedSongs(), true);
    });
    connect(m_unfavoriteSelected, &QPushButton::clicked, this, [this] {
        emit batchFavoriteRequested(selectedSongs(), false);
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

    connect(this, &QTableView::doubleClicked, this, [this](const QModelIndex &idx) {
        if (idx.isValid())
            emit playRequested(idx.row());
    });
}

void SongListView::setSongs(const QList<core::Song> &songs, qint64 playingId)
{
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
    updateBatchButtons();
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
    rebuildBatchPlaylistMenu();
}

void SongListView::setFavoriteIds(const QSet<qint64> &ids)
{
    m_favoriteIds = ids;
    m_model->setFavoriteIds(ids);
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
    m_moreDelete->setText(m_deleteSelected->text());
    updateBatchButtons();
    viewport()->update();
}

QList<core::Song> SongListView::selectedSongs() const
{
    QList<core::Song> result;
    for (int row = 0; row < m_model->rowCount(); ++row)
        if (m_selectedIdentities.contains(m_model->songAt(row).selectionIdentity()))
            result.append(m_model->songAt(row));
    return result;
}

void SongListView::setBatchMode(bool enabled)
{
    m_batchMode = enabled;
    m_batchBar->setVisible(true);
    setViewportMargins(0, 42, 0, 0);
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
        emit batchCreatePlaylistRequested(selectedSongs());
    });
    if (!m_playlistItems.isEmpty())
        m_batchPlaylistMenu->addSeparator();
    for (const auto &item : m_playlistItems) {
        QAction *action = m_batchPlaylistMenu->addAction(item.second);
        connect(action, &QAction::triggered, this, [this, id = item.first] {
            emit batchAddToPlaylistRequested(selectedSongs(), id);
        });
    }
}

void SongListView::toggleRowSelection(int row)
{
    if (row < 0 || row >= m_model->rowCount())
        return;
    const QString identity = m_model->songAt(row).selectionIdentity();
    if (m_selectedIdentities.contains(identity))
        m_selectedIdentities.remove(identity);
    else
        m_selectedIdentities.insert(identity);
    m_model->setSelectedIdentities(m_selectedIdentities);
    updateBatchButtons();
}

void SongListView::mouseMoveEvent(QMouseEvent *event)
{
    const int row = indexAt(event->pos()).row();
    if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate())) {
        if (delegate->hoverRow() != row) {
            delegate->setHoverRow(row);
            viewport()->update();
        }
    }
    QTableView::mouseMoveEvent(event);
}

void SongListView::leaveEvent(QEvent *event)
{
    if (auto *delegate = static_cast<SongRowDelegate *>(itemDelegate())) {
        if (delegate->hoverRow() != -1) {
            delegate->setHoverRow(-1);
            viewport()->update();
        }
    }
    QTableView::leaveEvent(event);
}

void SongListView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QModelIndex idx = indexAt(event->pos());
        if (idx.isValid()) {
            if (m_batchMode && idx.column() == 0) {
                toggleRowSelection(idx.row());
                return;
            }
            if (idx.column() == 5) {
                emit heartRequested(idx.row());
                return;
            }
            if (idx.column() == 6) {
                const core::Song song = m_model->songAt(idx.row());
                if (m_downloadMode == DeleteDownloadAction && song.isDownloaded())
                    emit deleteDownloadRequested(idx.row());
                else if (m_downloadMode == DownloadAction && song.isOnline() && !song.isDownloaded())
                    emit downloadRequested(idx.row());
                return;
            }
        }
    }
    QTableView::mousePressEvent(event);
}

void SongListView::resizeEvent(QResizeEvent *event)
{
    QTableView::resizeEvent(event);
    if (m_batchBar) {
        m_batchBar->setGeometry(0, 0, width(), 38);
        updateBatchLayout();
    }
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
