#pragma once

#include "obs-dock-wrapper.hpp"
#include "timer-widget.hpp"

#include <obs-frontend-api.h>

#include <QMetaType>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QList>

Q_DECLARE_OPAQUE_POINTER(obs_data_t *)

class TMinusDock : public OBSDock {
	Q_OBJECT

public:
	explicit TMinusDock(QWidget *parent = nullptr);
	~TMinusDock();

public slots:
	void AddTimer(obs_data_t *savedData = nullptr);
	void RemoveTimer(const QString &id);
	void MoveTimer(Direction dir, const QString &id);
	void StartAllTimers();
	void PauseAllTimers();

private:
	void SetupUI();
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

	QVBoxLayout *m_timerListLayout;
	QScrollArea *m_scrollArea;
	QPushButton *m_addBtn;
	QPushButton *m_startAllBtn;
	QPushButton *m_pauseAllBtn;

	QList<TimerWidget *> m_timers;

	int m_startAllHotkeyId = -1;
	int m_pauseAllHotkeyId = -1;
	bool m_exitSaveDone = false;
	bool m_loading = false;
	bool m_finishedLoading = false;
	QString m_currentCollection;

	static constexpr const char *CONFIG_FILE = "config.json";
};
