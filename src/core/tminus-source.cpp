#include "tminus-source.hpp"
#include "../widgets/tminus-dock.hpp"

#include <obs-module.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <util/bmem.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <string>

// A source that shows a timer. A private text source draws the letters; they
// are captured into a texture and drawn again through tminus.effect, which
// fills them (and the progress bar, ring and background panel) with gradients.

namespace {

constexpr float kGap = 6.0f;
constexpr float kFadeSeconds = 0.5f;

#ifdef _WIN32
constexpr const char *kTextSourceId = "text_gdiplus";
#else
constexpr const char *kTextSourceId = "text_ft2_source";
#endif

enum Style { StyleNone = 0, StyleBelow, StyleAbove, StyleBehind, StyleRing };
enum State { StateNormal = 0, StateWarning, StateFinal };

// Three gradient stops; a solid fill repeats one color
struct Fill {
	vec4 c[3];
};

// Settings, written on the UI thread and read while rendering
struct Config {
	Fill states[3];
	long long warnMs = 0, critMs = 0;
	bool radial = false;
	vec2 direction;
	float speed = 0.0f;
	bool fade = true, pulse = false;
	int align = 1;
	float minWidth = 0.0f;
	int style = StyleNone;
	float barSize = 8.0f;
	bool rounded = true, barMatch = false;
	Fill bar;
	vec4 track;
	bool background = false;
	Fill panel;
	float padding = 0.0f, panelRadius = 0.0f;
};

struct TMinusSource {
	obs_source_t *self = nullptr;
	obs_source_t *text = nullptr;
	gs_texrender_t *texrender = nullptr;
	gs_effect_t *effect = nullptr;
	std::string timerId;
	std::string shownText;
	Config cfg;
	std::atomic<int> targetState{StateNormal};
	std::atomic<float> progress{-1.0f};

	// Animation, on the graphics thread
	int shownState = StateNormal;
	Fill from, current;
	float fadeT = 1.0f, shift = 0.0f, phase = 0.0f;
};

// Where things go, in pixels from the source's top left
struct Layout {
	float w = 0, h = 0;
	float tx = 0, ty = 0, tw = 0, th = 0; // text
	float px = 0, py = 0, pw = 0, ph = 0; // bar or ring
};

// Sources are created and destroyed on any thread and updated from the UI
// thread, so the registry is guarded
std::mutex g_mutex;
std::set<TMinusSource *> g_sources;
std::map<std::string, TimerSnapshot> g_snapshots;

vec4 Color(obs_data_t *settings, const char *key)
{
	vec4 c;
	vec4_from_rgba(&c, static_cast<uint32_t>(obs_data_get_int(settings, key)));
	return c;
}

void Lerp(vec4 *dst, const vec4 *a, const vec4 *b, float t)
{
	vec4_set(dst, a->x + (b->x - a->x) * t, a->y + (b->y - a->y) * t, a->z + (b->z - a->z) * t,
		 a->w + (b->w - a->w) * t);
}

Fill Solid(const vec4 &c)
{
	return {{c, c, c}};
}

// prefix, prefix_mid and prefix_end, as many as the fill type uses
Fill ReadFill(obs_data_t *settings, const std::string &prefix, int stops)
{
	Fill f;
	f.c[0] = Color(settings, prefix.c_str());
	f.c[2] = stops > 1 ? Color(settings, (prefix + "_end").c_str()) : f.c[0];
	if (stops > 2)
		f.c[1] = Color(settings, (prefix + "_mid").c_str());
	else
		Lerp(&f.c[1], &f.c[0], &f.c[2], 0.5f);
	return f;
}

// Called with g_mutex held
void Apply(TMinusSource *s, const TimerSnapshot &snap)
{
	int state = StateNormal;
	if (snap.countsDown && s->cfg.critMs > 0 && snap.timeMs <= s->cfg.critMs)
		state = StateFinal;
	else if (snap.countsDown && s->cfg.warnMs > 0 && snap.timeMs <= s->cfg.warnMs)
		state = StateWarning;
	s->targetState = state;
	s->progress = static_cast<float>(snap.progress);

	const std::string text = snap.text.toStdString();
	if (text == s->shownText)
		return;
	s->shownText = text;
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "text", text.c_str());
	obs_source_update(s->text, settings);
	obs_data_release(settings);
}

