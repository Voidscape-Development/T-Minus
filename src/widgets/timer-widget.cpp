#include "timer-widget.hpp"
#include "ui_timer-widget.h"
#include "timer-settings-dialog.hpp"
#include "../core/action-executor.hpp"
#include "../utils/obs-utils.hpp"
#include "../utils/format-utils.hpp"
#include "../plugin-support.h"

#include <obs.h>
#include <obs-module.h>

#include <QHBoxLayout>
#include <QPushButton>

static const char *TimerTypeToString(TimerType t)
{
	switch (t) {
	case TimerType::Countdown:
		return "Countdown";
	case TimerType::Stopwatch:
		return "Stopwatch";
	case TimerType::TimeUntil:
		return "Time Until";
	}
	return "Unknown";
}

TimerWidget::TimerWidget(QWidget *parent, obs_data_t *savedData) : QWidget(parent), m_ui(new Ui::TimerWidget)
{
	m_engine = new TimerEngine(this);
	m_actionExecutor = new ActionExecutor(this);

	if (savedData) {
		LoadData(savedData);
	} else {
		m_data.timerId = GenerateUniqueID();
		m_data.countdownDuration = {0, 0, 5, 0};
	}

	SetupUI();

	connect(m_engine, &TimerEngine::tick, this, &TimerWidget::OnTick);
	connect(m_engine, &TimerEngine::finished, this, &TimerWidget::OnFinished);
	connect(m_engine, &TimerEngine::stateChanged, this, &TimerWidget::OnStateChanged);

	m_engine->configure(m_data.timerType, m_data.countdownDuration, m_data.targetDateTime);
	UpdateDisplay(m_engine->currentTimeMs());

	// Register hotkeys immediately so timers added at runtime get them
	// without an OBS restart. savedData restores saved key bindings.
	RegisterHotkeys(savedData);
}

TimerWidget::~TimerWidget()
{
	UnregisterHotkeys();
	delete m_ui;
}

void TimerWidget::SetupUI()
{
	m_ui->setupUi(this);

	// Give mainContent (index 0) all extra horizontal space; sideButtons (index 1)
	// stays at its natural fixed width. The stretch property in .ui format is only
	// handled by Qt Designer's runtime form builder, not uic, so we set it here.
	if (auto *outerRow = qobject_cast<QHBoxLayout *>(m_ui->m_groupBox->layout()))
		outerRow->setStretch(0, 1);

	// setContentsMargins(0,0,0,0) on labels eliminates Qt's internal widget
	// padding that stylesheet margin/padding properties cannot control.
	m_ui->m_nameLabel->setContentsMargins(0, 0, 0, 0);
	m_ui->m_typeLabel->setContentsMargins(0, 0, 0, 0);

	UpdateHeaderLabels();

	connect(m_ui->m_playPauseBtn, &QPushButton::clicked, this, &TimerWidget::ToggleStartPause);
	connect(m_ui->m_restartBtn, &QPushButton::clicked, this, &TimerWidget::RestartTimer);
	connect(m_ui->m_settingsBtn, &QPushButton::clicked, this, &TimerWidget::OnSettingsClicked);
	connect(m_ui->m_deleteBtn, &QPushButton::clicked, this, [this]() { emit removeRequested(m_data.timerId); });
	connect(m_ui->m_moveUpBtn, &QPushButton::clicked, this,
		[this]() { emit moveRequested(Direction::Up, m_data.timerId); });
	connect(m_ui->m_moveDownBtn, &QPushButton::clicked, this,
		[this]() { emit moveRequested(Direction::Down, m_data.timerId); });
}

void TimerWidget::UpdateHeaderLabels()
{
	if (m_data.displayName.isEmpty()) {
		m_ui->m_nameLabel->setText(TimerTypeToString(m_data.timerType));
		m_ui->m_typeLabel->hide();
	} else {
		m_ui->m_nameLabel->setText(m_data.displayName);
		m_ui->m_typeLabel->setText(TimerTypeToString(m_data.timerType));
		m_ui->m_typeLabel->show();
	}
}

