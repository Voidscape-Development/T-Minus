#pragma once

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QElapsedTimer>

#include "timer-data.hpp"

class TimerEngine : public QObject {
	Q_OBJECT

public:
	explicit TimerEngine(QObject *parent = nullptr);
	~TimerEngine();

	// Store a new configuration. It takes effect immediately when the
	// timer is idle (stopped or finished); a running or paused timer keeps
	// its current run and picks the configuration up on the next restart.
	void configure(TimerType type, const DurationValue &countdown, const QDateTime &target);
	void start();
	void pause();
	void restart();
	void stop();
	void addTime(int deltaMs);

	TimerState state() const { return m_state; }
	TimerType timerType() const { return m_type; }
	long long currentTimeMs() const { return m_timeMs; }
	long long initialMs() const { return m_initialMs; }
	QDateTime targetDateTime() const { return m_targetDateTime; }

	void setTimeMs(long long ms);

signals:
	void tick(long long currentMs);
	void finished();
	void stateChanged(TimerState newState);

private slots:
	void onTick();

private:
	void applyConfig();
	void resetTime();
	void finish();
	void setState(TimerState s);

	QTimer *m_tickTimer;
	TimerState m_state = TimerState::Stopped;

	// Configuration as last set; applied on idle configure() and restart()
	TimerType m_configType = TimerType::Countdown;
	long long m_configInitialMs = 0;
	QDateTime m_configTarget;

	// Configuration of the current run
	TimerType m_type = TimerType::Countdown;
	long long m_initialMs = 0;
	QDateTime m_targetDateTime;

	// Runtime. Countdown and stopwatch measure elapsed time on a monotonic
	// clock so system clock adjustments don't make them jump; time-until
	// is anchored to the wall clock by definition.
	long long m_timeMs = 0;
	QElapsedTimer m_elapsed;
	long long m_msAtStart = 0;
	long long m_lastDisplayedSeconds = -1;
};
