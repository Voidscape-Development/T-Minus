#include "format-utils.hpp"

long long DurationToMs(const DurationValue &d)
{
	long long ms = 0;
	ms += static_cast<long long>(d.days) * 86400000LL;
	ms += static_cast<long long>(d.hours) * 3600000LL;
	ms += static_cast<long long>(d.minutes) * 60000LL;
	ms += static_cast<long long>(d.seconds) * 1000LL;
	return ms;
}

DurationValue MsToDuration(long long ms)
{
	if (ms < 0)
		ms = 0;

	DurationValue d;
	d.days = static_cast<int>(ms / 86400000LL);
	ms %= 86400000LL;
	d.hours = static_cast<int>(ms / 3600000LL);
	ms %= 3600000LL;
	d.minutes = static_cast<int>(ms / 60000LL);
	ms %= 60000LL;
	d.seconds = static_cast<int>(ms / 1000LL);
	return d;
}

static QString BuildTimeString(const DisplayFormat &format, long long timeMs)
{
	DurationValue d = MsToDuration(timeMs);

	DisplayFormat fmt = format;
	if (fmt.autoUnits) {
		fmt.showDays = d.days > 0;
		fmt.showHours = d.days > 0 || d.hours > 0;
		fmt.showMinutes = true;
		fmt.showSeconds = true;
	}

	// Fold hidden leading units into the next unit down so no time is
	// lost: 1:05:00 shown as MM:SS reads 65:00, not 05:00.
	if (!fmt.showDays) {
		d.hours += d.days * 24;
		d.days = 0;
	}
	if (!fmt.showHours) {
		d.minutes += d.hours * 60;
		d.hours = 0;
	}
	if (!fmt.showMinutes) {
		d.seconds += d.minutes * 60;
		d.minutes = 0;
	}

	QString result;
	bool isFirst = true;

	auto appendField = [&](int value, bool show) {
		if (!show)
			return;
		if (!isFirst)
			result += ":";
		if (isFirst && !fmt.showLeadingZero) {
			result += QString::number(value);
		} else {
			result += QString("%1").arg(value, 2, 10, QChar('0'));
		}
		isFirst = false;
	};

	appendField(d.days, fmt.showDays);
	appendField(d.hours, fmt.showHours);
	appendField(d.minutes, fmt.showMinutes);
	appendField(d.seconds, fmt.showSeconds);

	if (result.isEmpty())
		result = "0";

	return result;
}

QString FormatTimerDisplay(const DisplayFormat &fmt, long long timeMs)
{
	QString timeStr = BuildTimeString(fmt, timeMs);

	if (fmt.useFormatString && !fmt.formatString.isEmpty()) {
		QString output = fmt.formatString;
		output.replace("%time%", timeStr);
		return output;
	}

	return timeStr;
}
