#pragma once

#include <obs.h>

#include <QWidget>

#include "../core/timer-data.hpp"
#include "../core/timer-engine.hpp"

namespace Ui {
class TimerWidget;
}

class ActionExecutor;

class TimerWidget : public QWidget {
	Q_OBJECT

public:
	explicit TimerWidget(QWidget *parent = nullptr, obs_data_t *savedData = nullptr);
	~TimerWidget();

	void SaveData(obs_data_t *dataObj);
	void LoadData(obs_data_t *dataObj);

	void RegisterHotkeys(obs_data_t *savedData);
	void ReregisterAllHotkeys();
	void UnregisterHotkeys();

	// Stop the tick timer and cancel any in-progress end-action sequence.
	// Called before deleteLater() so a queued QTimer tick can't drive OBS
	// mutations against a widget that's about to be destroyed.
	void Quiesce();

	void StartTimer();
	void PauseTimer();
	void ToggleStartPause();
	void RestartTimer();

	TimerData &data() { return m_data; }
	const QString &timerId() const { return m_data.timerId; }

signals:
	void removeRequested(const QString &id);
	void moveRequested(Direction dir, const QString &id);
	void settingsChanged();

private slots:
	void OnTick(long long currentMs);
	void OnFinished();
	void OnStateChanged(TimerState newState);
	void OnSettingsClicked();

private:
	void SetupUI();
	void UpdateDisplay(long long timeMs);
	void UpdateSourceText(const QString &text);
	void UpdatePlayPauseButton();
	void UpdateHeaderLabels();

	Ui::TimerWidget *m_ui;
	TimerData m_data;
	TimerEngine *m_engine;
	ActionExecutor *m_actionExecutor;
};
