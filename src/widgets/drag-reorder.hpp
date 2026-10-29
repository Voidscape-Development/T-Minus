#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QScrollArea>
#include <QTimer>
#include <QWidget>

#include <functional>

// Grip shown at the start of a reorderable row. Pressing and dragging it
// reports the drag in global coordinates; the row itself stays put until
// a DragReorderController moves it.
class DragHandle : public QWidget {
	Q_OBJECT

public:
	explicit DragHandle(QWidget *parent = nullptr);

	QSize sizeHint() const override { return QSize(12, 20); }

signals:
	void dragStarted();
	void dragMoved(const QPoint &globalPos);
	void dragFinished();

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	QPoint m_pressPos;
	bool m_pressed = false;
	bool m_dragging = false;
};

// Live drag-to-reorder for a vertical list of widgets inside a scroll area.
// As the pointer passes the middle of a neighbour, the owner is asked to
// move the dragged item there, so the list reflows under the pointer.
class DragReorderController : public QObject {
	Q_OBJECT

public:
	using ItemsFn = std::function<QList<QWidget *>()>;
	using MoveFn = std::function<void(int from, int to)>;
	using FinishedFn = std::function<void()>;

	// items returns the list in its current order; move reorders the
	// owner's list and layout; finished runs once after a drag that
	// changed the order.
	DragReorderController(QScrollArea *scroll, ItemsFn items, MoveFn move, FinishedFn finished,
			      QObject *parent = nullptr);

	// Connects a handle so dragging it drags item
	void Attach(DragHandle *handle, QWidget *item);

private:
	void Begin(QWidget *item);
	void Update(const QPoint &globalPos);
	void End();
	void AutoScroll();

	QScrollArea *m_scroll;
	ItemsFn m_items;
	MoveFn m_move;
	FinishedFn m_finished;

	QPointer<QWidget> m_dragged;
	bool m_moved = false;
	QTimer m_scrollTimer;
};