void TimerWidget::StartTimer()
{
	// A fresh run supersedes whatever the previous run's end actions are
	// still doing (e.g. waiting on a Delay step before a scene switch).
	if (m_engine->state() != TimerState::Running && m_engine->state() != TimerState::Paused)
		m_actionExecutor->cancel();
	m_engine->start();
}

void TimerWidget::PauseTimer()
{
	m_engine->pause();
}

void TimerWidget::ToggleStartPause()
{
	if (m_engine->state() == TimerState::Running)
		m_engine->pause();
	else
		StartTimer();
}

void TimerWidget::RestartTimer()
{
	m_actionExecutor->cancel();
	m_engine->restart();
}

void TimerWidget::OnTick(long long currentMs)
{
	m_data.timeLeftMs = currentMs;
	UpdateDisplay(currentMs);
}

void TimerWidget::OnFinished()
{
	obs_log(LOG_INFO, "Timer %s finished", m_data.timerId.toUtf8().constData());

	if (!m_data.endActions.isEmpty()) {
		// %time% in Set Text steps is the bare time value, without the
		// timer's own format string wrapped around it.
		DisplayFormat timeOnly = m_data.display;
		timeOnly.useFormatString = false;
		m_actionExecutor->execute(m_data.endActions, m_data.selectedTextSource,
					  FormatTimerDisplay(timeOnly, m_engine->currentTimeMs()));
	}
}

void TimerWidget::OnStateChanged(TimerState newState)
{
	m_data.state = newState;
	UpdatePlayPauseButton();
}

void TimerWidget::UpdateDisplay(long long timeMs)
{
	QString displayText = FormatTimerDisplay(m_data.display, timeMs);
	m_ui->m_timeDisplay->setText(displayText);
	UpdateSourceText(displayText);
}

void TimerWidget::UpdateSourceText(const QString &text)
{
	if (m_data.selectedTextSource.isEmpty())
		return;

	obs_source_t *source = obs_get_source_by_name(m_data.selectedTextSource.toUtf8().constData());
	if (!source)
		return;

	obs_data_t *settings = obs_source_get_settings(source);
	obs_data_set_string(settings, "text", text.toUtf8().constData());
	obs_source_update(source, settings);
	obs_data_release(settings);
	obs_source_release(source);
}

void TimerWidget::UpdatePlayPauseButton()
{
	if (m_engine->state() == TimerState::Running) {
		m_ui->m_playPauseBtn->setProperty("themeID", "pauseIcon");
		m_ui->m_playPauseBtn->setProperty("class", "icon-media-pause");
		m_ui->m_playPauseBtn->setToolTip("Pause");
	} else {
		m_ui->m_playPauseBtn->setProperty("themeID", "playIcon");
		m_ui->m_playPauseBtn->setProperty("class", "icon-media-play");
		m_ui->m_playPauseBtn->setToolTip("Start");
	}
	m_ui->m_playPauseBtn->style()->unpolish(m_ui->m_playPauseBtn);
	m_ui->m_playPauseBtn->style()->polish(m_ui->m_playPauseBtn);
}

void TimerWidget::OnSettingsClicked()
{
	TimerSettingsDialog dialog(m_data, this);
	if (dialog.exec() == QDialog::Accepted) {
		const QString oldName = m_data.displayName;
		dialog.ApplyToData(m_data);

		UpdateHeaderLabels();
		m_engine->configure(m_data.timerType, m_data.countdownDuration, m_data.targetDateTime);
		UpdateDisplay(m_engine->currentTimeMs());

		if (m_data.displayName != oldName) {
			// Re-register (preserving bindings) so hotkey
			// descriptions pick up the new name.
			ReregisterAllHotkeys();
		} else {
			// Register any add-time hotkeys created in the dialog.
			RegisterHotkeys(nullptr);
		}

		emit settingsChanged();
	}
}

