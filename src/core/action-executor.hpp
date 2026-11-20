#pragma once

#include <obs.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QTimer>

#include <functional>

#include "timer-data.hpp"

class ActionExecutor : public QObject {
	Q_OBJECT

public:
	explicit ActionExecutor(QObject *parent = nullptr);
	~ActionExecutor();

	// values fills %key% placeholders (time, name, ...) in Set Text steps
	void execute(const QList<ActionStep> &actions, const QString &textSourceName,
		     const QHash<QString, QString> &values);
	void cancel();

	// Runs Timer Control steps; set by the dock, which owns the timers
	static std::function<void(const QString &timerId, TimerControlMode mode, int seconds)> timerControl;

private:
	void executeNextStep();
	void executeStep(const ActionStep &step);

	QList<ActionStep> m_actions;
	QString m_textSourceName;
	QHash<QString, QString> m_values;
	int m_currentStep = 0;
	QTimer *m_delayTimer;
};

// Step lists as stored in the config
obs_data_array_t *SaveSteps(const QList<ActionStep> &steps);
QList<ActionStep> LoadSteps(obs_data_array_t *array);
