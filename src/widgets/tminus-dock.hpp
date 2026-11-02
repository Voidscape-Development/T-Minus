#pragma once

#include "timer-widget.hpp"

#include <obs-frontend-api.h>

#include <QMetaType>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QAction>
#include <QToolButton>
#include <QWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QList>

Q_DECLARE_OPAQUE_POINTER(obs_data_t *)

class DragReorderController;

// OBS wraps this widget in its own dock (an OBSDock), so the theme's
// "OBSDock > QWidget" panel rule styles this widget like a native dock's
// contents. It deliberately doesn't use a class named OBSDock itself: the
// same rule would then also hit every direct child (toolbar buttons and
// labels), giving each one a panel border.
class TMinusDock : public QWidget {
	Q_OBJECT

public:
	explicit TMinusDock(QWidget *parent = nullptr);
	~TMinusDock();

public slots:
	TimerWidget *AddTimer(obs_data_t *savedData = nullptr, int index = -1);
	void AddTimerOfType(TimerType type);
	void DuplicateTimer(const QString &id);
	void RemoveTimer(const QString &id);
	void MoveTimer(Direction dir, const QString &id);
	void StartAllTimers();
	void PauseAllTimers();

private:
	void SetupUI();
	QWidget *CreateEmptyState();
	void ShowAddMenu();
	void RelayoutTimers();
	void RefreshListState();
	void UpdateRunningCount();
	void SetCompact(bool compact);
	void SaveSettings();
	void LoadSavedSettings();
	void ReloadForCurrentCollection();
	void HandleCollectionRenamed();
	void PruneDeletedCollections();
	void ClearTimers();
	void LoadTimersFromSection(obs_data_t *section);
	void RegisterGlobalHotkeys(obs_data_t *savedData);
	void UnregisterGlobalHotkeys();

	obs_data_t *ReadConfigRoot() const;
	void WriteConfigRoot(obs_data_t *root) const;
	static QString CurrentCollectionName();

	static void OBSFrontendEventHandler(enum obs_frontend_event event, void *data);

	QStackedWidget *m_stack;
	QWidget *m_emptyState;
	QLabel *m_emptyTitle;
	QVBoxLayout *m_timerListLayout;
	QScrollArea *m_scrollArea;
	QToolButton *m_addBtn;
	QLabel *m_runningLabel;
	QAction *m_runningLabelAction;
	QToolButton *m_startAllBtn;
	QToolButton *m_pauseAllBtn;
	QToolButton *m_compactBtn;

	QList<TimerWidget *> m_timers;
	DragReorderController *m_reorder;

	int m_startAllHotkeyId = -1;
	int m_pauseAllHotkeyId = -1;
	bool m_exitSaveDone = false;
	bool m_loading = false;
	bool m_finishedLoading = false;
	bool m_compact = false;
	QString m_currentCollection;

	static constexpr const char *CONFIG_FILE = "config.json";
};
