#pragma once

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QStackedWidget>
#include <QLabel>
#include <QList>

#include "../core/timer-data.hpp"

class ActionListWidget : public QWidget {
	Q_OBJECT

public:
	explicit ActionListWidget(QWidget *parent = nullptr);

	void setActions(const QList<ActionStep> &actions);
	QList<ActionStep> actions() const;

signals:
	// Emitted when steps are added, removed or reordered
	void stepsChanged();

private slots:
	void OnAddAction(ActionType type);
	void OnRemoveAction();
	void OnMoveUp();
	void OnMoveDown();
	void OnSelectionChanged();
	void OnEditFieldChanged();

private:
	void SetupUI();
	void SetupEditForms();
	void UpdateListDisplay();
	void LoadStepIntoEditor(int index);
	void SaveEditorIntoStep(int index);
	void PopulateSourceDropdowns();
	void PopulateFilterDropdown(const QString &sourceName);
	void PopulateSceneDropdown();
	void PopulateGlobalHotkeyDropdown();
	void PopulateSourceHotkeyDropdown(const QString &sourceName);

	static QString ActionStepToString(const ActionStep &step);
	static QString ActionTypeName(ActionType type);

	QList<ActionStep> m_actions;

	QListWidget *m_listWidget;
	QPushButton *m_addBtn;
	QPushButton *m_removeBtn;
	QPushButton *m_moveUpBtn;
	QPushButton *m_moveDownBtn;

	// Editor
	QLabel *m_stepLabel;
	QStackedWidget *m_editStack;

	// SetText page
	QLineEdit *m_setTextEdit;

	// EnableFilter page
	QComboBox *m_filterSourceCombo;
	QComboBox *m_filterNameCombo;
	QCheckBox *m_filterEnabledCheck;

	// SwitchScene page
	QComboBox *m_sceneCombo;

	// Delay page
	QSpinBox *m_delaySpin;

	// TriggerGlobalHotkey page
	QComboBox *m_globalHotkeyCombo;

	// TriggerSourceHotkey page
	QComboBox *m_sourceHotkeySourceCombo;
	QComboBox *m_sourceHotkeyNameCombo;

	bool m_updatingEditor = false;
};
