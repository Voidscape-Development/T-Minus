#include "datetime-picker.hpp"
#include "button-style.hpp"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <functional>

namespace {

const QColor kPastColor(0xe8, 0x60, 0x4f);

// Height of the weekday initials above the day grid
constexpr int kWeekdayRowHeight = 22;
constexpr int kDayRowHeight = 30;
constexpr int kDayColumnWidth = 36;

// Stepper geometry: chevron, number, chevron
constexpr int kStepperWidth = 64;
constexpr int kChevronZoneHeight = 24;
constexpr int kValueZoneHeight = 44;
constexpr int kStepperHeight = kChevronZoneHeight * 2 + kValueZoneHeight;

QColor WithAlpha(QColor color, qreal alpha)
{
	color.setAlphaF(alpha);
	return color;
}

QString Rgba(const QColor &c, int alphaPercent)
{
	return QString("rgba(%1, %2, %3, %4%)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alphaPercent);
}

QFont PixelFont(const QFont &base, int pixelSize, bool bold)
{
	QFont f = base;
	f.setPixelSize(pixelSize);
	f.setBold(bold);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	// Tabular figures keep numbers from shifting as they change
	f.setFeature(QFont::Tag("tnum"), 1);
#endif
	return f;
}

// Chevron pointing in direction, centred in rect
void DrawChevron(QPainter &p, const QRectF &rect, Qt::ArrowType direction, const QColor &color, qreal size = 5.0)
{
	const QPointF c = rect.center();
	QPainterPath path;
	switch (direction) {
	case Qt::UpArrow:
		path.moveTo(c.x() - size, c.y() + size / 2);
		path.lineTo(c.x(), c.y() - size / 2);
		path.lineTo(c.x() + size, c.y() + size / 2);
		break;
	case Qt::DownArrow:
		path.moveTo(c.x() - size, c.y() - size / 2);
		path.lineTo(c.x(), c.y() + size / 2);
		path.lineTo(c.x() + size, c.y() - size / 2);
		break;
	case Qt::LeftArrow:
		path.moveTo(c.x() + size / 2, c.y() - size);
		path.lineTo(c.x() - size / 2, c.y());
		path.lineTo(c.x() + size / 2, c.y() + size);
		break;
	default:
		path.moveTo(c.x() - size / 2, c.y() - size);
		path.lineTo(c.x() + size / 2, c.y());
		path.lineTo(c.x() - size / 2, c.y() + size);
		break;
	}
	QPen pen(color, 1.8);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	p.drawPath(path);
}

QIcon ChevronIcon(Qt::ArrowType direction, const QColor &color)
{
	QPixmap pixmap(32, 32);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	DrawChevron(painter, QRectF(0, 0, 32, 32), direction, color, 9.0);
	return QIcon(pixmap);
}

// "4d 3h 12m", "2h 5m", "45m", "under a minute"
QString FormatSpan(qint64 secs)
{
	if (secs < 60)
		return QStringLiteral("under a minute");
	const qint64 days = secs / 86400;
	const qint64 hours = (secs % 86400) / 3600;
	const qint64 minutes = (secs % 3600) / 60;
	QStringList parts;
	if (days > 0)
		parts << QString("%1d").arg(days);
	if (hours > 0)
		parts << QString("%1h").arg(hours);
	if (minutes > 0 && days < 7)
		parts << QString("%1m").arg(minutes);
	return parts.join(' ');
}

bool LocaleUses12Hour()
{
	const QString fmt = QLocale().timeFormat(QLocale::ShortFormat);
	return fmt.contains(QLatin1Char('A'), Qt::CaseInsensitive);
}

// Next whole minute at or after dt, so presets give clean times
QDateTime CeilToMinute(const QDateTime &dt)
{
	const QTime t = dt.time();
	QDateTime floored(dt.date(), QTime(t.hour(), t.minute()));
	return (t.second() == 0 && t.msec() == 0) ? floored : floored.addSecs(60);
}

QPushButton *SmallButton(const QString &text, QWidget *parent, int height = 26, int padding = 10)
{
	auto *btn = new QPushButton(text, parent);
	btn->setAutoDefault(false);
	btn->setCursor(Qt::PointingHandCursor);
	StyleTextButton(btn, height, padding);
	return btn;
}

} // namespace

// --- CalendarGrid ---

CalendarGrid::CalendarGrid(QWidget *parent) : QWidget(parent)
{
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	setAccessibleName("Date");
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	const QDate today = QDate::currentDate();
	m_year = today.year();
	m_month = today.month();
	m_selected = today;
}

QSize CalendarGrid::sizeHint() const
{
	return QSize(kDayColumnWidth * 7, kWeekdayRowHeight + kDayRowHeight * 6);
}

QSize CalendarGrid::minimumSizeHint() const
{
	return QSize(28 * 7, kWeekdayRowHeight + kDayRowHeight * 6);
}

void CalendarGrid::setSelectedDate(const QDate &date)
{
	if (!date.isValid())
		return;
	m_selected = date;
	showMonth(date.year(), date.month());
	update();
}

void CalendarGrid::setMinimumDate(const QDate &date)
{
	m_minimum = date;
	update();
}

void CalendarGrid::showMonth(int year, int month)
{
	if (year == m_year && month == m_month)
		return;
	m_year = year;
	m_month = month;
	m_hover = -1;
	update();
	emit shownMonthChanged(m_year, m_month);
}

void CalendarGrid::showPreviousMonth()
{
	if (!canShowPreviousMonth())
		return;
	const QDate prev = QDate(m_year, m_month, 1).addMonths(-1);
	showMonth(prev.year(), prev.month());
}

void CalendarGrid::showNextMonth()
{
	const QDate next = QDate(m_year, m_month, 1).addMonths(1);
	showMonth(next.year(), next.month());
}

bool CalendarGrid::canShowPreviousMonth() const
{
	if (!m_minimum.isValid())
		return true;
	return QDate(m_year, m_month, 1) > QDate(m_minimum.year(), m_minimum.month(), 1);
}

QDate CalendarGrid::FirstShownDate() const
{
	const QDate first(m_year, m_month, 1);
	const int firstDay = static_cast<int>(locale().firstDayOfWeek());
	const int offset = (first.dayOfWeek() - firstDay + 7) % 7;
	return first.addDays(-offset);
}

QRectF CalendarGrid::CellRect(int index) const
{
	const qreal cellWidth = width() / 7.0;
	const qreal rowHeight = (height() - kWeekdayRowHeight) / 6.0;
	return QRectF((index % 7) * cellWidth, kWeekdayRowHeight + (index / 7) * rowHeight, cellWidth, rowHeight);
}

int CalendarGrid::CellAt(const QPoint &pos) const
{
	if (pos.y() < kWeekdayRowHeight || pos.x() < 0 || pos.x() >= width() || pos.y() >= height())
		return -1;
	const int column = qBound(0, static_cast<int>(pos.x() / (width() / 7.0)), 6);
	const int row =
		qBound(0, static_cast<int>((pos.y() - kWeekdayRowHeight) / ((height() - kWeekdayRowHeight) / 6.0)), 5);
	return row * 7 + column;
}

bool CalendarGrid::IsSelectable(const QDate &date) const
{
	return !m_minimum.isValid() || date >= m_minimum;
}

void CalendarGrid::SelectByUser(const QDate &date)
{
	if (!date.isValid() || !IsSelectable(date))
		return;
	m_selected = date;
	showMonth(date.year(), date.month());
	update();
	emit dateSelected(date);
}

void CalendarGrid::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	const QPalette pal = palette();
	const QColor text = pal.color(QPalette::WindowText);
	const QColor accent = pal.color(QPalette::Highlight);
	const QColor accentText = pal.color(QPalette::HighlightedText);
	const QDate today = QDate::currentDate();