void TimerWidget::RegisterHotkeys(obs_data_t *savedData)
{
	std::string idStr = m_data.timerId.toStdString();
	// Use the display name in hotkey descriptions so entries in
	// OBS Settings > Hotkeys are recognizable.
	QString timerLabel = m_data.displayName.isEmpty() ? m_data.timerId : m_data.displayName;

	if (m_data.startPauseHotkeyId == -1) {
		std::string startPauseName = "TMinus_StartPause_" + idStr;
		std::string startPauseDesc = QString("T-Minus: Start/Pause Timer (%1)").arg(timerLabel).toStdString();
		LoadHotkey(
			m_data.startPauseHotkeyId, startPauseName.c_str(), startPauseDesc.c_str(), this,
			[this]() { ToggleStartPause(); }, "Start/Pause Timer " + idStr, savedData);
	}

	if (m_data.restartHotkeyId == -1) {
		std::string restartName = "TMinus_Restart_" + idStr;
		std::string restartDesc = QString("T-Minus: Restart Timer (%1)").arg(timerLabel).toStdString();
		LoadHotkey(
			m_data.restartHotkeyId, restartName.c_str(), restartDesc.c_str(), this,
			[this]() { RestartTimer(); }, "Restart Timer " + idStr, savedData);
	}

	// Register dynamic add-time hotkeys
	for (auto &entry : m_data.addTimeHotkeys) {
		if (entry.hotkeyId != -1)
			continue;

		if (entry.hotkeyRegistrationName.isEmpty()) {
			entry.hotkeyRegistrationName =
				QString("TMinus_AddTime_%1_%2").arg(m_data.timerId).arg(GenerateUniqueID());
		}

		std::string name = entry.hotkeyRegistrationName.toStdString();
		std::string desc = QString("T-Minus: %1 (%2)").arg(entry.label).arg(timerLabel).toStdString();
		int deltaMs = entry.deltaSeconds * 1000;

		LoadHotkey(
			entry.hotkeyId, name.c_str(), desc.c_str(), this,
			[this, deltaMs]() { m_engine->addTime(deltaMs); }, "Add Time " + entry.label.toStdString(),
			savedData);
	}
}

void TimerWidget::ReregisterAllHotkeys()
{
	// Preserve current key bindings across the unregister/register cycle
	// (used when the timer is renamed so hotkey descriptions stay fresh).
	obs_data_t *bindings = obs_data_create();

	SaveHotkey(bindings, m_data.startPauseHotkeyId, ("TMinus_StartPause_" + m_data.timerId.toStdString()).c_str());
	SaveHotkey(bindings, m_data.restartHotkeyId, ("TMinus_Restart_" + m_data.timerId.toStdString()).c_str());
	for (const auto &entry : m_data.addTimeHotkeys) {
		if (!entry.hotkeyRegistrationName.isEmpty())
			SaveHotkey(bindings, entry.hotkeyId, entry.hotkeyRegistrationName.toUtf8().constData());
	}

	UnregisterHotkeys();
	RegisterHotkeys(bindings);

	obs_data_release(bindings);
}

void TimerWidget::UnregisterHotkeys()
{
	CleanupHotkey(m_data.startPauseHotkeyId);
	m_data.startPauseHotkeyId = -1;
	CleanupHotkey(m_data.restartHotkeyId);
	m_data.restartHotkeyId = -1;

	for (auto &entry : m_data.addTimeHotkeys) {
		CleanupHotkey(entry.hotkeyId);
		entry.hotkeyId = -1;
	}
}

void TimerWidget::StopAll()
{
	m_engine->stop();
	m_actionExecutor->cancel();
}

