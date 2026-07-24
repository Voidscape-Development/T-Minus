#include "obs-utils.hpp"

#include <QCryptographicHash>
#include <QMetaObject>
#include <QUuid>

std::mutex g_hotkeyMutex;
std::unordered_map<int, RegisterHotkeyCallbackData *> g_hotkeyCallbackDataMap;

void LoadHotkey(int &id, const char *name, const char *description, QObject *context, std::function<void()> function,
		std::string buttonLogMessage, obs_data_t *savedData)
{
	RegisterHotkeyCallbackData *callbackData = new RegisterHotkeyCallbackData{function, buttonLogMessage, context};

	id = (int)obs_hotkey_register_frontend(name, description, (obs_hotkey_func)HotkeyCallback, callbackData);

	if (id != -1) {
		{
			std::lock_guard<std::mutex> lock(g_hotkeyMutex);
			g_hotkeyCallbackDataMap[id] = callbackData;
		}

		if (savedData) {
			OBSDataArrayAutoRelease array = obs_data_get_array(savedData, name);
			obs_hotkey_load(id, array);
		}
	} else {
		delete callbackData;
	}
}

void CleanupHotkey(int id)
{
	if (id == -1)
		return;

	// obs_hotkey_unregister happens first so the hotkey subsystem stops
	// handing our callback out to new fire events. Any callback that had
	// already entered HotkeyCallback and is racing to read the data will
	// be serialized against the delete below by g_hotkeyMutex.
	obs_hotkey_unregister(id);

	RegisterHotkeyCallbackData *toDelete = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_hotkeyMutex);
		auto it = g_hotkeyCallbackDataMap.find(id);
		if (it != g_hotkeyCallbackDataMap.end()) {
			toDelete = it->second;
			g_hotkeyCallbackDataMap.erase(it);
		}
	}
	delete toDelete;
}

void SaveHotkey(obs_data_t *sv_data, obs_hotkey_id id, const char *name)
{
	if ((int)id == -1)
		return;
	OBSDataArrayAutoRelease array = obs_hotkey_save(id);
	obs_data_set_array(sv_data, name, array);
}

void HotkeyCallback(void *incoming_data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(incoming_data);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return;

	// Hotkey callbacks arrive on the libobs hotkey thread. Look up the
	// callback data through the map (rather than trusting incoming_data)
	// while holding g_hotkeyMutex, so CleanupHotkey on the UI thread
	// can't free it out from under us mid-callback. The whole invoke has
	// to happen under the lock: releasing before invokeMethod would leave
	// a window where the QObject can be destroyed between the null check
	// and Qt's event post, and posting to a dangling receiver crashes.
	std::lock_guard<std::mutex> lock(g_hotkeyMutex);
	auto it = g_hotkeyCallbackDataMap.find((int)id);
	if (it == g_hotkeyCallbackDataMap.end())
		return;

	auto *data = it->second;
	obs_log(LOG_INFO, "%s (hotkey)", data->hotkeyLogMessage.c_str());

	QObject *context = data->context;
	if (context)
		QMetaObject::invokeMethod(context, data->function, Qt::QueuedConnection);
}

QString GenerateUniqueID()
{
	QUuid uuid = QUuid::createUuid();
	QByteArray hash = QCryptographicHash::hash(uuid.toByteArray(), QCryptographicHash::Md5);
	return QString(hash.toHex().left(8));
}
