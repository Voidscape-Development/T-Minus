#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QTime>

#include "../core/timer-data.hpp"

long long DurationToMs(const DurationValue &d);
DurationValue MsToDuration(long long ms);
// The bare time, without the format string around it
QString FormatTimerDisplay(const DisplayFormat &fmt, long long timeMs);
// The timer's text template: its format string, or just "%time%"
QString TemplateText(const DisplayFormat &fmt);
// Replaces each %key% in text with its value
QString ExpandPlaceholders(QString text, const QHash<QString, QString> &values);

// Sorts names the way people read them: case-insensitively and with runs of
// digits compared by value, so "Scene 2" comes before "Scene 10".
void NaturalSort(QStringList &names);
