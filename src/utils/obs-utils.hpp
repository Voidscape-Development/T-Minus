#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

#include <obs.h>
#include <obs.hpp>
#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QPointer>
#include <QString>

#include "../plugin-support.h"

struct RegisterHotkeyCallbackData {
	std::function<void()> function;
	std::string hotkeyLogMessage;
	// Owner widget the callback is queued onto; if it has been destroyed
	// by the time the event is delivered, Qt drops the call.
	QPointer<QObject> context;
};

// Protects both g_hotkeyCallbackDataMap and every RegisterHotkeyCallbackData
// it stores. libobs dispatches hotkey callbacks on its own thread, so a
// scene-collection switch that tears down callback data on the UI thread
// would otherwise race with an in-flight callback reading that data.
extern std::mutex g_hotkeyMutex;
extern std::unordered_map<int, RegisterHotkeyCallbackData *> g_hotkeyCallbackDataMap;

void LoadHotkey(int &id, const char *name, const char *description, QObject *context, std::function<void()> function,
		std::string buttonLogMessage, obs_data_t *savedData = nullptr);
void SaveHotkey(obs_data_t *sv_data, obs_hotkey_id id, const char *name);
void HotkeyCallback(void *incoming_data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
void CleanupHotkey(int id);
QString GenerateUniqueID();
