#pragma once

#include <QWidget>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "../core/timer-data.hpp"

class DragHandle;
class DragReorderController;

// One end-action step, edited in place. The card owns a copy of the step
// and writes every field change straight into it.
class ActionStepCard : public QFrame {
	Q_OBJECT

public:
	explicit ActionStepCard(const ActionStep &step, QWidget *parent = nullptr);

	const ActionStep &step() const { return m_step; }
	DragHandle *dragHandle() const { return m_dragHandle; }
	void SetPosition(int index, int count);

	static QString TypeName(ActionType type);

signals:
	void moveUpRequested(ActionStepCard *card);
	void moveDownRequested(ActionStepCard *card);
	void removeRequested(ActionStepCard *card);

private:
	void BuildFields();
	void BuildSetText();
	void BuildEnableFilter();
	void BuildSwitchScene();
	void BuildDelay();
	void BuildGlobalHotkey();
	void BuildSourceHotkey();

	// Selects name in combo. Returns false when the name had to be added
	// because it isn't available right now (e.g. a deleted scene).
	bool SelectOrKeep(QComboBox *combo, const QString &name);
	void UpdateWarning();
	QComboBox *AddCombo(const QString &accessibleName, int stretch = 1);

	void ShowMenu(const QPoint &globalPos);

	ActionStep m_step;
	DragHandle *m_dragHandle;
	QLabel *m_numberLabel;
	int m_index = 0;
	int m_count = 0;
	QHBoxLayout *m_fieldsRow;
	QLabel *m_warningLabel;
	QStringList m_missing;
};

class ActionListWidget : public QWidget {
	Q_OBJECT

public:
	explicit ActionListWidget(QWidget *parent = nullptr);

	void setActions(const QList<ActionStep> &actions);
	QList<ActionStep> actions() const;

signals:
	// Emitted when steps are added, removed or reordered
	void stepsChanged();

private:
	void AddCard(const ActionStep &step);
	void MoveCard(ActionStepCard *card, int delta);
	void RemoveCard(ActionStepCard *card);
	void Relayout();

	QScrollArea *m_scroll;
	QVBoxLayout *m_cardsLayout;
	QLabel *m_emptyLabel;
	QPushButton *m_addBtn;
	QList<ActionStepCard *> m_cards;
	DragReorderController *m_reorder;
};
