#include "websocket-api.hpp"
#include "../widgets/tminus-dock.hpp"
#include "../plugin-support.h"

#include <callback/calldata.h>
#include <callback/proc.h>

#include <QMetaObject>
#include <QThread>

#include <functional>

// The obs-websocket vendor API is a set of procedures on obs-websocket's own
// proc handler; this calls them directly rather than vendoring its header.

namespace {

proc_handler_t *g_ph = nullptr;
void *g_vendor = nullptr;

using RequestFn = std::function<void(obs_data_t *request, obs_data_t *response)>;

struct Request {
	const char *type;
	RequestFn fn;
};

void Fail(obs_data_t *response, const char *error)
{
	obs_data_set_bool(response, "success", false);
	obs_data_set_string(response, "error", error);
}

// Every timer request names its timer by ID or name in "timer". Without one,
// requests that allow it apply to all timers.
RequestFn ForTimers(bool allowAll, std::function<void(TimerWidget *, obs_data_t *request)> fn)
{
	return [allowAll, fn](obs_data_t *request, obs_data_t *response) {
		TMinusDock *dock = TMinusDock::instance();
		const QString name = QString::fromUtf8(obs_data_get_string(request, "timer"));
		if (name.isEmpty() && allowAll) {
			for (TimerWidget *timer : dock->timers())
				fn(timer, request);
		} else if (TimerWidget *timer = dock->FindTimer(name)) {
			fn(timer, request);
		} else {
			return Fail(response, "Timer not found");
		}
		obs_data_set_bool(response, "success", true);
	};
}

const Request kRequests[] = {
	{"GetTimers",
	 [](obs_data_t *, obs_data_t *response) {
		 obs_data_array_t *array = obs_data_array_create();
		 for (TimerWidget *timer : TMinusDock::instance()->timers()) {
			 obs_data_t *obj = timer->Describe();
			 obs_data_array_push_back(array, obj);
			 obs_data_release(obj);
		 }
		 obs_data_set_array(response, "timers", array);
		 obs_data_array_release(array);
		 obs_data_set_bool(response, "success", true);
	 }},
	{"StartTimer", ForTimers(true, [](TimerWidget *t, obs_data_t *) { t->StartTimer(); })},
	{"PauseTimer", ForTimers(true, [](TimerWidget *t, obs_data_t *) { t->PauseTimer(); })},
	{"ResetTimer", ForTimers(true, [](TimerWidget *t, obs_data_t *) { t->RestartTimer(); })},
	{"ToggleTimer", ForTimers(false, [](TimerWidget *t, obs_data_t *) { t->ToggleStartPause(); })},
	{"AddTime", ForTimers(false,
			      [](TimerWidget *t, obs_data_t *req) {
				      t->AddTime(static_cast<long long>(obs_data_get_double(req, "seconds") * 1000));
			      })},
	{"SetTime", ForTimers(false,
			      [](TimerWidget *t, obs_data_t *req) {
				      t->SetTime(static_cast<long long>(obs_data_get_double(req, "seconds") * 1000));
			      })},
};

void OnRequest(obs_data_t *request, obs_data_t *response, void *priv)
{
	const auto *r = static_cast<const Request *>(priv);
	TMinusDock *dock = TMinusDock::instance();
	if (!dock)
		return Fail(response, "T-Minus is not loaded");

	// Requests arrive on obs-websocket's thread; timers live on the UI thread
	auto run = [&]() {
		r->fn(request, response);
	};
	if (QThread::currentThread() == dock->thread())
		run();
	else
		QMetaObject::invokeMethod(dock, run, Qt::BlockingQueuedConnection);
}

bool CallVendorProc(const char *proc, calldata_t *cd)
{
	calldata_set_ptr(cd, "vendor", g_vendor);
	proc_handler_call(g_ph, proc, cd);
	return calldata_bool(cd, "success");
}

} // namespace

void RegisterWebsocketVendor()
{
	calldata_t cd = {};
	if (proc_handler_call(obs_get_proc_handler(), "obs_websocket_api_get_ph", &cd))
		g_ph = static_cast<proc_handler_t *>(calldata_ptr(&cd, "ph"));
	calldata_free(&cd);
	if (!g_ph) {
		obs_log(LOG_INFO, "obs-websocket not found; remote control is unavailable");
		return;
	}

	cd = {};
	calldata_set_string(&cd, "name", "t-minus");
	proc_handler_call(g_ph, "vendor_register", &cd);
	g_vendor = calldata_ptr(&cd, "vendor");
	calldata_free(&cd);
	if (!g_vendor) {
		obs_log(LOG_WARNING, "Failed to register the obs-websocket vendor");
		return;
	}

	for (const Request &r : kRequests) {
		// Layout of obs-websocket's struct obs_websocket_request_callback
		struct {
			void (*callback)(obs_data_t *, obs_data_t *, void *);
			void *priv_data;
		} cb = {OnRequest, const_cast<Request *>(&r)};
		cd = {};
		calldata_set_string(&cd, "type", r.type);
		calldata_set_ptr(&cd, "callback", &cb);
		if (!CallVendorProc("vendor_request_register", &cd))
			obs_log(LOG_WARNING, "Failed to register obs-websocket request %s", r.type);
		calldata_free(&cd);
	}
	obs_log(LOG_INFO, "obs-websocket vendor \"t-minus\" registered");
}

void EmitWebsocketEvent(const char *type, obs_data_t *data)
{
	if (!g_vendor)
		return;
	calldata_t cd = {};
	calldata_set_string(&cd, "type", type);
	calldata_set_ptr(&cd, "data", data);
	CallVendorProc("vendor_event_emit", &cd);
	calldata_free(&cd);
}

void ShutdownWebsocketVendor()
{
	g_vendor = nullptr;
}
