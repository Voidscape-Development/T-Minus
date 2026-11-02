#pragma once

#include <QDate>
#include <QDateTime>
#include <QFrame>
#include <QTime>
#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;

// Month grid with large, round day cells. Days before the minimum date are
// shown dimmed and can't be picked; days of the neighbouring months fill the
// grid and switch the month when picked. Arrow keys move the selection,
// Page Up/Down and the mouse wheel change the month, Home goes to today.
class CalendarGrid : public QWidget {
	Q_OBJECT

public:
	explicit CalendarGrid(QWidget *parent = nullptr);

	QDate selectedDate() const { return m_selected; }
	void setSelectedDate(const QDate &date);
	void setMinimumDate(const QDate &date);

	int shownYear() const { return m_year; }
	int shownMonth() const { return m_month; }
	void showMonth(int year, int month);
	void showPreviousMonth();
	void showNextMonth();
	bool canShowPreviousMonth() const;

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	// Only for selections made by the user
	void dateSelected(const QDate &date);
	void shownMonthChanged(int year, int month);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	QDate FirstShownDate() const;
	QRectF CellRect(int index) const;
	int CellAt(const QPoint &pos) const;
	bool IsSelectable(const QDate &date) const;
	void SelectByUser(const QDate &date);

	QDate m_selected;
	QDate m_minimum;
	int m_year;
	int m_month;
	int m_hover = -1;
	int m_wheelDelta = 0;
};

// One column of a time picker: chevrons above and below a large number.
// Click or hold the chevrons, scroll, use the arrow keys, or type digits.
// Values wrap around.
class TimeStepper : public QWidget {
	Q_OBJECT

public:
	TimeStepper(int minimum, int maximum, bool zeroPadded, const QString &accessibleName,
		    QWidget *parent = nullptr);

	int value() const { return m_value; }
	void setValue(int value);
	void setRange(int minimum, int maximum);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override { return sizeHint(); }

signals:
	// Only for changes made by the user
	void edited(int value);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void focusOutEvent(QFocusEvent *event) override;

private:
	enum Zone { NoZone, UpZone, ValueZone, DownZone };
	Zone ZoneAt(const QPoint &pos) const;
	QRectF ZoneRect(Zone zone) const;
	void Step(int delta);
	void CommitTyped();

	int m_min;
	int m_max;
	int m_value;
	bool m_padded;
	Zone m_hover = NoZone;
	Zone m_pressed = NoZone;
	int m_wheelDelta = 0;
	QTimer *m_repeat;
	QTimer *m_typeTimeout;
	QString m_typed;
};

// Stacked AM / PM switch for 12-hour locales
class MeridiemToggle : public QWidget {
	Q_OBJECT

public:
	explicit MeridiemToggle(QWidget *parent = nullptr);

	bool isPm() const { return m_pm; }
	void setPm(bool pm);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override { return sizeHint(); }

signals:
	void edited(bool pm);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	QRectF SegmentRect(bool pm) const;
	void SetByUser(bool pm);

	bool m_pm = false;
	int m_hover = -1;
};

// Date and time picker for Time Until timers: a summary of the chosen
// moment and how far away it is, a month calendar beside a time picker, and
// one-click presets.
class DateTimePicker : public QFrame {
	Q_OBJECT

public:
	explicit DateTimePicker(QWidget *parent = nullptr);

	QDateTime dateTime() const;
	void setDateTime(const QDateTime &dateTime);

signals:
	void dateTimeChanged(const QDateTime &dateTime);

protected:
	void changeEvent(QEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;

private:
	QWidget *CreateCalendarPanel();
	QWidget *CreateTimePanel();
	QWidget *CreatePresetRow();

	void ApplyByUser(const QDate &date, const QTime &time);
	void TimeControlsEdited();
	void SyncControls();
	void UpdateMonthHeader();
	void UpdateSummary();
	void UpdateColors();

	CalendarGrid *m_calendar;
	QLabel *m_monthLabel;
	QPushButton *m_prevMonthBtn;
	QPushButton *m_nextMonthBtn;
	QPushButton *m_todayBtn;

	TimeStepper *m_hourStepper;
	TimeStepper *m_minuteStepper;
	MeridiemToggle *m_meridiem = nullptr;

	QLabel *m_whenTime;
	QLabel *m_whenDate;
	QLabel *m_relative;
	QTimer *m_refresh;

	QDate m_date;
	QTime m_time;
	bool m_12Hour;
};