Layout GetLayout(const TMinusSource *s)
{
	const Config &c = s->cfg;
	Layout l;
	l.tw = static_cast<float>(obs_source_get_width(s->text));
	l.th = static_cast<float>(obs_source_get_height(s->text));
	const float cw = std::max(l.tw, c.minWidth);
	const float alignOffset = (cw - l.tw) * static_cast<float>(c.align) / 2.0f;

	switch (c.style) {
	case StyleBelow:
		l = {cw, l.th + kGap + c.barSize, alignOffset, 0, l.tw, l.th, 0, l.th + kGap, cw, c.barSize};
		break;
	case StyleAbove:
		l = {cw, l.th + kGap + c.barSize, alignOffset, c.barSize + kGap, l.tw, l.th, 0, 0, cw, c.barSize};
		break;
	case StyleBehind:
		l = {cw, l.th, alignOffset, 0, l.tw, l.th, 0, 0, cw, l.th};
		break;
	case StyleRing: {
		// The circle has to clear the text box's corners
		const float d = std::hypot(cw, l.th) + 2.0f * (c.barSize + kGap);
		l = {d, d, (d - cw) / 2.0f + alignOffset, (d - l.th) / 2.0f, l.tw, l.th, 0, 0, d, d};
		break;
	}
	default:
		l = {cw, l.th, alignOffset, 0, l.tw, l.th, 0, 0, 0, 0};
		break;
	}

	const float pad = c.background ? c.padding : 0.0f;
	l.w += 2.0f * pad;
	l.h += 2.0f * pad;
	l.tx += pad;
	l.ty += pad;
	l.px += pad;
	l.py += pad;
	return l;
}

struct Shape {
	float mode = 1.0f; // 0 text, 1 rounded rectangle, 2 ring
	float radius = 0.0f;
	float thickness = 0.0f;
	float progress = 1.0f;
	float uvScaleX = 1.0f;
	float opacity = 1.0f;
	bool animate = false;
};

