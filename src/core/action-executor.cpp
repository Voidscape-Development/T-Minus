#include "action-executor.hpp"
#include "../plugin-support.h"
#include "../utils/format-utils.hpp"

#include <obs.h>
#include <obs-frontend-api.h>

#include <cstring>

std::function<void(const QString &, TimerControlMode, int)> ActionExecutor::timerControl;

namespace {

// Holds a source looked up by name for the scope of one step
struct SourceRef {
	obs_source_t *source;
	explicit SourceRef(const QString &name) : source(obs_get_source_by_name(name.toUtf8().constData()))
	{
		if (!source && !name.isEmpty())
			obs_log(LOG_WARNING, "T-Minus: Source '%s' not found", name.toUtf8().constData());
	}
	~SourceRef() { obs_source_release(source); }
	operator obs_source_t *() const { return source; }
};

// Presses and releases the hotkey with this name; with a source, only that
// source's hotkey (source hotkeys are registered with the source's weak
// reference as the registerer, so that is what the comparison uses)
void PressHotkey(const QString &name, obs_source_t *source)
{
	QByteArray nameBytes = name.toUtf8();
	struct Search {
		const char *name;
		obs_weak_source_t *weak;
		obs_hotkey_id id;
	} search = {nameBytes.constData(), source ? obs_source_get_weak_source(source) : nullptr,
		    OBS_INVALID_HOTKEY_ID};

	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *hotkey) -> bool {
			auto *s = static_cast<Search *>(data);
			const char *n = obs_hotkey_get_name(hotkey);
			if (!n || strcmp(n, s->name) != 0)
				return true;
			if (s->weak && (obs_hotkey_get_registerer_type(hotkey) != OBS_HOTKEY_REGISTERER_SOURCE ||
					obs_hotkey_get_registerer(hotkey) != s->weak))
				return true;
			s->id = id;
			return false;
		},
		&search);
	obs_weak_source_release(search.weak);

	if (search.id == OBS_INVALID_HOTKEY_ID) {
		obs_log(LOG_WARNING, "T-Minus: Hotkey '%s' not found", nameBytes.constData());
		return;
	}
	obs_hotkey_trigger_routed_callback(search.id, true);
	obs_hotkey_trigger_routed_callback(search.id, false);
}

void RunOutputControl(OutputMode mode)
{
	switch (mode) {
	case OutputMode::StartStreaming:
		if (!obs_frontend_streaming_active())
			obs_frontend_streaming_start();
		break;
	case OutputMode::StopStreaming:
		if (obs_frontend_streaming_active())
			obs_frontend_streaming_stop();
		break;
	case OutputMode::StartRecording:
		if (!obs_frontend_recording_active())
			obs_frontend_recording_start();
		break;
	case OutputMode::StopRecording:
		if (obs_frontend_recording_active())
			obs_frontend_recording_stop();
		break;
	case OutputMode::PauseRecording:
	case OutputMode::ResumeRecording:
		if (obs_frontend_recording_active())
			obs_frontend_recording_pause(mode == OutputMode::PauseRecording);
		break;
	case OutputMode::StartReplayBuffer:
		if (!obs_frontend_replay_buffer_active())
			obs_frontend_replay_buffer_start();
		break;
	case OutputMode::StopReplayBuffer:
		if (obs_frontend_replay_buffer_active())
			obs_frontend_replay_buffer_stop();
		break;
	case OutputMode::SaveReplayBuffer:
		if (obs_frontend_replay_buffer_active())
			obs_frontend_replay_buffer_save();
		break;
	}
}

} // namespace

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

void ActionExecutor::execute(const QList<ActionStep> &actions, const QString &textSourceName,
			     const QHash<QString, QString> &values)
{
	cancel();
	m_actions = actions;
	m_textSourceName = textSourceName;
	m_values = values;
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
	// A step may cancel this sequence (a Timer Control step resetting its
	// own timer), which empties m_actions and ends the loop; hence the copy.
	while (m_currentStep < m_actions.size()) {
		const ActionStep step = m_actions[m_currentStep++];
		if (step.type == ActionType::Delay) {
			m_delayTimer->start(step.delayMs);
			return;
		}
		executeStep(step);
	}
}

