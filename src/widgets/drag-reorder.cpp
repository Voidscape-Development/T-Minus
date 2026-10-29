#include "drag-reorder.hpp"

#include <QApplication>
#include <QCursor>
#include <QGraphicsOpacityEffect>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

DragHandle::DragHandle(QWidget *parent) : QWidget(parent)
{
	setFixedWidth(12);
	setCursor(Qt::OpenHandCursor);
	setToolTip("Drag to reorder");
}

void DragHandle::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	QColor dot = palette().color(QPalette::WindowText);
	dot.setAlphaF(underMouse() || m_dragging ? 0.8 : 0.4);
	painter.setPen(Qt::NoPen);
	painter.setBrush(dot);

	// Two columns of three dots, centred
	const QPointF center = QRectF(rect()).center();
	for (int col = 0; col < 2; col++) {
		for (int row = 0; row < 3; row++) {
			const QPointF p(center.x() + (col == 0 ? -2.5 : 2.5), center.y() + (row - 1) * 5.0);
			painter.drawEllipse(p, 1.4, 1.4);
		}
	}
}

void DragHandle::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}
	m_pressed = true;
	m_pressPos = event->globalPosition().toPoint();
	event->accept();
}

void DragHandle::mouseMoveEvent(QMouseEvent *event)
{
	if (!m_pressed)
		return;

	const QPoint pos = event->globalPosition().toPoint();
	if (!m_dragging) {
		if ((pos - m_pressPos).manhattanLength() < QApplication::startDragDistance())
			return;
		m_dragging = true;
		setCursor(Qt::ClosedHandCursor);
		emit dragStarted();
	}
	emit dragMoved(pos);
}

void DragHandle::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;
	m_pressed = false;
	if (m_dragging) {
		m_dragging = false;
		setCursor(Qt::OpenHandCursor);
		emit dragFinished();
	}
	update();
}

DragReorderController::DragReorderController(QScrollArea *scroll, ItemsFn items, MoveFn move, FinishedFn finished,
					     QObject *parent)
	: QObject(parent),
	  m_scroll(scroll),
	  m_items(std::move(items)),
	  m_move(std::move(move)),
	  m_finished(std::move(finished))
{
	m_scrollTimer.setInterval(30);
	connect(&m_scrollTimer, &QTimer::timeout, this, &DragReorderController::AutoScroll);
}

void DragReorderController::Attach(DragHandle *handle, QWidget *item)
{
	QPointer<QWidget> guarded(item);
	connect(handle, &DragHandle::dragStarted, this, [this, guarded]() {
		if (guarded)
			Begin(guarded);
	});
	connect(handle, &DragHandle::dragMoved, this, &DragReorderController::Update);
	connect(handle, &DragHandle::dragFinished, this, &DragReorderController::End);
}

void DragReorderController::Begin(QWidget *item)
{
	m_dragged = item;
	m_moved = false;

	// Fade the item being carried so it reads as "in hand"
	auto *effect = new QGraphicsOpacityEffect(item);
	effect->setOpacity(0.55);
	item->setGraphicsEffect(effect);

	m_scrollTimer.start();
}

void DragReorderController::Update(const QPoint &globalPos)
{
	if (!m_dragged)
		return;

	const QList<QWidget *> items = m_items();
	const int from = items.indexOf(m_dragged);
	if (from < 0)
		return;

	// The drop index is the number of other items whose midpoint is
	// above the pointer
	int to = 0;
	for (QWidget *item : items) {
		if (item == m_dragged)
			continue;
		const int mid = item->mapToGlobal(QPoint(0, item->height() / 2)).y();
		if (mid < globalPos.y())
			to++;
	}

	if (to == from)
		return;

	m_move(from, to);
	m_moved = true;

	// Apply the new order to geometry now, so the next pointer move is
	// measured against where the items actually are
	if (QWidget *content = m_scroll->widget()) {
		if (content->layout())
			content->layout()->activate();
	}
}

void DragReorderController::End()
{
	m_scrollTimer.stop();
	if (m_dragged)
		m_dragged->setGraphicsEffect(nullptr);
	m_dragged = nullptr;

	if (m_moved && m_finished)
		m_finished();
	m_moved = false;
}

void DragReorderController::AutoScroll()
{
	if (!m_dragged)
		return;

	// Scroll while the pointer rests near the top or bottom edge, so items
	// can be dragged past what's visible
	QWidget *viewport = m_scroll->viewport();
	const QPoint local = viewport->mapFromGlobal(QCursor::pos());
	const int edge = 28;
	int step = 0;
	if (local.y() < edge)
		step = -qMax(2, (edge - local.y()) / 2);
	else if (local.y() > viewport->height() - edge)
		step = qMax(2, (local.y() - (viewport->height() - edge)) / 2);

	if (step == 0)
		return;

	QScrollBar *bar = m_scroll->verticalScrollBar();
	const int before = bar->value();
	bar->setValue(before + step);
	if (bar->value() != before)
		Update(QCursor::pos());
}
