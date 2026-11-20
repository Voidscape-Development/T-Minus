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

void TimerEngine::setResolution(int ms)
{
	m_resolution = ms;
	// Hundredths can't change faster than OBS renders, so ~60 Hz is enough
	m_tickTimer->setInterval(qMin(TIMER_TICK_INTERVAL, ms < 100 ? 15 : ms));
}

void TimerEngine::resetTime()
{
	m_lastUnit = LLONG_MIN;
	m_finishFired = false;

	switch (m_type) {
	case TimerType::Countdown:
		m_timeMs = m_initialMs;
		break;
	case TimerType::Stopwatch:
		m_timeMs = 0;
		break;
	case TimerType::TimeUntil:
		m_timeMs = QDateTime::currentDateTime().msecsTo(m_targetDateTime);
		if (m_timeMs < 0 && !m_overtime)
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

	m_lastUnit = LLONG_MIN;
	m_elapsed.start();
	m_msAtStart = m_timeMs;
	m_tickTimer->start();
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
	if (m_timeMs < 0 && (!m_overtime || m_type == TimerType::Stopwatch))
		m_timeMs = 0;

	if (m_state == TimerState::Running) {
		m_elapsed.start();
		m_msAtStart = m_timeMs;
	}

	m_lastUnit = m_timeMs / m_resolution;
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
	m_lastUnit = 0;
	m_finishFired = true;
	emit tick(m_timeMs);
	setState(TimerState::Finished);
	emit finished();
}

void TimerEngine::onTick()
{
	if (m_type == TimerType::Stopwatch) {
		m_timeMs = m_msAtStart + m_elapsed.elapsed();
	} else {
		m_timeMs = m_type == TimerType::Countdown ? m_msAtStart - m_elapsed.elapsed()
							  : QDateTime::currentDateTime().msecsTo(m_targetDateTime);
		if (m_timeMs <= 0 && !m_overtime) {
			finish();
			return;
		}
	}

	// Floored, so overtime shows -0:00 as soon as it passes zero
	const long long unit = (m_timeMs < 0 ? m_timeMs - m_resolution + 1 : m_timeMs) / m_resolution;
	if (unit != m_lastUnit) {
		m_lastUnit = unit;
		emit tick(m_timeMs);
	}

	// Overtime runs on past zero: the end actions fire once, the timer
	// keeps going
	if (m_timeMs <= 0 && m_type != TimerType::Stopwatch && !m_finishFired) {
		m_finishFired = true;
		emit finished();
	}
}