	// Weekday initials
	p.setFont(PixelFont(font(), 11, true));
	p.setPen(WithAlpha(text, 0.5));
	const int firstDay = static_cast<int>(locale().firstDayOfWeek());
	const qreal cellWidth = width() / 7.0;
	for (int i = 0; i < 7; i++) {
		const int day = (firstDay - 1 + i) % 7 + 1;
		const QString name = locale().standaloneDayName(day, QLocale::ShortFormat).left(2);
		p.drawText(QRectF(i * cellWidth, 0, cellWidth, kWeekdayRowHeight), Qt::AlignCenter, name);
	}

	const QDate first = FirstShownDate();
	const QFont dayFont = PixelFont(font(), 13, false);
	const QFont boldDayFont = PixelFont(font(), 13, true);

	for (int i = 0; i < 42; i++) {
		const QDate date = first.addDays(i);
		const QRectF cell = CellRect(i);
		const qreal diameter = qMin(cell.width(), cell.height()) - 4;
		const QRectF circle(cell.center().x() - diameter / 2, cell.center().y() - diameter / 2, diameter,
				    diameter);

		const bool inMonth = date.month() == m_month;
		const bool selectable = IsSelectable(date);
		const bool selected = date == m_selected;
		const bool isToday = date == today;

		if (selected) {
			p.setPen(Qt::NoPen);
			p.setBrush(selectable ? accent : WithAlpha(kPastColor, 0.85));
			p.drawEllipse(circle);
		} else if (i == m_hover && selectable) {
			p.setPen(Qt::NoPen);
			p.setBrush(WithAlpha(text, 0.12));
			p.drawEllipse(circle);
		}

		if (isToday && !selected) {
			p.setPen(QPen(accent, 1.5));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(circle.adjusted(1, 1, -1, -1));
		}

		QColor color = text;
		if (selected)
			color = accentText;
		else if (!selectable)
			color = WithAlpha(text, inMonth ? 0.28 : 0.14);
		else if (!inMonth)
			color = WithAlpha(text, 0.42);

		p.setFont(selected || isToday ? boldDayFont : dayFont);
		p.setPen(color);
		p.drawText(cell, Qt::AlignCenter, QString::number(date.day()));
	}

