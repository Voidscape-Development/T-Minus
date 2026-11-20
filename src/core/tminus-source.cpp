#include "tminus-source.hpp"
#include "../widgets/tminus-dock.hpp"

#include <obs-module.h>
#include <graphics/vec4.h>

#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>

// A source that shows a timer: a private text source for the time, colored by
// how much time is left, with an optional progress bar under it.

namespace {

constexpr int kBarGap = 6;

#ifdef _WIN32
constexpr const char *kTextSourceId = "text_gdiplus";
#else
constexpr const char *kTextSourceId = "text_ft2_source";
#endif

struct TMinusSource {
	obs_source_t *self = nullptr;
	obs_source_t *text = nullptr;
	std::string timerId;
	uint32_t color = 0xFFFFFFFF, warnColor = 0xFF41A4D9, critColor = 0xFF4F60E8;
	long long warnMs = 0, critMs = 0;
	bool bar = false;
	int barHeight = 8;
	uint32_t barColor = 0xFFF07C4F, barBackground = 0x40FFFFFF;
	std::atomic<float> progress{-1.0f};
	std::string shownText;
	uint32_t shownColor = 0;
};

// Sources are created and destroyed on any thread and updated from the UI
// thread, so the registry is guarded
std::mutex g_mutex;
std::set<TMinusSource *> g_sources;
std::map<std::string, TimerSnapshot> g_snapshots;

// Called with g_mutex held
void Apply(TMinusSource *s, const TimerSnapshot &snap)
{
	uint32_t color = s->color;
	if (snap.countsDown && s->critMs > 0 && snap.timeMs <= s->critMs)
		color = s->critColor;
	else if (snap.countsDown && s->warnMs > 0 && snap.timeMs <= s->warnMs)
		color = s->warnColor;

	s->progress = static_cast<float>(snap.progress);

	const std::string text = snap.text.toStdString();
	if (text == s->shownText && color == s->shownColor)
		return;
	s->shownText = text;
	s->shownColor = color;

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "text", text.c_str());
	// GDI+ text reads "color", FreeType 2 a top and bottom color
	obs_data_set_int(settings, "color", color);
	obs_data_set_int(settings, "color1", color);
	obs_data_set_int(settings, "color2", color);
	obs_source_update(s->text, settings);
	obs_data_release(settings);
}

void DrawRect(float x, float y, float cx, float cy, uint32_t rgba)
{
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	struct vec4 color;
	vec4_from_rgba(&color, rgba);
	gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &color);

	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_sprite(nullptr, 0, static_cast<uint32_t>(cx), static_cast<uint32_t>(cy));
	gs_matrix_pop();
}

void Update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<TMinusSource *>(data);

	obs_data_t *textSettings = obs_data_create();
	obs_data_t *font = obs_data_get_obj(settings, "font");
	obs_data_set_obj(textSettings, "font", font);
	obs_data_release(font);
	const bool outline = obs_data_get_bool(settings, "outline");
	obs_data_set_bool(textSettings, "outline", outline);
	obs_data_set_int(textSettings, "outline_size", 4);
	obs_source_update(s->text, textSettings);
	obs_data_release(textSettings);

	std::lock_guard<std::mutex> lock(g_mutex);
	s->timerId = obs_data_get_string(settings, "timer");
	s->color = static_cast<uint32_t>(obs_data_get_int(settings, "color"));
	s->warnMs = obs_data_get_int(settings, "warn_seconds") * 1000;
	s->warnColor = static_cast<uint32_t>(obs_data_get_int(settings, "warn_color"));
	s->critMs = obs_data_get_int(settings, "crit_seconds") * 1000;
	s->critColor = static_cast<uint32_t>(obs_data_get_int(settings, "crit_color"));
	s->bar = obs_data_get_bool(settings, "bar");
	s->barHeight = static_cast<int>(obs_data_get_int(settings, "bar_height"));
	s->barColor = static_cast<uint32_t>(obs_data_get_int(settings, "bar_color"));
	s->barBackground = static_cast<uint32_t>(obs_data_get_int(settings, "bar_background"));

	// Re-apply whatever the timer shows now, with the new colors
	s->shownText.clear();
	auto it = g_snapshots.find(s->timerId);
	TimerSnapshot placeholder;
	placeholder.text = s->timerId.empty() ? QStringLiteral("Pick a timer") : QStringLiteral("--:--");
	Apply(s, it != g_snapshots.end() ? it->second : placeholder);
}

void *Create(obs_data_t *settings, obs_source_t *self)
{
	auto *s = new TMinusSource();
	s->self = self;
	s->text = obs_source_create_private(obs_get_latest_input_type_id(kTextSourceId), "T-Minus text", nullptr);
	Update(s, settings);
	std::lock_guard<std::mutex> lock(g_mutex);
	g_sources.insert(s);
	return s;
}

