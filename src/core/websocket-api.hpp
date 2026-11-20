#pragma once

#include <obs.h>

// Registers the "t-minus" obs-websocket vendor and its requests. Must run
// from obs_module_post_load, once obs-websocket has loaded.
void RegisterWebsocketVendor();

// Emits a vendor event; does nothing without obs-websocket
void EmitWebsocketEvent(const char *type, obs_data_t *data);

// Stops talking to obs-websocket, which may unload before T-Minus at exit
void ShutdownWebsocketVendor();