	// Keyboard focus: a ring around the selected day
	if (hasFocus()) {
		const int index = static_cast<int>(first.daysTo(m_selected));
		if (index >= 0 && index < 42) {
			const QRectF cell = CellRect(index);
			const qreal diameter = qMin(cell.width(), cell.height()) + 1;
			p.setPen(QPen(WithAlpha(accent, 0.6), 1.5));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(QRectF(cell.center().x() - diameter / 2, cell.center().y() - diameter / 2,
					     diameter, diameter));
		}
	}
}

void CalendarGrid::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return QWidget::mousePressEvent(event);
	const int index = CellAt(event->position().toPoint());
	if (index >= 0)
		SelectByUser(FirstShownDate().addDays(index));
}

void CalendarGrid::mouseMoveEvent(QMouseEvent *event)
{
	const int index = CellAt(event->position().toPoint());
	const bool selectable = index >= 0 && IsSelectable(FirstShownDate().addDays(index));
	setCursor(selectable ? Qt::PointingHandCursor : Qt::ArrowCursor);
	if (index != m_hover) {
		m_hover = index;
		update();
	}
}

void CalendarGrid::leaveEvent(QEvent *)
{
	m_hover = -1;
	update();
}

void CalendarGrid::wheelEvent(QWheelEvent *event)
{
	m_wheelDelta += event->angleDelta().y();
	while (m_wheelDelta >= 120) {
		showPreviousMonth();
		m_wheelDelta -= 120;
	}
	while (m_wheelDelta <= -120) {
		showNextMonth();
		m_wheelDelta += 120;
	}
	event->accept();
}

void CalendarGrid::keyPressEvent(QKeyEvent *event)
{
	const QDate base = m_selected.isValid() ? m_selected : QDate::currentDate();
	QDate target;
	switch (event->key()) {
	case Qt::Key_Left:
		target = base.addDays(-1);
		break;
	case Qt::Key_Right:
		target = base.addDays(1);
		break;
	case Qt::Key_Up:
		target = base.addDays(-7);
		break;
	case Qt::Key_Down:
		target = base.addDays(7);
		break;
	case Qt::Key_PageUp:
		target = base.addMonths(-1);
		break;
	case Qt::Key_PageDown:
		target = base.addMonths(1);
		break;
	case Qt::Key_Home:
		target = QDate::currentDate();
		break;
	default:
		return QWidget::keyPressEvent(event);
	}

	if (m_minimum.isValid() && target < m_minimum)
		target = m_minimum;
	SelectByUser(target);
	event->accept();
}

// --- TimeStepper ---

TimeStepper::TimeStepper(int minimum, int maximum, bool zeroPadded, const QString &accessibleName, QWidget *parent)
	: QWidget(parent),
	  m_min(minimum),
	  m_max(maximum),
	  m_value(minimum),
	  m_padded(zeroPadded)
{
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	setAccessibleName(accessibleName);
	setToolTip(QString("%1: click the arrows, scroll, or type a number").arg(accessibleName));
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

	// Holding a chevron keeps stepping, slowly at first
	m_repeat = new QTimer(this);
	connect(m_repeat, &QTimer::timeout, this, [this]() {
		if (m_pressed == UpZone)
			Step(1);
		else if (m_pressed == DownZone)
			Step(-1);
		m_repeat->setInterval(70);
	});

	m_typeTimeout = new QTimer(this);
	m_typeTimeout->setSingleShot(true);
	m_typeTimeout->setInterval(1000);
	connect(m_typeTimeout, &QTimer::timeout, this, &TimeStepper::CommitTyped);
}

QSize TimeStepper::sizeHint() const
{
	return QSize(kStepperWidth, kStepperHeight);
}

void TimeStepper::setValue(int value)
{
	m_value = qBound(m_min, value, m_max);
	m_typed.clear();
	update();
}

void TimeStepper::setRange(int minimum, int maximum)
{
	m_min = minimum;
	m_max = maximum;
	setValue(m_value);
}

QRectF TimeStepper::ZoneRect(Zone zone) const
{
	switch (zone) {
	case UpZone:
		return QRectF(0, 0, width(), kChevronZoneHeight);
	case DownZone:
		return QRectF(0, height() - kChevronZoneHeight, width(), kChevronZoneHeight);
	case ValueZone:
		return QRectF(0, kChevronZoneHeight, width(), height() - 2 * kChevronZoneHeight);
	default:
		return QRectF();
	}
}

