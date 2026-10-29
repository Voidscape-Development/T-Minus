#include "tminus-dock.hpp"
#include "drag-reorder.hpp"
#include "../utils/obs-utils.hpp"
#include "../plugin-support.h"

#include <obs.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>

#include <QHBoxLayout>
#include <QMenu>
#include <QWidget>

TMinusDock::TMinusDock(QWidget *parent) : OBSDock(parent)
{
	qRegisterMetaType<obs_data_t *>("obs_data_t*");

	SetupUI();

	obs_frontend_add_event_callback(OBSFrontendEventHandler, this);
}

TMinusDock::~TMinusDock()
{
	obs_frontend_remove_event_callback(OBSFrontendEventHandler, this);
	if (!m_exitSaveDone)
		SaveSettings();
	UnregisterGlobalHotkeys();
}

void TMinusDock::SetupUI()
{
	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);

	m_stack = new QStackedWidget(this);
	mainLayout->addWidget(m_stack, 1);

	m_emptyState = CreateEmptyState();
	m_stack->addWidget(m_emptyState);

	// Scrollable timer list. No frame and no background fill so the dock
	// takes on the active OBS theme's window background, like native docks.
	m_scrollArea = new QScrollArea(this);
	m_scrollArea->setWidgetResizable(true);
	m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_scrollArea->setFrameShape(QFrame::NoFrame);
	m_scrollArea->viewport()->setAutoFillBackground(false);

	auto *scrollContent = new QWidget(m_scrollArea);
	scrollContent->setAutoFillBackground(false);
	m_timerListLayout = new QVBoxLayout(scrollContent);
	m_timerListLayout->setContentsMargins(4, 4, 4, 4);
	m_timerListLayout->setSpacing(4);
	m_timerListLayout->addStretch();

	m_scrollArea->setWidget(scrollContent);
	m_stack->addWidget(m_scrollArea);

	// Timers are reordered by dragging their grip; the order is saved once
	// the drag ends
	m_reorder = new DragReorderController(
		m_scrollArea,
		[this]() {
			QList<QWidget *> items;
			for (auto *timer : m_timers)
				items.append(timer);
			return items;
		},
		[this](int from, int to) {
			m_timers.move(from, to);
			RelayoutTimers();
		},
		[this]() {
			RefreshListState();
			SaveSettings();
		},
		this);

	// Bottom toolbar, mirroring the native OBS dock toolbars (Sources,
	// Scenes, Audio Mixer). The theme's "icon-*" class rules render these
	// as flat themed icon buttons; themeID covers legacy themes.
	auto *toolbar = new QHBoxLayout();
	toolbar->setContentsMargins(4, 2, 4, 2);
	toolbar->setSpacing(2);

	m_addBtn = new QPushButton(this);
	m_addBtn->setFixedSize(24, 24);
	m_addBtn->setFlat(true);
	m_addBtn->setProperty("themeID", "addIconSmall");
	m_addBtn->setProperty("class", "icon-plus");
	m_addBtn->setToolTip("Add Timer");
	m_addBtn->setAccessibleName("Add Timer");
	toolbar->addWidget(m_addBtn);

	toolbar->addStretch();

	m_runningLabel = new QLabel(this);
	m_runningLabel->setStyleSheet("color: rgba(127, 127, 127, 100%);");
	toolbar->addWidget(m_runningLabel);
	toolbar->addSpacing(4);

	m_startAllBtn = new QPushButton(this);
	m_startAllBtn->setFixedSize(24, 24);
	m_startAllBtn->setFlat(true);
	m_startAllBtn->setProperty("themeID", "playIcon");
	m_startAllBtn->setProperty("class", "icon-media-play");
	m_startAllBtn->setToolTip("Start All Timers");
	m_startAllBtn->setAccessibleName("Start All Timers");
	toolbar->addWidget(m_startAllBtn);

	m_pauseAllBtn = new QPushButton(this);
	m_pauseAllBtn->setFixedSize(24, 24);
	m_pauseAllBtn->setFlat(true);
	m_pauseAllBtn->setProperty("themeID", "pauseIcon");
	m_pauseAllBtn->setProperty("class", "icon-media-pause");
	m_pauseAllBtn->setToolTip("Pause All Timers");
	m_pauseAllBtn->setAccessibleName("Pause All Timers");
	toolbar->addWidget(m_pauseAllBtn);

	m_compactBtn = new QToolButton(this);
	m_compactBtn->setFixedSize(24, 24);
	m_compactBtn->setAutoRaise(true);
	m_compactBtn->setCheckable(true);
	m_compactBtn->setText(QString::fromUtf8("≡"));
	m_compactBtn->setToolTip("Compact View");
	m_compactBtn->setAccessibleName("Compact View");
	toolbar->addWidget(m_compactBtn);

	mainLayout->addLayout(toolbar);

	// Connections
	connect(m_addBtn, &QPushButton::clicked, this, &TMinusDock::ShowAddMenu);
	connect(m_startAllBtn, &QPushButton::clicked, this, &TMinusDock::StartAllTimers);
	connect(m_pauseAllBtn, &QPushButton::clicked, this, &TMinusDock::PauseAllTimers);
	connect(m_compactBtn, &QToolButton::toggled, this, [this](bool checked) {
		SetCompact(checked);
		SaveSettings();
	});

	RefreshListState();
}

