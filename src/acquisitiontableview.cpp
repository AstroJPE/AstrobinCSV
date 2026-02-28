#include "acquisitiontableview.h"
#include "models/csvtablemodel.h"
#include "settings/appsettings.h"
#include <QMouseEvent>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QMenu>
#include <QAction>
#include <QHeaderView>

AcquisitionTableView::AcquisitionTableView(QWidget *parent)
    : QTableView(parent)
{
    setMouseTracking(true);

    horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(horizontalHeader(), &QWidget::customContextMenuRequested,
            this, &AcquisitionTableView::onHeaderContextMenu);

    connect(this, &QAbstractItemView::clicked,
            viewport(), [this](const QModelIndex &) {
        viewport()->update();
    });
}

void AcquisitionTableView::restoreColumnVisibility()
{
    auto *proxy = qobject_cast<QSortFilterProxyModel *>(model());
    if (!proxy) return;

    QSet<int> hidden = AppSettings::instance().hiddenColumns();
    int cols = proxy->columnCount();
    for (int c = 0; c < cols; ++c) {
        auto srcIdx = proxy->mapToSource(proxy->index(0, c));
        int srcCol = srcIdx.isValid() ? srcIdx.column() : c;
        if (srcCol == CsvTableModel::ColGroup)
            setColumnHidden(c, false);
        else
            setColumnHidden(c, hidden.contains(srcCol));
    }
}

