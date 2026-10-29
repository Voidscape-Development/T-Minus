#include "action-executor.hpp"
#include "../plugin-support.h"

#include <obs.h>
#include <obs-frontend-api.h>

ActionExecutor::ActionExecutor(QObject *parent) : QObject(parent)
{
	m_delayTimer = new QTimer(this);
	m_delayTimer->setSingleShot(true);
	connect(m_delayTimer, &QTimer::timeout, this, &ActionExecutor::executeNextStep);
}

ActionExecutor::~ActionExecutor()
{
	cancel();
}

void ActionExecutor::execute(const QList<ActionStep> &actions, const QString &textSourceName, const QString &timeText)
{
	cancel();
	m_actions = actions;
	m_textSourceName = textSourceName;
	m_timeText = timeText;
	m_currentStep = 0;

	if (m_actions.isEmpty()) {
		emit sequenceComplete();
		return;
	}

	executeNextStep();
}

void ActionExecutor::cancel()
{
	m_delayTimer->stop();
	m_actions.clear();
	m_currentStep = 0;
}

void ActionExecutor::executeNextStep()
{
	if (m_currentStep >= m_actions.size()) {
		emit sequenceComplete();
		return;
	}

	const ActionStep &step = m_actions[m_currentStep];
	m_currentStep++;

	switch (step.type) {
	case ActionType::SetText:
		executeSetText(step);
		executeNextStep();
		break;

	case ActionType::EnableFilter:
		executeEnableFilter(step);
		executeNextStep();
		break;

	case ActionType::SwitchScene:
		executeSwitchScene(step);
		executeNextStep();
		break;

	case ActionType::Delay:
		m_delayTimer->start(step.delayMs);
		return;

	case ActionType::TriggerGlobalHotkey:
		executeTriggerGlobalHotkey(step);
		executeNextStep();
		break;

	case ActionType::TriggerSourceHotkey:
		executeTriggerSourceHotkey(step);
		executeNextStep();
		break;
	}
}

void ActionExecutor::executeSetText(const ActionStep &step)
{
	QString sourceName = m_textSourceName;
	if (sourceName.isEmpty())
		return;

	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (!source)
		return;

	QString text = step.text;
	text.replace("%time%", m_timeText);

	obs_data_t *settings = obs_source_get_settings(source);
	obs_data_set_string(settings, "text", text.toUtf8().constData());
	obs_source_update(source, settings);
	obs_data_release(settings);
	obs_source_release(source);
}

void ActionExecutor::executeEnableFilter(const ActionStep &step)
{
	obs_source_t *source = obs_get_source_by_name(step.filterSourceName.toUtf8().constData());
	if (!source)
		return;

	obs_source_t *filter = obs_source_get_filter_by_name(source, step.filterName.toUtf8().constData());
	if (filter) {
		obs_source_set_enabled(filter, step.filterEnabled);
		obs_source_release(filter);
	}

	obs_source_release(source);
}

void ActionExecutor::executeSwitchScene(const ActionStep &step)
{
	obs_source_t *scene = obs_get_source_by_name(step.sceneName.toUtf8().constData());
	if (scene) {
		obs_frontend_set_current_scene(scene);
		obs_source_release(scene);
	}
}

struct HotkeySearchData {
	const char *targetName;
	obs_hotkey_id foundId;
	bool found;
};

static bool FindHotkeyByName(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey)
{
	auto *search = static_cast<HotkeySearchData *>(data);
	const char *name = obs_hotkey_get_name(hotkey);

	if (name && strcmp(name, search->targetName) == 0) {
		search->foundId = id;
		search->found = true;
		return false;
	}
	return true;
}

void ActionExecutor::executeTriggerGlobalHotkey(const ActionStep &step)
{
	QByteArray nameBytes = step.globalHotkeyName.toUtf8();

	HotkeySearchData searchData;
	searchData.targetName = nameBytes.constData();
	searchData.foundId = OBS_INVALID_HOTKEY_ID;
	searchData.found = false;

	obs_enum_hotkeys(FindHotkeyByName, &searchData);

	if (searchData.found) {
		obs_hotkey_trigger_routed_callback(searchData.foundId, true);
		obs_hotkey_trigger_routed_callback(searchData.foundId, false);
	} else {
		obs_log(LOG_WARNING, "T-Minus: Global hotkey '%s' not found", nameBytes.constData());
	}
}

void ActionExecutor::executeTriggerSourceHotkey(const ActionStep &step)
{
	obs_source_t *source = obs_get_source_by_name(step.sourceHotkeySourceName.toUtf8().constData());
	if (!source) {
		obs_log(LOG_WARNING, "T-Minus: Source '%s' not found for hotkey trigger",
			step.sourceHotkeySourceName.toUtf8().constData());
		return;
	}

	QByteArray nameBytes = step.sourceHotkeyName.toUtf8();

	// Find the hotkey that belongs to this source. Source hotkeys are
	// registered with the source's weak reference as the registerer, so
	// that is what the comparison has to use.
	obs_weak_source_t *weak = obs_source_get_weak_source(source);

	struct SourceHotkeySearchData {
		const char *targetName;
		obs_weak_source_t *targetWeak;
		obs_hotkey_id foundId;
		bool found;
	};

	SourceHotkeySearchData searchData;
	searchData.targetName = nameBytes.constData();
	searchData.targetWeak = weak;
	searchData.foundId = OBS_INVALID_HOTKEY_ID;
	searchData.found = false;

	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *hotkey) -> bool {
			auto *search = static_cast<SourceHotkeySearchData *>(data);
			const char *name = obs_hotkey_get_name(hotkey);

			if (name && strcmp(name, search->targetName) == 0) {
				obs_hotkey_registerer_t regType = obs_hotkey_get_registerer_type(hotkey);
				if (regType == OBS_HOTKEY_REGISTERER_SOURCE &&
				    obs_hotkey_get_registerer(hotkey) == search->targetWeak) {
					search->foundId = id;
					search->found = true;
					return false;
				}
			}
			return true;
		},
		&searchData);

	obs_weak_source_release(weak);

	if (searchData.found) {
		obs_hotkey_trigger_routed_callback(searchData.foundId, true);
		obs_hotkey_trigger_routed_callback(searchData.foundId, false);
	} else {
		obs_log(LOG_WARNING, "T-Minus: Source hotkey '%s' on '%s' not found", nameBytes.constData(),
			step.sourceHotkeySourceName.toUtf8().constData());
	}

	obs_source_release(source);
}