void Draw(const TMinusSource *s, gs_texture_t *tex, float x, float y, float w, float h, const Fill &fill,
	  const Shape &shape)
{
	if (w <= 0.0f || h <= 0.0f)
		return;
	gs_effect_t *e = s->effect;
	auto setFloat = [e](const char *name, float v) {
		gs_effect_set_float(gs_effect_get_param_by_name(e, name), v);
	};
	auto setVec2 = [e](const char *name, float a, float b) {
		vec2 v;
		vec2_set(&v, a, b);
		gs_effect_set_vec2(gs_effect_get_param_by_name(e, name), &v);
	};

	setVec2("size", w, h);
	setVec2("uv_scale", shape.uvScaleX, 1.0f);
	setFloat("mode", shape.mode);
	setFloat("radius", shape.radius);
	setFloat("thickness", shape.thickness);
	setFloat("progress", shape.progress);
	gs_effect_set_vec4(gs_effect_get_param_by_name(e, "color0"), &fill.c[0]);
	gs_effect_set_vec4(gs_effect_get_param_by_name(e, "color1"), &fill.c[1]);
	gs_effect_set_vec4(gs_effect_get_param_by_name(e, "color2"), &fill.c[2]);
	setFloat("radial", s->cfg.radial ? 1.0f : 0.0f);
	gs_effect_set_vec2(gs_effect_get_param_by_name(e, "direction"), &s->cfg.direction);
	setFloat("shift", shape.animate ? s->shift : 0.0f);
	setFloat("opacity", shape.opacity);
	if (tex)
		gs_effect_set_texture(gs_effect_get_param_by_name(e, "image"), tex);

	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	while (gs_effect_loop(e, "Draw"))
		gs_draw_sprite(tex, 0, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
	gs_matrix_pop();
}

void Render(void *data, gs_effect_t *)
{
	auto *s = static_cast<TMinusSource *>(data);
	const Config &c = s->cfg;
	const Layout l = GetLayout(s);
	if (!s->effect || l.tw <= 0.0f || l.th <= 0.0f)
		return;

	// Capture the text as drawn, outline included
	gs_texrender_reset(s->texrender);
	if (gs_texrender_begin(s->texrender, static_cast<uint32_t>(l.tw), static_cast<uint32_t>(l.th))) {
		vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, l.tw, 0.0f, l.th, -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_source_video_render(s->text);
		gs_blend_state_pop();
		gs_texrender_end(s->texrender);
	}

	if (c.background)
		Draw(s, nullptr, 0, 0, l.w, l.h, c.panel, {1.0f, std::min(c.panelRadius, std::min(l.w, l.h) / 2.0f)});

	const float progress = s->progress;
	if (c.style != StyleNone && progress >= 0.0f) {
		const Fill &barFill = c.barMatch ? s->current : c.bar;
		if (c.style == StyleRing) {
			Draw(s, nullptr, l.px, l.py, l.pw, l.ph, Solid(c.track), {2.0f, 0.0f, c.barSize});
			Draw(s, nullptr, l.px, l.py, l.pw, l.ph, barFill,
			     {2.0f, 0.0f, c.barSize, progress, 1.0f, 1.0f, c.barMatch});
		} else {
			const float radius = c.rounded ? std::min(l.ph, l.pw) / 2.0f : 0.0f;
			Draw(s, nullptr, l.px, l.py, l.pw, l.ph, Solid(c.track), {1.0f, radius});
			Draw(s, nullptr, l.px, l.py, l.pw * progress, l.ph, barFill,
			     {1.0f, std::min(radius, l.pw * progress / 2.0f), 0.0f, 1.0f, progress, 1.0f, c.barMatch});
		}
	}

	// The final seconds can pulse
	const float opacity =
		s->shownState == StateFinal && c.pulse ? 0.65f + 0.35f * std::cos(s->phase * 2.0f * 3.14159265f) : 1.0f;
	Draw(s, gs_texrender_get_texture(s->texrender), l.tx, l.ty, l.tw, l.th, s->current,
	     {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, opacity, true});
}

void Tick(void *data, float seconds)
{
	auto *s = static_cast<TMinusSource *>(data);
	const int target = s->targetState;
	if (target != s->shownState) {
		s->from = s->current;
		s->shownState = target;
		s->fadeT = s->cfg.fade ? 0.0f : 1.0f;
	}
	s->fadeT = std::min(1.0f, s->fadeT + seconds / kFadeSeconds);
	for (int i = 0; i < 3; i++)
		Lerp(&s->current.c[i], &s->from.c[i], &s->cfg.states[target].c[i], s->fadeT);
	s->shift += s->cfg.speed * seconds;
	s->phase += seconds;
}

void Update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<TMinusSource *>(data);

	// The text is drawn white and colored by the shader
	obs_data_t *textSettings = obs_data_create();
	// Copied value by value: a font left at its default has no values of its
	// own, and the text source would see an empty font
	obs_data_t *font = obs_data_get_obj(settings, "font");
	obs_data_t *fontCopy = obs_data_create();
	for (const char *key : {"face", "style"})
		obs_data_set_string(fontCopy, key, obs_data_get_string(font, key));
	for (const char *key : {"size", "flags"})
		obs_data_set_int(fontCopy, key, obs_data_get_int(font, key));
	obs_data_set_obj(textSettings, "font", fontCopy);
	obs_data_release(fontCopy);
	obs_data_release(font);
	obs_data_set_bool(textSettings, "outline", obs_data_get_bool(settings, "outline"));
	obs_data_set_int(textSettings, "outline_size", 4);
	obs_data_set_int(textSettings, "color", 0xFFFFFFFF);
	obs_data_set_int(textSettings, "color1", 0xFFFFFFFF);
	obs_data_set_int(textSettings, "color2", 0xFFFFFFFF);
	obs_source_update(s->text, textSettings);
	obs_data_release(textSettings);

	Config c;
	const int stops = static_cast<int>(obs_data_get_int(settings, "fill_type")) + 1;
	c.states[StateNormal] = ReadFill(settings, "color", stops);
	c.states[StateWarning] = ReadFill(settings, "warn_color", stops);
	c.states[StateFinal] = ReadFill(settings, "crit_color", stops);
	c.warnMs = obs_data_get_int(settings, "warn_seconds") * 1000;
	c.critMs = obs_data_get_int(settings, "crit_seconds") * 1000;
	c.radial = obs_data_get_int(settings, "gradient_shape") == 1;
	const float angle = static_cast<float>(obs_data_get_int(settings, "gradient_angle")) * 3.14159265f / 180.0f;
	vec2_set(&c.direction, std::cos(angle), std::sin(angle));
	c.speed = static_cast<float>(obs_data_get_int(settings, "gradient_speed")) / 100.0f;
	c.fade = obs_data_get_bool(settings, "fade");
	c.pulse = obs_data_get_bool(settings, "pulse");
	c.align = static_cast<int>(obs_data_get_int(settings, "align"));
	c.minWidth = static_cast<float>(obs_data_get_int(settings, "min_width"));
	c.style = static_cast<int>(obs_data_get_int(settings, "progress_style"));
	c.barSize = static_cast<float>(obs_data_get_int(settings, "bar_size"));
	c.rounded = obs_data_get_bool(settings, "bar_rounded");
	c.barMatch = obs_data_get_bool(settings, "bar_match");
	c.bar = ReadFill(settings, "bar_color", std::min(stops, 2));
	c.track = Color(settings, "bar_track");
	c.background = obs_data_get_bool(settings, "background");
	c.panel = ReadFill(settings, "bg_color", std::min(stops, 2));
	c.padding = static_cast<float>(obs_data_get_int(settings, "bg_padding"));
	c.panelRadius = static_cast<float>(obs_data_get_int(settings, "bg_radius"));

	std::lock_guard<std::mutex> lock(g_mutex);
	s->cfg = c;
	s->timerId = obs_data_get_string(settings, "timer");

	// Re-apply whatever the timer shows now
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

	char *path = obs_module_file("effects/tminus.effect");
	obs_enter_graphics();
	s->texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	s->effect = gs_effect_create_from_file(path, nullptr);
	obs_leave_graphics();
	bfree(path);

	Update(s, settings);
	s->current = s->from = s->cfg.states[StateNormal];
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
	obs_enter_graphics();
	gs_texrender_destroy(s->texrender);
	gs_effect_destroy(s->effect);
	obs_leave_graphics();
	obs_source_release(s->text);
	delete s;
}