void AcquisitionTableView::onHeaderContextMenu(const QPoint &pos)
{
    auto *proxy = qobject_cast<QSortFilterProxyModel *>(model());
    if (!proxy) return;

    QMenu *menu = new QMenu(tr("Show/Hide Columns"), this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    int cols = proxy->columnCount();
    for (int c = 0; c < cols; ++c) {
        auto srcIdx = proxy->mapToSource(proxy->index(0, c));
        int srcCol = srcIdx.isValid() ? srcIdx.column() : c;
        if (srcCol == CsvTableModel::ColGroup) continue;

        QString label = proxy->headerData(c, Qt::Horizontal).toString();
        QAction *act = menu->addAction(label);
        act->setCheckable(true);
        act->setChecked(!isColumnHidden(c));
        act->setData(c);

        connect(act, &QAction::triggered, this, [this, c, srcCol](bool checked) {
            setColumnHidden(c, !checked);
            QSet<int> hidden = AppSettings::instance().hiddenColumns();
            if (checked)
                hidden.remove(srcCol);
            else
                hidden.insert(srcCol);
            AppSettings::instance().setHiddenColumns(hidden);
        });
    }

    // Intercept mouse-release on the menu's viewport so that clicking a
    // checkable action toggles it without dismissing the menu.
    menu->installEventFilter(this);

    menu->popup(horizontalHeader()->mapToGlobal(pos));
}

bool AcquisitionTableView::eventFilter(QObject *obj, QEvent *e)
{
    // Keep the header context menu open when the user clicks a checkable item.
    if (auto *menu = qobject_cast<QMenu *>(obj)) {
        if (e->type() == QEvent::MouseButtonRelease) {
            QAction *act = menu->activeAction();
            if (act && act->isCheckable()) {
                act->trigger();
                return true;   // swallow the event — menu stays open
            }
        }
    }
    return QTableView::eventFilter(obj, e);
}

QRect AcquisitionTableView::visualRectFor(const QModelIndex &proxyIdx) const
{
    return QTableView::visualRect(proxyIdx);
}

QRect AcquisitionTableView::fillHandleRect() const
{
    auto *proxy = qobject_cast<QSortFilterProxyModel *>(model());
    if (!proxy) return {};

    auto *sel = selectionModel();
    if (!sel || !sel->hasSelection()) return {};

    auto idxList = sel->selectedIndexes();
    if (idxList.isEmpty()) return {};

    int col = idxList.first().column();
    for (const auto &idx : idxList)
        if (idx.column() != col) return {};

    int srcCol = proxy->mapToSource(idxList.first()).column();
    if (srcCol == CsvTableModel::ColGroup ||
        srcCol == CsvTableModel::ColFilterName)
        return {};

    QModelIndex bottomProxy = idxList.first();
    for (const auto &idx : idxList)
        if (idx.row() > bottomProxy.row()) bottomProxy = idx;

    QRect cellRect = visualRectFor(bottomProxy);
    if (cellRect.isNull()) return {};

    int x = cellRect.right()  - kHandleSize / 2;
    int y = cellRect.bottom() - kHandleSize / 2;
    return QRect(x, y, kHandleSize, kHandleSize);
}

bool AcquisitionTableView::overFillHandle(const QPoint &pos) const
{
    QRect r = fillHandleRect();
    return !r.isNull() && r.adjusted(-2, -2, 2, 2).contains(pos);
}

void AcquisitionTableView::paintEvent(QPaintEvent *e)
{
    QTableView::paintEvent(e);

    if (m_dragging && m_dragToRow > m_srcProxyIdx.row()) {
        auto *proxy = qobject_cast<QSortFilterProxyModel *>(model());
        if (proxy) {
            QModelIndex topIdx = proxy->index(m_srcProxyIdx.row() + 1,
                                              m_srcProxyIdx.column());
            QModelIndex botIdx = proxy->index(m_dragToRow,
                                              m_srcProxyIdx.column());
            QRect fillRect = visualRectFor(topIdx) | visualRectFor(botIdx);
            if (!fillRect.isNull()) {
                QPainter p(viewport());
                QColor hi = palette().highlight().color();
                hi.setAlpha(50);
                p.fillRect(fillRect, hi);
                p.setPen(QPen(palette().highlight().color(), 1, Qt::DashLine));
                p.drawRect(fillRect.adjusted(0, 0, -1, -1));
            }
        }
    }

    QRect hr = fillHandleRect();
    if (hr.isNull()) return;

    QPainter p(viewport());
    p.fillRect(hr, palette().highlight());
    p.setPen(palette().highlightedText().color());
    p.drawRect(hr.adjusted(0, 0, -1, -1));
}

void AcquisitionTableView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && overFillHandle(e->pos())) {
        auto *proxy = qobject_cast<QSortFilterProxyModel *>(model());
        if (!proxy) { QTableView::mousePressEvent(e); return; }

        auto idxList = selectionModel()->selectedIndexes();
        QModelIndex bottomProxy = idxList.first();
        for (const auto &idx : idxList)
            if (idx.row() > bottomProxy.row()) bottomProxy = idx;

        m_srcProxyIdx = bottomProxy;
        m_dragToRow   = bottomProxy.row();
        m_dragging    = true;
        setCursor(Qt::CrossCursor);
        e->accept();
        return;
    }
    QTableView::mousePressEvent(e);
}

void AcquisitionTableView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging) {
        QModelIndex hovered = indexAt(e->pos());
        if (hovered.isValid() && hovered.column() == m_srcProxyIdx.column())
            m_dragToRow = qMax(m_srcProxyIdx.row(), hovered.row());
        viewport()->update();
        e->accept();
        return;
    }

    if (overFillHandle(e->pos()))
        setCursor(Qt::CrossCursor);
    else
        unsetCursor();

    QTableView::mouseMoveEvent(e);
}

void AcquisitionTableView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_dragging && e->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();

        auto *proxy = qobject_cast<QSortFilterProxyModel *>(model());
        if (proxy && m_dragToRow > m_srcProxyIdx.row()) {
            QVariant val = proxy->data(m_srcProxyIdx, Qt::EditRole);
            for (int row = m_srcProxyIdx.row() + 1; row <= m_dragToRow; ++row) {
                QModelIndex dst = proxy->index(row, m_srcProxyIdx.column());
                proxy->setData(dst, val, Qt::EditRole);
            }
        }

        m_srcProxyIdx = {};
        m_dragToRow   = -1;
        viewport()->update();
        e->accept();
        return;
    }
    QTableView::mouseReleaseEvent(e);
}

void AcquisitionTableView::leaveEvent(QEvent *e)
{
    unsetCursor();
    QTableView::leaveEvent(e);
}
