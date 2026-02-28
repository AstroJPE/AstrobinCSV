#pragma once
#include <QTableView>
#include <QModelIndex>

class AcquisitionTableView : public QTableView {
    Q_OBJECT
public:
    explicit AcquisitionTableView(QWidget *parent = nullptr);

    void restoreColumnVisibility();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *e) override;

private slots:
    void onHeaderContextMenu(const QPoint &pos);

private:
    QRect fillHandleRect() const;
    bool  overFillHandle(const QPoint &pos) const;
    QRect visualRectFor(const QModelIndex &proxyIdx) const;

    static constexpr int kHandleSize = 7;

    bool        m_dragging{false};
    QModelIndex m_srcProxyIdx;
    int         m_dragToRow{-1};
};