QWidget *TMinusDock::CreateEmptyState()
{
	auto *page = new QWidget(this);
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(12, 24, 12, 12);
	layout->setSpacing(8);

	m_emptyTitle = new QLabel(page);
	m_emptyTitle->setWordWrap(true);
	m_emptyTitle->setStyleSheet("font-weight: bold;");
	layout->addWidget(m_emptyTitle);

	auto *body = new QLabel("Timers belong to the current scene collection. Pick a type to create one; "
				"you can name it and link a text source next.",
				page);
	body->setWordWrap(true);
	body->setStyleSheet("color: rgba(127, 127, 127, 100%);");
	layout->addWidget(body);
	layout->addSpacing(4);

	auto addTile = [this, page, layout](TimerType type, const QString &title, const QString &desc) {
		auto *tile = new QPushButton(QString("%1\n%2").arg(title, desc), page);
		tile->setStyleSheet("text-align: left; padding: 8px 10px;");
		tile->setMinimumHeight(52);
		tile->setAccessibleName(title);
		connect(tile, &QPushButton::clicked, this, [this, type]() { AddTimerOfType(type); });
		layout->addWidget(tile);
	};
	addTile(TimerType::Countdown, "Countdown", "Counts down a fixed length. Starting soon, BRB.");
	addTile(TimerType::Stopwatch, "Stopwatch", "Counts up from zero. Stream uptime, segments.");
	addTile(TimerType::TimeUntil, "Time Until", "Counts to a date and time. Event starts.");

	layout->addStretch();
	return page;
}

void TMinusDock::ShowAddMenu()
{
	QMenu menu(this);
	menu.addAction("Countdown", this, [this]() { AddTimerOfType(TimerType::Countdown); });
	menu.addAction("Stopwatch", this, [this]() { AddTimerOfType(TimerType::Stopwatch); });
	menu.addAction("Time Until", this, [this]() { AddTimerOfType(TimerType::TimeUntil); });

	// The toolbar sits at the bottom of the dock, so open upwards
	const QPoint pos = m_addBtn->mapToGlobal(QPoint(0, 0)) - QPoint(0, menu.sizeHint().height());
	menu.exec(pos);
}