void TimerWidget::SaveData(obs_data_t *obj)
{
	obs_data_set_string(obj, "timerId", m_data.timerId.toUtf8().constData());
	obs_data_set_string(obj, "displayName", m_data.displayName.toUtf8().constData());
	obs_data_set_int(obj, "timerType", static_cast<int>(m_data.timerType));

	// Countdown duration
	obs_data_set_int(obj, "countdownDays", m_data.countdownDuration.days);
	obs_data_set_int(obj, "countdownHours", m_data.countdownDuration.hours);
	obs_data_set_int(obj, "countdownMinutes", m_data.countdownDuration.minutes);
	obs_data_set_int(obj, "countdownSeconds", m_data.countdownDuration.seconds);

	// TimeUntil target
	obs_data_set_string(obj, "targetDateTime", m_data.targetDateTime.toString(Qt::ISODate).toUtf8().constData());

	// Text source
	obs_data_set_string(obj, "selectedTextSource", m_data.selectedTextSource.toUtf8().constData());

	// Stream behavior
	obs_data_set_bool(obj, "startOnStreamStart", m_data.startOnStreamStart);
	obs_data_set_bool(obj, "resetOnStreamStart", m_data.resetOnStreamStart);

	// Display format
	obs_data_set_bool(obj, "showDays", m_data.display.showDays);
	obs_data_set_bool(obj, "showHours", m_data.display.showHours);
	obs_data_set_bool(obj, "showMinutes", m_data.display.showMinutes);
	obs_data_set_bool(obj, "showSeconds", m_data.display.showSeconds);
	obs_data_set_bool(obj, "showLeadingZero", m_data.display.showLeadingZero);
	obs_data_set_bool(obj, "useFormatString", m_data.display.useFormatString);
	obs_data_set_string(obj, "formatString", m_data.display.formatString.toUtf8().constData());

	// Hotkeys
	SaveHotkey(obj, m_data.startPauseHotkeyId, ("TMinus_StartPause_" + m_data.timerId.toStdString()).c_str());
	SaveHotkey(obj, m_data.restartHotkeyId, ("TMinus_Restart_" + m_data.timerId.toStdString()).c_str());

	// Add-time hotkeys. Key bindings are stored on the timer object keyed
	// by registration name, which is where RegisterHotkeys looks them up.
	obs_data_array_t *addTimeArray = obs_data_array_create();
	for (const auto &entry : m_data.addTimeHotkeys) {
		obs_data_t *entryObj = obs_data_create();
		obs_data_set_string(entryObj, "label", entry.label.toUtf8().constData());
		obs_data_set_int(entryObj, "deltaSeconds", entry.deltaSeconds);
		obs_data_set_string(entryObj, "hotkeyRegName", entry.hotkeyRegistrationName.toUtf8().constData());
		obs_data_array_push_back(addTimeArray, entryObj);
		obs_data_release(entryObj);

		if (!entry.hotkeyRegistrationName.isEmpty())
			SaveHotkey(obj, entry.hotkeyId, entry.hotkeyRegistrationName.toUtf8().constData());
	}
	obs_data_set_array(obj, "addTimeHotkeys", addTimeArray);
	obs_data_array_release(addTimeArray);

	// End actions
	obs_data_array_t *actionsArray = obs_data_array_create();
	for (const auto &step : m_data.endActions) {
		obs_data_t *stepObj = obs_data_create();
		obs_data_set_int(stepObj, "type", static_cast<int>(step.type));
		obs_data_set_string(stepObj, "text", step.text.toUtf8().constData());
		obs_data_set_string(stepObj, "filterSourceName", step.filterSourceName.toUtf8().constData());
		obs_data_set_string(stepObj, "filterName", step.filterName.toUtf8().constData());
		obs_data_set_bool(stepObj, "filterEnabled", step.filterEnabled);
		obs_data_set_string(stepObj, "sceneName", step.sceneName.toUtf8().constData());
		obs_data_set_int(stepObj, "delayMs", step.delayMs);
		obs_data_set_string(stepObj, "globalHotkeyName", step.globalHotkeyName.toUtf8().constData());
		obs_data_set_string(stepObj, "sourceHotkeySourceName",
				    step.sourceHotkeySourceName.toUtf8().constData());
		obs_data_set_string(stepObj, "sourceHotkeyName", step.sourceHotkeyName.toUtf8().constData());
		obs_data_array_push_back(actionsArray, stepObj);
		obs_data_release(stepObj);
	}
	obs_data_set_array(obj, "endActions", actionsArray);
	obs_data_array_release(actionsArray);
}