TimeStepper::Zone TimeStepper::ZoneAt(const QPoint &pos) const
{
	if (!rect().contains(pos))
		return NoZone;
	if (pos.y() < kChevronZoneHeight)
		return UpZone;
	if (pos.y() >= height() - kChevronZoneHeight)
		return DownZone;
	return ValueZone;
}

void TimeStepper::Step(int delta)
{
	m_typed.clear();
	const int range = m_max - m_min + 1;
	m_value = m_min + (((m_value - m_min + delta) % range) + range) % range;
	update();
	emit edited(m_value);
}

void TimeStepper::CommitTyped()
{
	m_typeTimeout->stop();
	if (m_typed.isEmpty())
		return;
	const int typed = m_typed.toInt();
	m_typed.clear();
	if (typed >= m_min && typed <= m_max && typed != m_value) {
		m_value = typed;
		emit edited(m_value);
	}
	update();
}

void TimeStepper::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	const QPalette pal = palette();
	const QColor text = pal.color(QPalette::WindowText);
	const QColor accent = pal.color(QPalette::Highlight);
	const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

	p.setPen(QPen(hasFocus() ? accent : WithAlpha(text, 0.18), hasFocus() ? 1.5 : 1.0));
	p.setBrush(WithAlpha(text, m_hover != NoZone ? 0.08 : 0.05));
	p.drawRoundedRect(box, 8, 8);

	// Hovered or pressed chevron zone gets its own fill, clipped to the box
	for (Zone zone : {UpZone, DownZone}) {
		if (m_hover != zone && m_pressed != zone)
			continue;
		QPainterPath clip;
		clip.addRoundedRect(box, 8, 8);
		p.save();
		p.setClipPath(clip);
		p.fillRect(ZoneRect(zone), WithAlpha(text, m_pressed == zone ? 0.16 : 0.08));
		p.restore();
	}

	DrawChevron(p, ZoneRect(UpZone), Qt::UpArrow, WithAlpha(text, m_hover == UpZone ? 0.95 : 0.5));
	DrawChevron(p, ZoneRect(DownZone), Qt::DownArrow, WithAlpha(text, m_hover == DownZone ? 0.95 : 0.5));

	QString label;
	QColor labelColor = text;
	if (!m_typed.isEmpty()) {
		label = m_typed;
		labelColor = accent;
	} else {
		label = m_padded ? QString("%1").arg(m_value, 2, 10, QLatin1Char('0')) : QString::number(m_value);
	}
	p.setFont(PixelFont(font(), 26, true));
	p.setPen(labelColor);
	p.drawText(ZoneRect(ValueZone), Qt::AlignCenter, label);
}

void TimeStepper::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return QWidget::mousePressEvent(event);
	setFocus(Qt::MouseFocusReason);
	CommitTyped();
	m_pressed = ZoneAt(event->position().toPoint());
	if (m_pressed == UpZone || m_pressed == DownZone) {
		Step(m_pressed == UpZone ? 1 : -1);
		m_repeat->start(400);
	}
	update();
}

void TimeStepper::mouseReleaseEvent(QMouseEvent *event)
{
	QWidget::mouseReleaseEvent(event);
	m_repeat->stop();
	m_pressed = NoZone;
	update();
}

void TimeStepper::mouseMoveEvent(QMouseEvent *event)
{
	const Zone zone = ZoneAt(event->position().toPoint());
	setCursor(zone == UpZone || zone == DownZone ? Qt::PointingHandCursor : Qt::ArrowCursor);
	if (zone != m_hover) {
		m_hover = zone;
		update();
	}
}

void TimeStepper::leaveEvent(QEvent *)
{
	m_hover = NoZone;
	update();
}

void TimeStepper::wheelEvent(QWheelEvent *event)
{
	CommitTyped();
	m_wheelDelta += event->angleDelta().y();
	while (m_wheelDelta >= 120) {
		Step(1);
		m_wheelDelta -= 120;
	}
	while (m_wheelDelta <= -120) {
		Step(-1);
		m_wheelDelta += 120;
	}
	event->accept();
}

