#pragma once

#include <QString>

// What a timer currently shows, pushed to every T-Minus source linked to it
struct TimerSnapshot {
	QString text;
	long long timeMs = 0;
	// Fraction of a countdown left (1 to 0), or negative when there's none
	double progress = -1.0;
	bool countsDown = false;
};

void RegisterTMinusSource();
void PublishTimerSnapshot(const QString &timerId, const TimerSnapshot &snapshot);
