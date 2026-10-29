#pragma once

#include <obs.h>

#include <QFrame>
#include <QLabel>
#include <QList>
#include <QProgressBar>
#include <QPushButton>
#include <QWidget>

#include "../core/timer-data.hpp"
#include "../core/timer-engine.hpp"

class ActionExecutor;
class DragHandle;

class TimerWidget : public QWidget {
	Q_OBJECT

public:
	explicit TimerWidget(QWidget *parent = nullptr, obs_data_t *savedData = nullptr);
	~TimerWidget();

	void SaveData(obs_data_t *dataObj);
	void LoadData(obs_data_t *dataObj);

	// Saved data for a copy of this timer: new ID, no hotkey bindings.
	void SaveDuplicateData(obs_data_t *dataObj);

	// Turn a freshly created timer into the given type with sensible
	// defaults for it.
	void InitAsType(TimerType type);

	void RegisterHotkeys(obs_data_t *savedData);
	// Current key bindings, keyed by registration name, in the form
	// RegisterHotkeys() restores them from
	void SaveHotkeyBindings(obs_data_t *bindings);
	void UnregisterHotkeys();

	// Stop the tick timer and cancel any in-progress end-action sequence.
	// Called before deleteLater() so a queued QTimer tick can't drive OBS
	// mutations against a widget that's about to be destroyed.
	void StopAll();

	void StartTimer();
	void PauseTimer();
	void ToggleStartPause();
	void RestartTimer();
	void OpenSettings();

	void SetCompact(bool compact);
	void SetMoveEnabled(bool canMoveUp, bool canMoveDown);

	// Grips for drag-to-reorder, one per layout mode
	QList<DragHandle *> dragHandles() const { return {m_dragHandle, m_compactDragHandle}; }

	TimerData &data() { return m_data; }
	const QString &timerId() const { return m_data.timerId; }
	TimerState state() const { return m_engine->state(); }

signals:
	void removeRequested(const QString &id);
	void duplicateRequested(const QString &id);
	void moveRequested(Direction dir, const QString &id);
	void settingsChanged();
	void stateChanged(TimerState newState);

protected:
	void mouseDoubleClickEvent(QMouseEvent *event) override;

private slots:
	void OnTick(long long currentMs);
	void OnFinished();
	void OnStateChanged(TimerState newState);
	void ShowMenu(const QPoint &globalPos);

private:
	void SetupUI();
	void RebuildNudgeButtons();
	void UpdateDisplay(long long timeMs);
	void UpdateSourceText(const QString &text);
	void UpdateStateVisuals();
	void UpdateHeaderLabels();
	void UpdateMeta();
	void UpdateSubLabel();
	void ConfirmDelete();
	QString DisplayLabel() const;

	// Nudge amounts shown on the card: the timer's add-time hotkey
	// entries, or a default pair when it has none.
	QList<AddTimeHotkeyEntry> NudgeEntries() const;

	TimerData m_data;
	TimerEngine *m_engine;
	ActionExecutor *m_actionExecutor;

	bool m_compact = false;
	bool m_sourceMissing = false;
	bool m_canMoveUp = false;
	bool m_canMoveDown = false;

	QFrame *m_card;
	DragHandle *m_dragHandle;
	DragHandle *m_compactDragHandle;

	// Comfortable layout
	QWidget *m_full;
	QLabel *m_statusPill;
	QLabel *m_nameLabel;
	QLabel *m_typeLabel;
	QPushButton *m_menuBtn;
	QLabel *m_timeDisplay;
	QLabel *m_subLabel;
	QProgressBar *m_progress;
	QLabel *m_sourceLabel;
	QLabel *m_infoLabel;
	QPushButton *m_primaryBtn;
	QWidget *m_nudgeBox;
	QList<QPushButton *> m_nudgeButtons;
	QPushButton *m_restartBtn;

	// Compact layout
	QWidget *m_compactRow;
	QLabel *m_compactDot;
	QLabel *m_compactName;
	QLabel *m_compactTime;
	QPushButton *m_compactBtn;
	QProgressBar *m_compactProgress;
};