void ActionExecutor::executeStep(const ActionStep &step)
{
	switch (step.type) {
	case ActionType::SetText: {
		SourceRef source(m_textSourceName);
		if (!source)
			return;
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "text", ExpandPlaceholders(step.text, m_values).toUtf8().constData());
		obs_source_update(source, settings);
		obs_data_release(settings);
		break;
	}
	case ActionType::EnableFilter: {
		SourceRef source(step.filterSourceName);
		obs_source_t *filter =
			source ? obs_source_get_filter_by_name(source, step.filterName.toUtf8().constData()) : nullptr;
		if (filter)
			obs_source_set_enabled(filter, step.filterEnabled);
		obs_source_release(filter);
		break;
	}
	case ActionType::SwitchScene: {
		SourceRef scene(step.sceneName);
		if (scene)
			obs_frontend_set_current_scene(scene);
		break;
	}
	case ActionType::Delay:
		break;
	case ActionType::TriggerGlobalHotkey:
		PressHotkey(step.globalHotkeyName, nullptr);
		break;
	case ActionType::TriggerSourceHotkey: {
		SourceRef source(step.sourceHotkeySourceName);
		if (source)
			PressHotkey(step.sourceHotkeyName, source);
		break;
	}
	case ActionType::SourceVisibility: {
		SourceRef sceneSource(step.sceneName);
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		obs_sceneitem_t *item =
			scene ? obs_scene_find_source_recursive(scene, step.targetName.toUtf8().constData()) : nullptr;
		if (item)
			obs_sceneitem_set_visible(item, step.filterEnabled);
		break;
	}
	case ActionType::MediaControl: {
		SourceRef source(step.targetName);
		if (!source)
			return;
		switch (static_cast<MediaMode>(step.mode)) {
		case MediaMode::Play:
		case MediaMode::Pause:
			obs_source_media_play_pause(source, step.mode == static_cast<int>(MediaMode::Pause));
			break;
		case MediaMode::Restart:
			obs_source_media_restart(source);
			break;
		case MediaMode::Stop:
			obs_source_media_stop(source);
			break;
		}
		break;
	}
	case ActionType::AudioControl: {
		SourceRef source(step.targetName);
		if (!source)
			return;
		switch (static_cast<AudioMode>(step.mode)) {
		case AudioMode::Mute:
		case AudioMode::Unmute:
			obs_source_set_muted(source, step.mode == static_cast<int>(AudioMode::Mute));
			break;
		case AudioMode::ToggleMute:
			obs_source_set_muted(source, !obs_source_muted(source));
			break;
		case AudioMode::SetVolume:
			obs_source_set_volume(source, obs_db_to_mul(static_cast<float>(step.value)));
			break;
		}
		break;
	}
	case ActionType::OutputControl:
		RunOutputControl(static_cast<OutputMode>(step.mode));
		break;
	case ActionType::TimerControl:
		if (timerControl)
			timerControl(step.targetName, static_cast<TimerControlMode>(step.mode),
				     static_cast<int>(step.value));
		break;
	}
}

obs_data_array_t *SaveSteps(const QList<ActionStep> &steps)
{
	obs_data_array_t *array = obs_data_array_create();
	for (const auto &step : steps) {
		obs_data_t *obj = obs_data_create();
		obs_data_set_int(obj, "type", static_cast<int>(step.type));
		obs_data_set_string(obj, "text", step.text.toUtf8().constData());
		obs_data_set_string(obj, "filterSourceName", step.filterSourceName.toUtf8().constData());
		obs_data_set_string(obj, "filterName", step.filterName.toUtf8().constData());
		obs_data_set_bool(obj, "filterEnabled", step.filterEnabled);
		obs_data_set_string(obj, "sceneName", step.sceneName.toUtf8().constData());
		obs_data_set_int(obj, "delayMs", step.delayMs);
		obs_data_set_string(obj, "globalHotkeyName", step.globalHotkeyName.toUtf8().constData());
		obs_data_set_string(obj, "sourceHotkeySourceName", step.sourceHotkeySourceName.toUtf8().constData());
		obs_data_set_string(obj, "sourceHotkeyName", step.sourceHotkeyName.toUtf8().constData());
		obs_data_set_string(obj, "targetName", step.targetName.toUtf8().constData());
		obs_data_set_int(obj, "mode", step.mode);
		obs_data_set_double(obj, "value", step.value);
		obs_data_array_push_back(array, obj);
		obs_data_release(obj);
	}
	return array;
}

QList<ActionStep> LoadSteps(obs_data_array_t *array)
{
	QList<ActionStep> steps;
	for (size_t i = 0; i < obs_data_array_count(array); i++) {
		obs_data_t *obj = obs_data_array_item(array, i);
		ActionStep step;
		step.type = static_cast<ActionType>(obs_data_get_int(obj, "type"));
		step.text = obs_data_get_string(obj, "text");
		step.filterSourceName = obs_data_get_string(obj, "filterSourceName");
		step.filterName = obs_data_get_string(obj, "filterName");
		step.filterEnabled = obs_data_get_bool(obj, "filterEnabled");
		step.sceneName = obs_data_get_string(obj, "sceneName");
		step.delayMs = static_cast<int>(obs_data_get_int(obj, "delayMs"));
		step.globalHotkeyName = obs_data_get_string(obj, "globalHotkeyName");
		step.sourceHotkeySourceName = obs_data_get_string(obj, "sourceHotkeySourceName");
		step.sourceHotkeyName = obs_data_get_string(obj, "sourceHotkeyName");
		step.targetName = obs_data_get_string(obj, "targetName");
		step.mode = static_cast<int>(obs_data_get_int(obj, "mode"));
		step.value = obs_data_get_double(obj, "value");
		steps.append(step);
		obs_data_release(obj);
	}
	return steps;
}
