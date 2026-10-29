#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QTimer>

const int TIMER_TICK_INTERVAL = 100;

enum class TimerType {
	Countdown = 0,
	Stopwatch = 1,
	TimeUntil = 2,
};

enum class TimerState {
	Stopped = 0,
	Running = 1,
	Paused = 2,
	Finished = 3,
};

enum class ActionType {
	SetText = 0,
	EnableFilter = 1,
	SwitchScene = 2,
	Delay = 3,
	TriggerGlobalHotkey = 4,
	TriggerSourceHotkey = 5,
};

enum class Direction { Up, Down };

struct DurationValue {
	int days = 0;
	int hours = 0;
	int minutes = 0;
	int seconds = 0;
};

struct DisplayFormat {
	// Auto shows days and hours only while they're non-zero (minutes and
	// seconds always); the show* flags below are used when it's off.
	bool autoUnits = false;
	bool showDays = false;
	bool showHours = true;
	bool showMinutes = true;
	bool showSeconds = true;
	bool showLeadingZero = true;
	bool useFormatString = false;
	QString formatString = "%time%";
};

struct ActionStep {
	ActionType type = ActionType::SetText;

	// SetText
	QString text;

	// EnableFilter
	QString filterSourceName;
	QString filterName;
	bool filterEnabled = true;

	// SwitchScene
	QString sceneName;

	// Delay (milliseconds)
	int delayMs = 1000;

	// TriggerGlobalHotkey
	QString globalHotkeyName;

	// TriggerSourceHotkey
	QString sourceHotkeySourceName;
	QString sourceHotkeyName;
};

struct AddTimeHotkeyEntry {
	int hotkeyId = -1;
	QString label;
	int deltaSeconds = 0;
	QString hotkeyRegistrationName;
};

struct TimerData {
	QString timerId;
	QString displayName;

	TimerType timerType = TimerType::Countdown;

	// Countdown duration
	DurationValue countdownDuration;

	// TimeUntil target
	QDateTime targetDateTime;

	// Display
	DisplayFormat display;
	QString selectedTextSource;

	// Text written to the source once the timer finishes, in place of the
	// final time; empty keeps the time showing
	QString finishedText;

	// Stream behavior
	bool startOnStreamStart = false;
	bool resetOnStreamStart = false;

	// Runtime state
	TimerState state = TimerState::Stopped;
	QTimer *tickTimer = nullptr;
	long long timeLeftMs = 0;
	QDateTime timeAtStart;
	long long pausedTimeMs = 0;

	// Hotkeys
	int startPauseHotkeyId = -1;
	int restartHotkeyId = -1;
	QList<AddTimeHotkeyEntry> addTimeHotkeys;

	// End actions
	QList<ActionStep> endActions;
};