void TimeStepper::keyPressEvent(QKeyEvent *event)
{
	switch (event->key()) {
	case Qt::Key_Up:
		CommitTyped();
		Step(1);
		return;
	case Qt::Key_Down:
		CommitTyped();
		Step(-1);
		return;
	case Qt::Key_PageUp:
		CommitTyped();
		Step(m_max >= 59 ? 10 : 3);
		return;
	case Qt::Key_PageDown:
		CommitTyped();
		Step(m_max >= 59 ? -10 : -3);
		return;
	case Qt::Key_Backspace:
		if (!m_typed.isEmpty()) {
			m_typed.chop(1);
			update();
			return;
		}
		break;
	case Qt::Key_Return:
	case Qt::Key_Enter:
		if (!m_typed.isEmpty()) {
			CommitTyped();
			return;
		}
		break;
	case Qt::Key_Escape:
		if (!m_typed.isEmpty()) {
			m_typed.clear();
			m_typeTimeout->stop();
			update();
			return;
		}
		break;
	default:
		break;
	}

	const QString text = event->text();
	if (text.size() == 1 && text[0].isDigit()) {
		m_typed += text;
		// Commit as soon as another digit couldn't produce a valid value
		if (m_typed.size() >= 2 || m_typed.toInt() * 10 > m_max)
			CommitTyped();
		else
			m_typeTimeout->start();
		update();
		return;
	}

	QWidget::keyPressEvent(event);
}

void TimeStepper::focusOutEvent(QFocusEvent *event)
{
	CommitTyped();
	QWidget::focusOutEvent(event);
}

// --- MeridiemToggle ---

MeridiemToggle::MeridiemToggle(QWidget *parent) : QWidget(parent)
{
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	setAccessibleName("AM or PM");
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize MeridiemToggle::sizeHint() const
{
	const QFontMetrics fm(PixelFont(font(), 12, true));
	const int textWidth = qMax(fm.horizontalAdvance(locale().amText()), fm.horizontalAdvance(locale().pmText()));
	return QSize(qMax(48, textWidth + 20), kStepperHeight);
}

void MeridiemToggle::setPm(bool pm)
{
	m_pm = pm;
	update();
}

void MeridiemToggle::SetByUser(bool pm)
{
	if (pm == m_pm)
		return;
	m_pm = pm;
	update();
	emit edited(m_pm);
}

QRectF MeridiemToggle::SegmentRect(bool pm) const
{
	const qreal half = (height() - 6) / 2.0;
	return pm ? QRectF(0.5, half + 6, width() - 1, half - 0.5) : QRectF(0.5, 0.5, width() - 1, half - 0.5);
}

void MeridiemToggle::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	const QPalette pal = palette();
	const QColor text = pal.color(QPalette::WindowText);
	const QColor accent = pal.color(QPalette::Highlight);
	const QColor accentText = pal.color(QPalette::HighlightedText);

	p.setFont(PixelFont(font(), 12, true));
	for (bool pm : {false, true}) {
		const QRectF r = SegmentRect(pm);
		const bool active = pm == m_pm;
		const bool hovered = m_hover == (pm ? 1 : 0);
		if (active) {
			p.setPen(hasFocus() ? QPen(WithAlpha(accentText, 0.7), 1.5) : Qt::NoPen);
			p.setBrush(accent);
		} else {
			p.setPen(QPen(WithAlpha(text, 0.18), 1.0));
			p.setBrush(WithAlpha(text, hovered ? 0.1 : 0.05));
		}
		p.drawRoundedRect(r, 8, 8);
		p.setPen(active ? accentText : WithAlpha(text, hovered ? 0.9 : 0.6));
		p.drawText(r, Qt::AlignCenter, pm ? locale().pmText() : locale().amText());
	}
}

void MeridiemToggle::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return QWidget::mousePressEvent(event);
	setFocus(Qt::MouseFocusReason);
	SetByUser(event->position().y() >= height() / 2.0);
}

void MeridiemToggle::mouseMoveEvent(QMouseEvent *event)
{
	setCursor(Qt::PointingHandCursor);
	const int hover = event->position().y() >= height() / 2.0 ? 1 : 0;
	if (hover != m_hover) {
		m_hover = hover;
		update();
	}
}

void MeridiemToggle::leaveEvent(QEvent *)
{
	m_hover = -1;
	update();
}

void MeridiemToggle::keyPressEvent(QKeyEvent *event)
{
	switch (event->key()) {
	case Qt::Key_Up:
		SetByUser(false);
		return;
	case Qt::Key_Down:
		SetByUser(true);
		return;
	case Qt::Key_Space:
		SetByUser(!m_pm);
		return;
	case Qt::Key_A:
		SetByUser(false);
		return;
	case Qt::Key_P:
		SetByUser(true);
		return;
	default:
		QWidget::keyPressEvent(event);
	}
}

// --- DateTimePicker ---