void TimerWidget::LoadData(obs_data_t *obj)
{
	m_data.timerId = obs_data_get_string(obj, "timerId");
	if (m_data.timerId.isEmpty())
		m_data.timerId = GenerateUniqueID();
	m_data.displayName = obs_data_get_string(obj, "displayName");
	m_data.timerType = static_cast<TimerType>(obs_data_get_int(obj, "timerType"));

	m_data.countdownDuration.days = static_cast<int>(obs_data_get_int(obj, "countdownDays"));
	m_data.countdownDuration.hours = static_cast<int>(obs_data_get_int(obj, "countdownHours"));
	m_data.countdownDuration.minutes = static_cast<int>(obs_data_get_int(obj, "countdownMinutes"));
	m_data.countdownDuration.seconds = static_cast<int>(obs_data_get_int(obj, "countdownSeconds"));

	m_data.targetDateTime = QDateTime::fromString(obs_data_get_string(obj, "targetDateTime"), Qt::ISODate);

	m_data.selectedTextSource = obs_data_get_string(obj, "selectedTextSource");

	m_data.startOnStreamStart = obs_data_get_bool(obj, "startOnStreamStart");
	m_data.resetOnStreamStart = obs_data_get_bool(obj, "resetOnStreamStart");

	m_data.display.showDays = obs_data_get_bool(obj, "showDays");
	m_data.display.showHours = obs_data_get_bool(obj, "showHours");
	m_data.display.showMinutes = obs_data_get_bool(obj, "showMinutes");
	m_data.display.showSeconds = obs_data_get_bool(obj, "showSeconds");
	m_data.display.showLeadingZero = obs_data_get_bool(obj, "showLeadingZero");
	m_data.display.useFormatString = obs_data_get_bool(obj, "useFormatString");
	m_data.display.formatString = obs_data_get_string(obj, "formatString");

	// Add-time hotkeys
	obs_data_array_t *addTimeArray = obs_data_get_array(obj, "addTimeHotkeys");
	if (addTimeArray) {
		size_t count = obs_data_array_count(addTimeArray);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *entryObj = obs_data_array_item(addTimeArray, i);
			AddTimeHotkeyEntry entry;
			entry.label = obs_data_get_string(entryObj, "label");
			entry.deltaSeconds = static_cast<int>(obs_data_get_int(entryObj, "deltaSeconds"));
			entry.hotkeyRegistrationName = obs_data_get_string(entryObj, "hotkeyRegName");
			m_data.addTimeHotkeys.append(entry);
			obs_data_release(entryObj);
		}
		obs_data_array_release(addTimeArray);
	}

	// End actions
	obs_data_array_t *actionsArray = obs_data_get_array(obj, "endActions");
	if (actionsArray) {
		size_t count = obs_data_array_count(actionsArray);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *stepObj = obs_data_array_item(actionsArray, i);
			ActionStep step;
			step.type = static_cast<ActionType>(obs_data_get_int(stepObj, "type"));
			step.text = obs_data_get_string(stepObj, "text");
			step.filterSourceName = obs_data_get_string(stepObj, "filterSourceName");
			step.filterName = obs_data_get_string(stepObj, "filterName");
			step.filterEnabled = obs_data_get_bool(stepObj, "filterEnabled");
			step.sceneName = obs_data_get_string(stepObj, "sceneName");
			step.delayMs = static_cast<int>(obs_data_get_int(stepObj, "delayMs"));
			step.globalHotkeyName = obs_data_get_string(stepObj, "globalHotkeyName");
			step.sourceHotkeySourceName = obs_data_get_string(stepObj, "sourceHotkeySourceName");
			step.sourceHotkeyName = obs_data_get_string(stepObj, "sourceHotkeyName");
			m_data.endActions.append(step);
			obs_data_release(stepObj);
		}
		obs_data_array_release(actionsArray);
	}
}