TimerWidget *TMinusDock::AddTimer(obs_data_t *savedData, int index)
{
	auto *timer = new TimerWidget(this, savedData);
	timer->SetCompact(m_compact);
	for (DragHandle *handle : timer->dragHandles())
		m_reorder->Attach(handle, timer);

	connect(timer, &TimerWidget::removeRequested, this, &TMinusDock::RemoveTimer);
	connect(timer, &TimerWidget::duplicateRequested, this, &TMinusDock::DuplicateTimer);
	connect(timer, &TimerWidget::moveRequested, this, &TMinusDock::MoveTimer);
	connect(timer, &TimerWidget::settingsChanged, this, [this]() { SaveSettings(); });
	connect(timer, &TimerWidget::stateChanged, this, &TMinusDock::UpdateRunningCount);

	if (index < 0 || index > m_timers.size())
		index = m_timers.size();

	// The list layout holds the timers in order followed by a stretch
	m_timerListLayout->insertWidget(index, timer);
	m_timers.insert(index, timer);

	obs_log(LOG_INFO, "Timer added: %s", timer->timerId().toUtf8().constData());

	RefreshListState();
	SaveSettings();
	return timer;
}

void TMinusDock::AddTimerOfType(TimerType type)
{
	TimerWidget *timer = AddTimer();
	timer->InitAsType(type);
	SaveSettings();

	m_scrollArea->ensureWidgetVisible(timer);
	timer->OpenSettings();
}

void TMinusDock::DuplicateTimer(const QString &id)
{
	for (int i = 0; i < m_timers.size(); i++) {
		if (m_timers[i]->timerId() != id)
			continue;

		obs_data_t *copy = obs_data_create();
		m_timers[i]->SaveDuplicateData(copy);
		AddTimer(copy, i + 1);
		obs_data_release(copy);
		return;
	}
}

void TMinusDock::RemoveTimer(const QString &id)
{
	for (int i = 0; i < m_timers.size(); i++) {
		if (m_timers[i]->timerId() == id) {
			TimerWidget *timer = m_timers.takeAt(i);
			m_timerListLayout->removeWidget(timer);
			timer->deleteLater();
			obs_log(LOG_INFO, "Timer removed: %s", id.toUtf8().constData());
			RefreshListState();
			SaveSettings();
			return;
		}
	}
}

void TMinusDock::MoveTimer(Direction dir, const QString &id)
{
	for (int i = 0; i < m_timers.size(); i++) {
		if (m_timers[i]->timerId() != id)
			continue;

		int newIdx = (dir == Direction::Up) ? i - 1 : i + 1;
		if (newIdx < 0 || newIdx >= m_timers.size())
			return;

		m_timers.swapItemsAt(i, newIdx);
		RelayoutTimers();
		RefreshListState();
		SaveSettings();
		return;
	}
}

void TMinusDock::RelayoutTimers()
{
	for (auto *timer : m_timers)
		m_timerListLayout->removeWidget(timer);
	for (int i = 0; i < m_timers.size(); i++)
		m_timerListLayout->insertWidget(i, m_timers[i]);
}

void TMinusDock::RefreshListState()
{
	for (int i = 0; i < m_timers.size(); i++)
		m_timers[i]->SetMoveEnabled(i > 0, i < m_timers.size() - 1);

	QString collection = m_currentCollection;
	if (collection.isEmpty() || collection == QStringLiteral("__default__"))
		m_emptyTitle->setText("No timers in this scene collection yet");
	else
		m_emptyTitle->setText(QString::fromUtf8("No timers in “%1” yet").arg(collection));

	m_stack->setCurrentWidget(m_timers.isEmpty() ? m_emptyState : static_cast<QWidget *>(m_scrollArea));
	m_startAllBtn->setEnabled(!m_timers.isEmpty());
	m_pauseAllBtn->setEnabled(!m_timers.isEmpty());

	UpdateRunningCount();
}

void TMinusDock::UpdateRunningCount()
{
	int running = 0;
	for (auto *timer : m_timers) {
		if (timer->state() == TimerState::Running)
			running++;
	}
	m_runningLabel->setText(running > 0 ? QString("%1 running").arg(running) : QString());
}

