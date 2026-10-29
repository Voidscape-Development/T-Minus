#include "timer-engine.hpp"
#include "../utils/format-utils.hpp"

TimerEngine::TimerEngine(QObject *parent) : QObject(parent)
{
	m_tickTimer = new QTimer(this);
	m_tickTimer->setInterval(TIMER_TICK_INTERVAL);
	connect(m_tickTimer, &QTimer::timeout, this, &TimerEngine::onTick);
}

TimerEngine::~TimerEngine()
{
	m_tickTimer->stop();
}

void TimerEngine::configure(TimerType type, const DurationValue &countdown, const QDateTime &target)
{
	m_configType = type;
	m_configInitialMs = type == TimerType::Countdown ? DurationToMs(countdown) : 0;
	m_configTarget = target;

	// Editing a running or paused timer must not throw away its progress
	if (m_state == TimerState::Stopped || m_state == TimerState::Finished) {
		applyConfig();
		resetTime();
	}
}

void TimerEngine::applyConfig()
{
	m_type = m_configType;
	m_initialMs = m_configInitialMs;
	m_targetDateTime = m_configTarget;
}

void TimerEngine::resetTime()
{
	m_lastDisplayedSeconds = -1;

	switch (m_type) {
	case TimerType::Countdown:
		m_timeMs = m_initialMs;
		break;
	case TimerType::Stopwatch:
		m_timeMs = 0;
		break;
	case TimerType::TimeUntil:
		m_timeMs = QDateTime::currentDateTime().msecsTo(m_targetDateTime);
		if (m_timeMs < 0)
			m_timeMs = 0;
		break;
	}
}

void TimerEngine::start()
{
	if (m_state == TimerState::Running)
		return;

	// Starting a finished timer begins a fresh run; resuming from zero
	// would finish again on the first tick and re-fire the end actions.
	if (m_state == TimerState::Finished) {
		applyConfig();
		resetTime();
	}

	m_lastDisplayedSeconds = -1;
	m_elapsed.start();
	m_msAtStart = m_timeMs;
	m_tickTimer->start(TIMER_TICK_INTERVAL);
	setState(TimerState::Running);
}

void TimerEngine::pause()
{
	if (m_state != TimerState::Running)
		return;

	m_tickTimer->stop();
	setState(TimerState::Paused);
}

void TimerEngine::restart()
{
	m_tickTimer->stop();
	applyConfig();
	resetTime();
	setState(TimerState::Stopped);
	emit tick(m_timeMs);
}

void TimerEngine::stop()
{
	m_tickTimer->stop();
	setState(TimerState::Stopped);
}

void TimerEngine::addTime(int deltaMs)
{
	if (m_type == TimerType::TimeUntil) {
		// Time-until is recomputed from the target on every tick, so
		// adjusting it means moving the target of the current run.
		m_targetDateTime = m_targetDateTime.addMSecs(deltaMs);
		setTimeMs(QDateTime::currentDateTime().msecsTo(m_targetDateTime));
		return;
	}

	setTimeMs(m_timeMs + deltaMs);
}

void TimerEngine::setTimeMs(long long ms)
{
	if (m_type == TimerType::TimeUntil)
		m_targetDateTime = QDateTime::currentDateTime().addMSecs(ms);

	m_timeMs = ms;
	if (m_timeMs < 0)
		m_timeMs = 0;

	if (m_state == TimerState::Running) {
		m_elapsed.start();
		m_msAtStart = m_timeMs;
	}

	m_lastDisplayedSeconds = m_timeMs / 1000;
	emit tick(m_timeMs);
}

void TimerEngine::setState(TimerState s)
{
	if (m_state == s)
		return;
	m_state = s;
	emit stateChanged(m_state);
}

void TimerEngine::finish()
{
	m_timeMs = 0;
	m_tickTimer->stop();
	m_lastDisplayedSeconds = 0;
	emit tick(m_timeMs);
	setState(TimerState::Finished);
	emit finished();
}

void TimerEngine::onTick()
{
	switch (m_type) {
	case TimerType::Countdown:
		m_timeMs = m_msAtStart - m_elapsed.elapsed();
		if (m_timeMs <= 0) {
			finish();
			return;
		}
		break;

	case TimerType::Stopwatch:
		m_timeMs = m_msAtStart + m_elapsed.elapsed();
		break;

	case TimerType::TimeUntil:
		m_timeMs = QDateTime::currentDateTime().msecsTo(m_targetDateTime);
		if (m_timeMs <= 0) {
			finish();
			return;
		}
		break;
	}

	long long currentSeconds = m_timeMs / 1000;
	if (currentSeconds != m_lastDisplayedSeconds) {
		m_lastDisplayedSeconds = currentSeconds;
		emit tick(m_timeMs);
	}
}