void Destroy(void *data)
{
	auto *s = static_cast<TMinusSource *>(data);
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_sources.erase(s);
	}
	obs_source_release(s->text);
	delete s;
}

uint32_t Width(void *data)
{
	return obs_source_get_width(static_cast<TMinusSource *>(data)->text);
}

uint32_t Height(void *data)
{
	auto *s = static_cast<TMinusSource *>(data);
	return obs_source_get_height(s->text) + (s->bar ? kBarGap + s->barHeight : 0);
}

void Render(void *data, gs_effect_t *)
{
	auto *s = static_cast<TMinusSource *>(data);
	obs_source_video_render(s->text);

	const float progress = s->progress;
	if (!s->bar || progress < 0.0f)
		return;
	const float cx = static_cast<float>(obs_source_get_width(s->text));
	const float y = static_cast<float>(obs_source_get_height(s->text) + kBarGap);
	DrawRect(0.0f, y, cx, static_cast<float>(s->barHeight), s->barBackground);
	DrawRect(0.0f, y, cx * progress, static_cast<float>(s->barHeight), s->barColor);
}

void EnumActive(void *data, obs_source_enum_proc_t enumCallback, void *param)
{
	auto *s = static_cast<TMinusSource *>(data);
	enumCallback(s->self, s->text, param);
}

void Defaults(obs_data_t *settings)
{
	obs_data_t *font = obs_data_create();
	obs_data_set_default_string(font, "face", "Arial");
	obs_data_set_default_int(font, "size", 96);
	obs_data_set_default_obj(settings, "font", font);
	obs_data_release(font);
	obs_data_set_default_int(settings, "color", 0xFFFFFFFF);
	obs_data_set_default_int(settings, "warn_seconds", 60);
	obs_data_set_default_int(settings, "warn_color", 0xFF41A4D9);
	obs_data_set_default_int(settings, "crit_seconds", 10);
	obs_data_set_default_int(settings, "crit_color", 0xFF4F60E8);
	obs_data_set_default_int(settings, "bar_height", 8);
	obs_data_set_default_int(settings, "bar_color", 0xFFF07C4F);
	obs_data_set_default_int(settings, "bar_background", 0x40FFFFFF);
}

obs_properties_t *Properties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *timer =
		obs_properties_add_list(props, "timer", "Timer", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	if (TMinusDock *dock = TMinusDock::instance()) {
		for (TimerWidget *t : dock->timers())
			obs_property_list_add_string(timer, t->DisplayLabel().toUtf8().constData(),
						     t->timerId().toUtf8().constData());
	}
	obs_property_set_long_description(timer, "Shows the timer's text, including its format string and "
						 "finished text.");

	obs_properties_add_font(props, "font", "Font");
	obs_properties_add_color_alpha(props, "color", "Color");
	obs_properties_add_bool(props, "outline", "Outline");

	obs_properties_add_int(props, "warn_seconds", "Warning color from (seconds left, 0 = off)", 0, 86400, 1);
	obs_properties_add_color_alpha(props, "warn_color", "Warning color");
	obs_properties_add_int(props, "crit_seconds", "Final color from (seconds left, 0 = off)", 0, 86400, 1);
	obs_properties_add_color_alpha(props, "crit_color", "Final color");

	obs_properties_add_bool(props, "bar", "Show progress bar (countdowns)");
	obs_properties_add_int_slider(props, "bar_height", "Bar height", 2, 64, 1);
	obs_properties_add_color_alpha(props, "bar_color", "Bar color");
	obs_properties_add_color_alpha(props, "bar_background", "Bar background");
	return props;
}

} // namespace

void RegisterTMinusSource()
{
	obs_source_info info = {};
	info.id = "tminus_timer_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name = [](void *) {
		return "T-Minus Timer";
	};
	info.create = Create;
	info.destroy = Destroy;
	info.update = Update;
	info.get_width = Width;
	info.get_height = Height;
	info.video_render = Render;
	info.enum_active_sources = EnumActive;
	info.get_defaults = Defaults;
	info.get_properties = Properties;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	obs_register_source(&info);
}

void PublishTimerSnapshot(const QString &timerId, const TimerSnapshot &snapshot)
{
	const std::string id = timerId.toStdString();
	std::lock_guard<std::mutex> lock(g_mutex);
	g_snapshots[id] = snapshot;
	for (TMinusSource *s : g_sources) {
		if (s->timerId == id)
			Apply(s, snapshot);
	}
}