void TMinusDock::SetCompact(bool compact)
{
	m_compact = compact;
	for (auto *timer : m_timers)
		timer->SetCompact(compact);
	m_timerListLayout->setSpacing(compact ? 2 : 4);

	if (m_compactBtn->isChecked() != compact) {
		QSignalBlocker blocker(m_compactBtn);
		m_compactBtn->setChecked(compact);
	}
}

void TMinusDock::StartAllTimers()
{
	for (auto *timer : m_timers) {
		timer->StartTimer();
	}
}

void TMinusDock::PauseAllTimers()
{
	for (auto *timer : m_timers) {
		timer->PauseTimer();
	}
}

QString TMinusDock::CurrentCollectionName()
{
	char *name = obs_frontend_get_current_scene_collection();
	QString result = QString::fromUtf8(name ? name : "");
	bfree(name);
	return result.isEmpty() ? QStringLiteral("__default__") : result;
}

obs_data_t *TMinusDock::ReadConfigRoot() const
{
	obs_data_t *root = nullptr;

	char *configPath = obs_module_config_path(CONFIG_FILE);
	if (configPath) {
		root = obs_data_create_from_json_file_safe(configPath, "bak");
		bfree(configPath);
	}

	return root;
}

void TMinusDock::WriteConfigRoot(obs_data_t *root) const
{
	// The module config directory is not created by OBS automatically;
	// without it obs_data_save_json fails silently and nothing persists.
	char *configDir = obs_module_config_path("");
	if (configDir) {
		os_mkdirs(configDir);
		bfree(configDir);
	}

	char *configPath = obs_module_config_path(CONFIG_FILE);
	if (configPath) {
		if (!obs_data_save_json_safe(root, configPath, "tmp", "bak"))
			obs_log(LOG_WARNING, "Failed to save timer settings to %s", configPath);
		bfree(configPath);
	}
}

void TMinusDock::SaveSettings()
{
	// Before the first load the timer list is empty; saving it would wipe
	// the stored timers for this collection.
	if (m_loading || !m_finishedLoading)
		return;

	if (m_currentCollection.isEmpty())
		m_currentCollection = CurrentCollectionName();

	// Timers are stored per scene collection. Re-read the config so other
	// collections' sections are preserved, then replace only ours.
	obs_data_t *root = ReadConfigRoot();
	if (!root)
		root = obs_data_create();

	obs_data_t *section = obs_data_create();
	obs_data_array_t *timerArray = obs_data_array_create();

	for (auto *timer : m_timers) {
		obs_data_t *timerObj = obs_data_create();
		timer->SaveData(timerObj);
		obs_data_array_push_back(timerArray, timerObj);
		obs_data_release(timerObj);
	}

	obs_data_set_array(section, "timers", timerArray);
	obs_data_array_release(timerArray);

	obs_data_t *collections = obs_data_get_obj(root, "collections");
	if (!collections)
		collections = obs_data_create();
	obs_data_set_obj(collections, m_currentCollection.toUtf8().constData(), section);
	obs_data_set_obj(root, "collections", collections);
	obs_data_release(collections);
	obs_data_release(section);

	// Legacy single-collection format; drop once migrated
	obs_data_erase(root, "timers");

	obs_data_set_bool(root, "compactView", m_compact);

	// Global hotkeys apply across collections
	SaveHotkey(root, m_startAllHotkeyId, "TMinus_StartAll");
	SaveHotkey(root, m_pauseAllHotkeyId, "TMinus_StopAll");

	WriteConfigRoot(root);
	obs_data_release(root);
}