DateTimePicker::DateTimePicker(QWidget *parent) : QFrame(parent), m_12Hour(LocaleUses12Hour())
{
	setObjectName("tminusDateTimePicker");

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	// Summary: the chosen moment in words, and how far away it is
	auto *summary = new QWidget(this);
	summary->setObjectName("tminusPickerSummary");
	auto *summaryLayout = new QHBoxLayout(summary);
	summaryLayout->setContentsMargins(16, 12, 16, 12);
	summaryLayout->setSpacing(12);

	auto *whenColumn = new QVBoxLayout();
	whenColumn->setSpacing(2);
	m_whenTime = new QLabel(summary);
	m_whenTime->setObjectName("tminusWhenTime");
	whenColumn->addWidget(m_whenTime);
	m_whenDate = new QLabel(summary);
	m_whenDate->setObjectName("tminusWhenDate");
	whenColumn->addWidget(m_whenDate);
	summaryLayout->addLayout(whenColumn, 1);

	m_relative = new QLabel(summary);
	m_relative->setObjectName("tminusRelative");
	m_relative->setAlignment(Qt::AlignCenter);
	summaryLayout->addWidget(m_relative, 0, Qt::AlignVCenter);
	layout->addWidget(summary);

	// Calendar beside the time picker
	auto *body = new QWidget(this);
	auto *bodyLayout = new QHBoxLayout(body);
	bodyLayout->setContentsMargins(12, 10, 16, 12);
	bodyLayout->setSpacing(16);
	bodyLayout->addWidget(CreateCalendarPanel(), 1);

	auto *divider = new QFrame(body);
	divider->setObjectName("tminusPickerDivider");
	divider->setFixedWidth(1);
	bodyLayout->addWidget(divider);

	bodyLayout->addWidget(CreateTimePanel(), 0, Qt::AlignTop);
	layout->addWidget(body);

	layout->addWidget(CreatePresetRow());

	// Keeps "in 2h 15m" current while the dialog stays open
	m_refresh = new QTimer(this);
	m_refresh->setInterval(15000);
	connect(m_refresh, &QTimer::timeout, this, [this]() {
		m_calendar->setMinimumDate(QDate::currentDate());
		UpdateSummary();
	});

	const QDateTime start = CeilToMinute(QDateTime::currentDateTime().addSecs(3600));
	m_date = start.date();
	m_time = start.time();
	SyncControls();
	UpdateColors();
}

QWidget *DateTimePicker::CreateCalendarPanel()
{
	auto *panel = new QWidget(this);
	auto *layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);

	auto *nav = new QHBoxLayout();
	nav->setSpacing(4);

	m_prevMonthBtn = new QPushButton(panel);
	m_prevMonthBtn->setFlat(true);
	m_prevMonthBtn->setAutoDefault(false);
	m_prevMonthBtn->setToolTip("Previous month");
	m_prevMonthBtn->setAccessibleName("Previous month");
	StyleIconButton(m_prevMonthBtn, QSize(28, 26));

	m_monthLabel = new QLabel(panel);
	m_monthLabel->setObjectName("tminusMonthLabel");
	m_monthLabel->setAlignment(Qt::AlignCenter);

	m_nextMonthBtn = new QPushButton(panel);
	m_nextMonthBtn->setFlat(true);
	m_nextMonthBtn->setAutoDefault(false);
	m_nextMonthBtn->setToolTip("Next month");
	m_nextMonthBtn->setAccessibleName("Next month");
	StyleIconButton(m_nextMonthBtn, QSize(28, 26));

	m_todayBtn = SmallButton("Today", panel);
	m_todayBtn->setToolTip("Pick today's date");

	nav->addWidget(m_prevMonthBtn);
	nav->addWidget(m_monthLabel, 1);
	nav->addWidget(m_nextMonthBtn);
	nav->addSpacing(4);
	nav->addWidget(m_todayBtn);
	layout->addLayout(nav);

	m_calendar = new CalendarGrid(panel);
	m_calendar->setMinimumDate(QDate::currentDate());
	layout->addWidget(m_calendar);

	connect(m_prevMonthBtn, &QPushButton::clicked, m_calendar, &CalendarGrid::showPreviousMonth);
	connect(m_nextMonthBtn, &QPushButton::clicked, m_calendar, &CalendarGrid::showNextMonth);
	connect(m_todayBtn, &QPushButton::clicked, this, [this]() { ApplyByUser(QDate::currentDate(), m_time); });
	connect(m_calendar, &CalendarGrid::shownMonthChanged, this, &DateTimePicker::UpdateMonthHeader);
	connect(m_calendar, &CalendarGrid::dateSelected, this,
		[this](const QDate &date) { ApplyByUser(date, m_time); });

	return panel;
}

