#include "format-utils.hpp"

#include <algorithm>

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

namespace {

// Compares a and b chunk by chunk: runs of digits by numeric value, other
// text case-insensitively and locale-aware. Done by hand because
// QCollator's numeric mode isn't supported by every Qt collation backend.
int NaturalCompare(const QString &a, const QString &b)
{
	int i = 0, j = 0;
	while (i < a.size() && j < b.size()) {
		const bool aDigit = a[i].isDigit();
		const bool bDigit = b[j].isDigit();

		if (aDigit && bDigit) {
			int iEnd = i, jEnd = j;
			while (iEnd < a.size() && a[iEnd].isDigit())
				iEnd++;
			while (jEnd < b.size() && b[jEnd].isDigit())
				jEnd++;

			// Compare by value: skip leading zeros, then the longer
			// number is larger, then digit by digit
			int iStart = i, jStart = j;
			while (iStart < iEnd - 1 && a[iStart] == QLatin1Char('0'))
				iStart++;
			while (jStart < jEnd - 1 && b[jStart] == QLatin1Char('0'))
				jStart++;
			const int aLen = iEnd - iStart, bLen = jEnd - jStart;
			if (aLen != bLen)
				return aLen < bLen ? -1 : 1;
			for (int k = 0; k < aLen; k++) {
				const int da = a[iStart + k].digitValue(), db = b[jStart + k].digitValue();
				if (da != db)
					return da < db ? -1 : 1;
			}
			// Equal values: fewer leading zeros first ("1" before "01")
			if (iEnd - i != jEnd - j)
				return (iEnd - i) < (jEnd - j) ? -1 : 1;
			i = iEnd;
			j = jEnd;
			continue;
		}

		if (aDigit != bDigit) {
			// Numbers sort before text at the same position
			return aDigit ? -1 : 1;
		}

		int iEnd = i, jEnd = j;
		while (iEnd < a.size() && !a[iEnd].isDigit())
			iEnd++;
		while (jEnd < b.size() && !b[jEnd].isDigit())
			jEnd++;
		const QString aText = a.mid(i, iEnd - i).toCaseFolded();
		const QString bText = b.mid(j, jEnd - j).toCaseFolded();
		const int order = QString::localeAwareCompare(aText, bText);
		if (order != 0)
			return order;
		i = iEnd;
		j = jEnd;
	}

	if (i < a.size() || j < b.size())
		return i < a.size() ? 1 : -1;
	return 0;
}

} // namespace

void NaturalSort(QStringList &names)
{
	std::stable_sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
		const int order = NaturalCompare(a, b);
		// Names differing only in case still get a predictable order
		return order != 0 ? order < 0 : a < b;
	});
}
