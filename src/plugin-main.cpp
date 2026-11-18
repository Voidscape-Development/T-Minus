#include <obs.h>
#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QMainWindow>

#include "plugin-support.h"
#include "widgets/tminus-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")
OBS_MODULE_AUTHOR("Voidscape Development")

static TMinusDock *tminusDock = nullptr;

bool obs_module_load(void)
{
	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());

	obs_frontend_push_ui_translation(obs_module_get_string);
	tminusDock = new TMinusDock(main_window);
	obs_frontend_pop_ui_translation();

	obs_frontend_add_dock_by_id("TMinusDock", obs_module_text("T-Minus"), tminusDock);

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}

const char *obs_module_name(void)
{
	return obs_module_text("T-Minus");
}

const char *obs_module_description(void)
{
	return obs_module_text("TMinusDescription");
}
