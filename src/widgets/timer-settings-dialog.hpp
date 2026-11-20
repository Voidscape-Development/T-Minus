#pragma once

#include <QDialog>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimeEdit>

#include "../core/timer-data.hpp"
#include "action-list-widget.hpp"

class ActionExecutor;
class DateTimePicker;

class TimerSettingsDialog : public QDialog {
	Q_OBJECT

public:
	explicit TimerSettingsDialog(const TimerData &data, QWidget *parent = nullptr);

	// Copies the dialog's values into data. Add-time entries keep their
	// registration names (and so their key bindings); registering and
	// unregistering hotkeys is left to the caller.
	void ApplyToData(TimerData &data) const;

private:
	void SetupUI();
	QWidget *CreatePreview();
	QWidget *CreateTimerPage();
	QWidget *CreateOutputPage();
	QWidget *CreateFinishPage();
	QWidget *CreateHotkeysPage();

	void LoadFromData(const TimerData &data);
	void PopulateTextSourceDropdown(const QString &selected);
	void AddAddTimeRow(const AddTimeHotkeyEntry &entry);

	TimerType SelectedType() const;
	DurationValue SelectedDuration() const;
	DisplayFormat SelectedFormat() const;
	QDateTime SelectedTargetDateTime() const;
	void SetTargetDateTime(const QDateTime &dt);
	void SetDuration(int hours, int minutes, int seconds);
	QHash<QString, QString> SampleValues(long long ms) const;

	// Actions page: trigger 0 is "when the timer ends", trigger i > 0 is
	// m_timedGroups[i - 1]. The list widget holds the selected trigger's
	// steps; the others are kept in m_endSteps and m_timedGroups.
	QList<ActionStep> StepsFor(int trigger) const;
	QString TriggerLabel(int trigger) const;
	void SelectTrigger(int trigger);
	void RefreshTriggerLabels();

private slots:
	void UpdateTypeVisibility();
	void UpdateUnitsVisibility();
	void UpdatePreview();
	void UpdateNavCounts();

private:
	QListWidget *m_nav;
	QStackedWidget *m_pages;

	// Preview
	QLabel *m_previewLabel;
	QLabel *m_previewSourceLabel;

	// Timer page
	QLabel *m_inUseNote;
	QLineEdit *m_nameEdit;
	QButtonGroup *m_typeGroup;
	QWidget *m_countdownBox;
	QSpinBox *m_daysSpin;
	QSpinBox *m_hoursSpin;
	QSpinBox *m_minutesSpin;
	QSpinBox *m_secondsSpin;
	QWidget *m_timeUntilBox;
	DateTimePicker *m_targetPicker;
	QComboBox *m_streamStartCombo;
	QWidget *m_overtimeBox;
	QCheckBox *m_overtimeCheck;
	QLineEdit *m_overtimePrefixEdit;

	// Output page
	QComboBox *m_textSourceCombo;
	QLineEdit *m_templateEdit;
	QComboBox *m_unitsCombo;
	QWidget *m_customUnitsBox;
	QCheckBox *m_showDaysCheck;
	QCheckBox *m_showHoursCheck;
	QCheckBox *m_showMinutesCheck;
	QCheckBox *m_showSecondsCheck;
	QCheckBox *m_leadingZeroCheck;
	QComboBox *m_fractionCombo;
	QLineEdit *m_finishedTextEdit;

	// Actions page
	QComboBox *m_triggerCombo;
	QWidget *m_triggerTimeBox;
	QTimeEdit *m_triggerTime;
	QPushButton *m_removeTriggerBtn;
	int m_currentTrigger = 0;
	QList<ActionStep> m_endSteps;
	QList<TimedActionGroup> m_timedGroups;
	ActionListWidget *m_actionList;
	ActionExecutor *m_testExecutor;

	// Hotkeys page. Rows line up with m_addTimeEntries, which carries
	// each entry's registration name.
	QTableWidget *m_addTimeTable;
	QList<AddTimeHotkeyEntry> m_addTimeEntries;

	QString m_timerLabel;
};