QWidget *DateTimePicker::CreateTimePanel()
{
	auto *panel = new QWidget(this);
	auto *layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(8);

	auto *caption = new QLabel("TIME", panel);
	caption->setObjectName("tminusPickerCaption");
	caption->setFixedHeight(26);
	layout->addWidget(caption);

	auto *steppers = new QHBoxLayout();
	steppers->setSpacing(6);
	m_hourStepper = m_12Hour ? new TimeStepper(1, 12, false, "Hour", panel)
				 : new TimeStepper(0, 23, true, "Hour", panel);
	steppers->addWidget(m_hourStepper);

	auto *colon = new QLabel(":", panel);
	colon->setObjectName("tminusPickerColon");
	colon->setAlignment(Qt::AlignCenter);
	steppers->addWidget(colon);

	m_minuteStepper = new TimeStepper(0, 59, true, "Minute", panel);
	steppers->addWidget(m_minuteStepper);

	if (m_12Hour) {
		steppers->addSpacing(4);
		m_meridiem = new MeridiemToggle(panel);
		steppers->addWidget(m_meridiem);
		connect(m_meridiem, &MeridiemToggle::edited, this, &DateTimePicker::TimeControlsEdited);
	}
	steppers->addStretch();
	layout->addLayout(steppers);

	// Common minute marks, one click each
	auto *marks = new QHBoxLayout();
	marks->setSpacing(4);
	for (int minute : {0, 15, 30, 45}) {
		auto *btn = SmallButton(QString(":%1").arg(minute, 2, 10, QLatin1Char('0')), panel, 24, 8);
		btn->setToolTip(QString("Set the minutes to %1").arg(minute, 2, 10, QLatin1Char('0')));
		connect(btn, &QPushButton::clicked, this, [this, minute]() {
			m_minuteStepper->setValue(minute);
			TimeControlsEdited();
		});
		marks->addWidget(btn);
	}
	marks->addStretch();
	layout->addLayout(marks);

	layout->addStretch();

	connect(m_hourStepper, &TimeStepper::edited, this, &DateTimePicker::TimeControlsEdited);
	connect(m_minuteStepper, &TimeStepper::edited, this, &DateTimePicker::TimeControlsEdited);

	return panel;
}

QWidget *DateTimePicker::CreatePresetRow()
{
	auto *row = new QWidget(this);
	row->setObjectName("tminusPickerPresets");
	auto *layout = new QHBoxLayout(row);
	layout->setContentsMargins(16, 10, 16, 12);
	layout->setSpacing(4);

	auto *caption = new QLabel("QUICK SET", row);
	caption->setObjectName("tminusPickerCaption");
	layout->addWidget(caption);
	layout->addSpacing(6);

	auto addPreset = [this, row, layout](const QString &text, const QString &tip, std::function<QDateTime()> calc) {
		auto *btn = SmallButton(text, row);
		btn->setToolTip(tip);
		connect(btn, &QPushButton::clicked, this, [this, calc]() {
			const QDateTime dt = calc();
			ApplyByUser(dt.date(), dt.time());
		});
		layout->addWidget(btn);
	};

	addPreset("15 min", "15 minutes from now",
		  []() { return CeilToMinute(QDateTime::currentDateTime().addSecs(15 * 60)); });
	addPreset("30 min", "30 minutes from now",
		  []() { return CeilToMinute(QDateTime::currentDateTime().addSecs(30 * 60)); });
	addPreset("1 hour", "An hour from now",
		  []() { return CeilToMinute(QDateTime::currentDateTime().addSecs(3600)); });
	addPreset("Next hour", "The next full hour", []() {
		const QDateTime now = QDateTime::currentDateTime();
		return QDateTime(now.date(), QTime(now.time().hour(), 0)).addSecs(3600);
	});
	addPreset("Midnight", "Midnight tonight",
		  []() { return QDateTime(QDate::currentDate().addDays(1), QTime(0, 0)); });
	layout->addStretch();

	return row;
}

QDateTime DateTimePicker::dateTime() const
{
	return QDateTime(m_date, m_time);
}

void DateTimePicker::setDateTime(const QDateTime &dateTime)
{
	if (!dateTime.isValid())
		return;
	m_date = dateTime.date();
	m_time = dateTime.time();
	SyncControls();
}

void DateTimePicker::ApplyByUser(const QDate &date, const QTime &time)
{
	m_date = date;
	m_time = time;
	SyncControls();
	emit dateTimeChanged(dateTime());
}

void DateTimePicker::TimeControlsEdited()
{
	int hour = m_hourStepper->value();
	if (m_12Hour) {
		hour %= 12;
		if (m_meridiem && m_meridiem->isPm())
			hour += 12;
	}
	// Times picked here are whole minutes; seconds only survive from a
	// saved target until the time is edited
	ApplyByUser(m_date, QTime(hour, m_minuteStepper->value()));
}

void DateTimePicker::SyncControls()
{
	m_calendar->setSelectedDate(m_date);

	const int hour = m_time.hour();
	if (m_12Hour) {
		m_hourStepper->setValue(hour % 12 == 0 ? 12 : hour % 12);
		if (m_meridiem)
			m_meridiem->setPm(hour >= 12);
	} else {
		m_hourStepper->setValue(hour);
	}
	m_minuteStepper->setValue(m_time.minute());

	UpdateMonthHeader();
	UpdateSummary();
}