void TMinusDock::LoadTimersFromSection(obs_data_t *section)
{
	obs_data_array_t *timerArray = obs_data_get_array(section, "timers");
	if (!timerArray)
		return;

	size_t count = obs_data_array_count(timerArray);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *timerObj = obs_data_array_item(timerArray, i);

		// Configs written by builds that double-loaded on startup can
		// contain the same timer many times over; keep the first copy
		// so such configs heal on the next save.
		const QString id = QString::fromUtf8(obs_data_get_string(timerObj, "timerId"));
		bool duplicate = false;
		if (!id.isEmpty()) {
			for (auto *existing : m_timers) {
				if (existing->timerId() == id) {
					duplicate = true;
					break;
				}
			}
		}

		if (duplicate)
			obs_log(LOG_WARNING, "Skipping duplicate saved timer entry: %s", id.toUtf8().constData());
		else
			AddTimer(timerObj);

		obs_data_release(timerObj);
	}
	obs_data_array_release(timerArray);
}

void TMinusDock::LoadSavedSettings()
{
	// Idempotence guard: if timers were already loaded (e.g. by a scene
	// collection event that slipped in first), don't stack a second copy.
	ClearTimers();

	m_loading = true;

	obs_data_t *root = ReadConfigRoot();
	m_currentCollection = CurrentCollectionName();

	obs_data_t *section = nullptr;
	if (root) {
		SetCompact(obs_data_get_bool(root, "compactView"));

		obs_data_t *collections = obs_data_get_obj(root, "collections");
		if (collections) {
			section = obs_data_get_obj(collections, m_currentCollection.toUtf8().constData());
			obs_data_release(collections);
		}
	}

	if (section) {
		LoadTimersFromSection(section);
		obs_data_release(section);
	} else if (root) {
		// Legacy global format: adopt top-level timers into the
		// collection that is active on first load.
		LoadTimersFromSection(root);
	}

	// Register global hotkeys even on a fresh install (no settings file
	// yet), otherwise they never show up in OBS Settings > Hotkeys.
	RegisterGlobalHotkeys(root);

	obs_data_release(root);

	m_loading = false;
	RefreshListState();
}

void TMinusDock::ClearTimers()
{
	for (auto *timer : m_timers) {
		// Halt everything before deleteLater: with destruction deferred
		// until after the new collection's timers are created, a still-
		// active tick timer or end-action executor on the old widget
		// would otherwise fire during the transition and drive OBS
		// mutations (scene switches, hotkey triggers, source text
		// updates) on behalf of a widget that no longer represents live
		// state.
		timer->StopAll();
		// Detach outgoing signals (settingsChanged, removeRequested,
		// moveRequested) so a queued emission can't invoke SaveSettings
		// or list mutations against the fresh m_timers we're about to
		// populate.
		timer->disconnect();
		timer->UnregisterHotkeys();
		m_timerListLayout->removeWidget(timer);
		timer->deleteLater();
	}
	m_timers.clear();
	RefreshListState();
}

void TMinusDock::ReloadForCurrentCollection()
{
	ClearTimers();

	m_loading = true;

	obs_data_t *root = ReadConfigRoot();
	m_currentCollection = CurrentCollectionName();

	if (root) {
		obs_data_t *collections = obs_data_get_obj(root, "collections");
		if (collections) {
			obs_data_t *section = obs_data_get_obj(collections, m_currentCollection.toUtf8().constData());
			if (section) {
				LoadTimersFromSection(section);
				obs_data_release(section);
			}
			obs_data_release(collections);
		}
		obs_data_release(root);
	}

	m_loading = false;
	RefreshListState();
}

void TMinusDock::HandleCollectionRenamed()
{
	const QString oldName = m_currentCollection;
	m_currentCollection = CurrentCollectionName();
	if (oldName == m_currentCollection)
		return;

	// Drop the section stored under the old name, then re-save the
	// in-memory timers under the new one.
	obs_data_t *root = ReadConfigRoot();
	if (root) {
		obs_data_t *collections = obs_data_get_obj(root, "collections");
		if (collections) {
			obs_data_erase(collections, oldName.toUtf8().constData());
			obs_data_set_obj(root, "collections", collections);
			obs_data_release(collections);
			WriteConfigRoot(root);
		}
		obs_data_release(root);
	}

	SaveSettings();
	RefreshListState();
}