uint32_t Width(void *data)
{
	return static_cast<uint32_t>(GetLayout(static_cast<TMinusSource *>(data)).w);
}

uint32_t Height(void *data)
{
	return static_cast<uint32_t>(GetLayout(static_cast<TMinusSource *>(data)).h);
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

	const std::pair<const char *, long long> ints[] = {{"align", 1},
							   {"color", 0xFFFFFFFF},
							   {"color_mid", 0xFFFFE6D0},
							   {"color_end", 0xFFF0C8A0},
							   {"warn_seconds", 60},
							   {"warn_color", 0xFF41A4D9},
							   {"warn_color_mid", 0xFF3C8CE8},
							   {"warn_color_end", 0xFF2E6BE0},
							   {"crit_seconds", 10},
							   {"crit_color", 0xFF4F60E8},
							   {"crit_color_mid", 0xFF4A3FD8},
							   {"crit_color_end", 0xFF6A2FB0},
							   {"gradient_angle", 90},
							   {"bar_size", 8},
							   {"bar_color", 0xFFF07C4F},
							   {"bar_color_end", 0xFFE05AB0},
							   {"bar_track", 0x40FFFFFF},
							   {"bg_color", 0xC0201C18},
							   {"bg_color_end", 0xC0302820},
							   {"bg_padding", 24},
							   {"bg_radius", 16}};
	for (const auto &[key, value] : ints)
		obs_data_set_default_int(settings, key, value);
	obs_data_set_default_bool(settings, "fade", true);
	obs_data_set_default_bool(settings, "bar_rounded", true);
}

// Shows only the settings the current choices use
bool RefreshVisibility(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const long long stops = obs_data_get_int(settings, "fill_type") + 1;
	const bool progress = obs_data_get_int(settings, "progress_style") != StyleNone;
	const bool ownColors = progress && !obs_data_get_bool(settings, "bar_match");
	for (const char *key : {"color_end", "warn_color_end", "crit_color_end", "bg_color_end"})
		obs_property_set_visible(obs_properties_get(props, key), stops > 1);
	for (const char *key : {"color_mid", "warn_color_mid", "crit_color_mid"})
		obs_property_set_visible(obs_properties_get(props, key), stops > 2);
	for (const char *key : {"gradient_shape", "gradient_angle", "gradient_speed"})
		obs_property_set_visible(obs_properties_get(props, key), stops > 1);
	for (const char *key : {"bar_size", "bar_rounded", "bar_match", "bar_track"})
		obs_property_set_visible(obs_properties_get(props, key), progress);
	obs_property_set_visible(obs_properties_get(props, "bar_color"), ownColors);
	obs_property_set_visible(obs_properties_get(props, "bar_color_end"), ownColors && stops > 1);
	return true;
}

obs_properties_t *Group(obs_properties_t *props, const char *name, const char *label,
			obs_group_type type = OBS_GROUP_NORMAL)
{
	obs_properties_t *group = obs_properties_create();
	obs_properties_add_group(props, name, label, type, group);
	return group;
}

void AddColors(obs_properties_t *group, const std::string &prefix)
{
	obs_properties_add_color_alpha(group, prefix.c_str(), "Color");
	obs_properties_add_color_alpha(group, (prefix + "_mid").c_str(), "Middle color");
	obs_properties_add_color_alpha(group, (prefix + "_end").c_str(), "End color");
}