void DateTimePicker::UpdateMonthHeader()
{
	m_monthLabel->setText(QString("%1 %2")
				      .arg(locale().standaloneMonthName(m_calendar->shownMonth(), QLocale::LongFormat))
				      .arg(m_calendar->shownYear()));
	m_prevMonthBtn->setEnabled(m_calendar->canShowPreviousMonth());
}

void DateTimePicker::UpdateSummary()
{
	const QLocale loc = locale();
	const QDate today = QDate::currentDate();

	// Explicit formats: some locales' short format includes seconds. Saved
	// targets can carry seconds; those are shown rather than hidden.
	QString fmt = m_12Hour ? QStringLiteral("h:mm") : QStringLiteral("HH:mm");
	if (m_time.second() != 0)
		fmt += QStringLiteral(":ss");
	if (m_12Hour)
		fmt += QStringLiteral(" AP");
	m_whenTime->setText(loc.toString(m_time, fmt));

	const QString longDate = loc.toString(m_date, QLocale::LongFormat);
	QString dayName;
	if (m_date == today)
		dayName = QStringLiteral("Today");
	else if (m_date == today.addDays(1))
		dayName = QStringLiteral("Tomorrow");
	m_whenDate->setText(dayName.isEmpty() ? longDate : QString::fromUtf8("%1 · %2").arg(dayName, longDate));

	const qint64 secs = QDateTime::currentDateTime().secsTo(dateTime());
	const bool past = secs <= 0;
	m_relative->setText(past ? QStringLiteral("In the past") : QString("in %1").arg(FormatSpan(secs)));
	m_relative->setToolTip(past ? QStringLiteral("This moment has passed, so the timer will read zero.")
				    : QStringLiteral("How long the timer will count down from now"));
	m_relative->setProperty("past", past);
	UpdateColors();
}

void DateTimePicker::UpdateColors()
{
	const QPalette pal = palette();
	const QColor accent = pal.color(QPalette::Highlight);
	const QColor text = pal.color(QPalette::WindowText);
	const bool past = m_relative->property("past").toBool();
	const QColor pill = past ? kPastColor : accent;

	// Scoped by object name so neither tooltips nor child widgets inherit
	// these rules
	const QString style =
		QString("QFrame#tminusDateTimePicker { background-color: rgba(127, 127, 127, 7%); "
			"border: 1px solid rgba(127, 127, 127, 25%); border-radius: 8px; }"
			"QWidget#tminusPickerSummary { border-bottom: 1px solid rgba(127, 127, 127, 20%); }"
			"QWidget#tminusPickerPresets { border-top: 1px solid rgba(127, 127, 127, 20%); }"
			"QFrame#tminusPickerDivider { background-color: rgba(127, 127, 127, 20%); border: none; }"
			"QLabel#tminusWhenTime { font-size: 22px; font-weight: bold; background: transparent; }"
			"QLabel#tminusWhenDate { color: %1; background: transparent; }"
			"QLabel#tminusMonthLabel { font-weight: bold; font-size: 14px; background: transparent; }"
			"QLabel#tminusPickerCaption { color: %1; font-size: 10px; font-weight: bold; "
			"background: transparent; }"
			"QLabel#tminusPickerColon { font-size: 24px; font-weight: bold; color: %1; "
			"background: transparent; }"
			"QLabel#tminusRelative { background-color: %2; border: 1px solid %3; color: %4; "
			"border-radius: 12px; padding: 4px 12px; font-weight: bold; }")
			.arg(Rgba(text, 60), Rgba(pill, 22), Rgba(pill, 60),
			     past ? kPastColor.name() : pal.color(QPalette::WindowText).name());
	// Only on a real change: re-applying a style sheet repolishes, which
	// can itself deliver a palette change and land back here
	if (style != styleSheet())
		setStyleSheet(style);

	m_prevMonthBtn->setIcon(ChevronIcon(Qt::LeftArrow, pal.color(QPalette::ButtonText)));
	m_nextMonthBtn->setIcon(ChevronIcon(Qt::RightArrow, pal.color(QPalette::ButtonText)));
}

void DateTimePicker::changeEvent(QEvent *event)
{
	QFrame::changeEvent(event);
	if (event->type() == QEvent::PaletteChange)
		UpdateColors();
}

void DateTimePicker::showEvent(QShowEvent *event)
{
	QFrame::showEvent(event);
	m_calendar->setMinimumDate(QDate::currentDate());
	UpdateSummary();
	m_refresh->start();
}

void DateTimePicker::hideEvent(QHideEvent *event)
{
	QFrame::hideEvent(event);
	m_refresh->stop();
}