void TMinusDock::PruneDeletedCollections()
{
	obs_data_t *root = ReadConfigRoot();
	if (!root)
		return;

	obs_data_t *collections = obs_data_get_obj(root, "collections");
	if (!collections) {
		obs_data_release(root);
		return;
	}

	char **names = obs_frontend_get_scene_collections();

	// An empty enumeration means the frontend has no collection list to
	// offer (e.g. still starting up); pruning against it would treat every
	// stored section as stale and delete all saved timers.
	if (!names || !*names) {
		if (names)
			bfree(names);
		obs_data_release(collections);
		obs_data_release(root);
		return;
	}

	QStringList stale;
	for (obs_data_item_t *item = obs_data_first(collections); item; obs_data_item_next(&item)) {
		const char *key = obs_data_item_get_name(item);
		bool exists = false;
		for (char **name = names; *name; name++) {
			if (strcmp(*name, key) == 0) {
				exists = true;
				break;
			}
		}
		if (!exists)
			stale.append(QString::fromUtf8(key));
	}

	for (char **name = names; *name; name++)
		bfree(*name);
	bfree(names);

	if (!stale.isEmpty()) {
		for (const QString &key : stale)
			obs_data_erase(collections, key.toUtf8().constData());
		obs_data_set_obj(root, "collections", collections);
		WriteConfigRoot(root);
	}

	obs_data_release(collections);
	obs_data_release(root);
}

void TMinusDock::RegisterGlobalHotkeys(obs_data_t *savedData)
{
	LoadHotkey(
		m_startAllHotkeyId, "TMinus_StartAll", "T-Minus: Start All Timers", this,
		[this]() { StartAllTimers(); }, "Start All Timers", savedData);

	// Registered under its original "StopAll" name so existing key
	// bindings keep working; it has always paused rather than stopped.
	LoadHotkey(
		m_pauseAllHotkeyId, "TMinus_StopAll", "T-Minus: Pause All Timers", this, [this]() { PauseAllTimers(); },
		"Pause All Timers", savedData);
}

void TMinusDock::UnregisterGlobalHotkeys()
{
	CleanupHotkey(m_startAllHotkeyId);
	m_startAllHotkeyId = -1;
	CleanupHotkey(m_pauseAllHotkeyId);
	m_pauseAllHotkeyId = -1;
}

void TMinusDock::OBSFrontendEventHandler(enum obs_frontend_event event, void *data)
{
	auto *dock = static_cast<TMinusDock *>(data);

	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		dock->m_finishedLoading = true;
		dock->LoadSavedSettings();
		break;

	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		for (auto *timer : dock->m_timers) {
			if (timer->data().resetOnStreamStart) {
				timer->RestartTimer();
			}
			if (timer->data().startOnStreamStart) {
				timer->StartTimer();
			}
		}
		break;

	// OBS 31+ delivers scene collection events during startup, before
	// FINISHED_LOADING. Acting on them then would load every timer twice
	// per launch (the config doubles on each run until OBS can no longer
	// start) and prune/save against half-initialized frontend state, so
	// collection events are ignored until the first full load completed.
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
	case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
		// Persist timers and current hotkey bindings before OBS tears
		// down and reloads state for the new collection/profile.
		if (dock->m_finishedLoading)
			dock->SaveSettings();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		if (dock->m_finishedLoading)
			dock->ReloadForCurrentCollection();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_RENAMED:
		if (dock->m_finishedLoading)
			dock->HandleCollectionRenamed();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_LIST_CHANGED:
		if (dock->m_finishedLoading)
			dock->PruneDeletedCollections();
		break;

	case OBS_FRONTEND_EVENT_EXIT:
		dock->SaveSettings();
		dock->m_exitSaveDone = true;
		break;

	default:
		break;
	}
}
