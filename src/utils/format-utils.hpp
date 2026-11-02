#pragma once

#include <QString>
#include <QStringList>
#include <QTime>

#include "../core/timer-data.hpp"

long long DurationToMs(const DurationValue &d);
DurationValue MsToDuration(long long ms);
QString FormatTimerDisplay(const DisplayFormat &fmt, long long timeMs);

// Sorts names the way people read them: case-insensitively and with runs of
// digits compared by value, so "Scene 2" comes before "Scene 10".
void NaturalSort(QStringList &names);