obs_properties_t *Properties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_t *text = Group(props, "group_text", "Text");
	obs_property_t *timer =
		obs_properties_add_list(text, "timer", "Timer", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	if (TMinusDock *dock = TMinusDock::instance()) {
		for (TimerWidget *t : dock->timers())
			obs_property_list_add_string(timer, t->DisplayLabel().toUtf8().constData(),
						     t->timerId().toUtf8().constData());
	}
	obs_property_set_long_description(timer, "Shows the timer's text, including its format string and "
						 "finished text.");
	obs_properties_add_font(text, "font", "Font");
	obs_properties_add_bool(text, "outline", "Outline");
	obs_property_t *align =
		obs_properties_add_list(text, "align", "Alignment", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(align, "Left", 0);
	obs_property_list_add_int(align, "Center", 1);
	obs_property_list_add_int(align, "Right", 2);
	obs_property_t *minWidth = obs_properties_add_int(text, "min_width", "Minimum width", 0, 4096, 1);
	obs_property_int_set_suffix(minWidth, " px");
	obs_property_set_long_description(minWidth, "Keeps the source from resizing as the digits change.");

	obs_properties_t *fill = Group(props, "group_fill", "Fill");
	obs_property_t *fillType =
		obs_properties_add_list(fill, "fill_type", "Fill", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(fillType, "Solid", 0);
	obs_property_list_add_int(fillType, "Two-color gradient", 1);
	obs_property_list_add_int(fillType, "Three-color gradient", 2);
	obs_property_set_modified_callback(fillType, RefreshVisibility);
	obs_property_t *shape = obs_properties_add_list(fill, "gradient_shape", "Gradient shape", OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(shape, "Linear", 0);
	obs_property_list_add_int(shape, "Radial", 1);
	obs_property_t *angle = obs_properties_add_int_slider(fill, "gradient_angle", "Gradient angle", 0, 359, 1);
	obs_property_int_set_suffix(angle, QString::fromUtf8("°").toUtf8().constData());
	obs_properties_add_int_slider(fill, "gradient_speed", "Gradient animation speed (0 = still)", 0, 200, 1);
	AddColors(fill, "color");
	obs_properties_add_bool(fill, "fade", "Fade between colors");

	obs_properties_t *warn = Group(props, "group_warn", "Warning");
	obs_property_t *warnAt = obs_properties_add_int(warn, "warn_seconds", "From (0 = off)", 0, 86400, 1);
	obs_property_int_set_suffix(warnAt, " s left");
	AddColors(warn, "warn_color");

	obs_properties_t *crit = Group(props, "group_final", "Final seconds");
	obs_property_t *critAt = obs_properties_add_int(crit, "crit_seconds", "From (0 = off)", 0, 86400, 1);
	obs_property_int_set_suffix(critAt, " s left");
	AddColors(crit, "crit_color");
	obs_properties_add_bool(crit, "pulse", "Pulse");

	obs_properties_t *progress = Group(props, "group_progress", "Progress (countdowns)");
	obs_property_t *style =
		obs_properties_add_list(progress, "progress_style", "Style", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(style, "None", StyleNone);
	obs_property_list_add_int(style, "Bar below", StyleBelow);
	obs_property_list_add_int(style, "Bar above", StyleAbove);
	obs_property_list_add_int(style, "Bar behind the text", StyleBehind);
	obs_property_list_add_int(style, "Ring", StyleRing);
	obs_property_set_modified_callback(style, RefreshVisibility);
	obs_properties_add_int_slider(progress, "bar_size", "Thickness", 2, 64, 1);
	obs_properties_add_bool(progress, "bar_rounded", "Rounded ends");
	obs_property_set_modified_callback(obs_properties_add_bool(progress, "bar_match", "Use the text's colors"),
					   RefreshVisibility);
	obs_properties_add_color_alpha(progress, "bar_color", "Color");
	obs_properties_add_color_alpha(progress, "bar_color_end", "End color");
	obs_properties_add_color_alpha(progress, "bar_track", "Track color");

	obs_properties_t *panel = Group(props, "background", "Background", OBS_GROUP_CHECKABLE);
	obs_properties_add_color_alpha(panel, "bg_color", "Color");
	obs_properties_add_color_alpha(panel, "bg_color_end", "End color");
	obs_properties_add_int(panel, "bg_padding", "Padding", 0, 512, 1);
	obs_properties_add_int(panel, "bg_radius", "Corner radius", 0, 512, 1);

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
	info.video_tick = Tick;
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
