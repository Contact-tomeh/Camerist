/*
Camerist - camera control for OBS Studio
Copyright (C) 2026 tomi_tomeh

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "app.hpp"
#include "browser-panel.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <util/dstr.h>
#include <util/platform.h>

#include <QLabel>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

App &app()
{
	static App instance;
	return instance;
}

/* ---------------------------------------------------------------------- */
/* Actions                                                                 */
/* ---------------------------------------------------------------------- */
static double Dur(double d)
{
	return d < 0 ? app().store.MoveDuration() : d;
}

QJsonObject GoPreset(const std::string &key, double duration)
{
	QJsonObject p = app().store.FindPreset(key);
	if (p.isEmpty())
		throw std::runtime_error(Text("Error.PresetNotFound"));
	std::map<int, double> target;
	for (int a = 0; a < AXIS_COUNT; a++)
		if (p.contains(AXIS_NAMES[a]))
			target[a] = p.value(AXIS_NAMES[a]).toDouble();
	app().cam.GoTo(target, Dur(duration));
	return p;
}

void DoHome(double duration)
{
	app().cam.Home(Dur(duration));
}

void DoFlip(double duration)
{
	CameraStatus s = app().cam.Status();
	if (!s.has[AXIS_PAN])
		throw std::runtime_error(Text("Error.NotControllable"));
	double front = (double)s.ranges[AXIS_PAN].def;
	/* si on regarde plutôt devant, on passe derrière, sinon on revient devant */
	double target = s.pos[AXIS_PAN] < front + 90 ? front + 180 : front;
	app().cam.GoTo({{AXIS_PAN, target}}, Dur(duration));
}

void DoZoom(int direction)
{
	CameraStatus s = app().cam.Status();
	if (!s.has[AXIS_ZOOM])
		throw std::runtime_error(Text("Error.NotControllable"));
	double step = app().store.Get().value("zoom_step").toDouble(25);
	app().cam.GoTo({{AXIS_ZOOM, s.pos[AXIS_ZOOM] + direction * step}}, std::min(0.6, app().store.MoveDuration()));
}

/* ---------------------------------------------------------------------- */
/* Raccourcis clavier, enregistrés dans Paramètres > Raccourcis d'OBS      */
/* ---------------------------------------------------------------------- */
static std::map<std::string, obs_hotkey_id> hotkeys; // "home", "preset:<id>"…
static std::mutex hotkeyMutex;
static obs_data_t *hotkeyData = nullptr; // combinaisons enregistrées

static std::string HotkeysPath()
{
	char *p = obs_module_config_path("hotkeys.json");
	std::string s = p ? p : "";
	bfree(p);
	return s;
}

static void RunSafe(const std::function<void()> &fn)
{
	try {
		fn();
	} catch (const std::exception &e) {
		obs_log(LOG_WARNING, "hotkey: %s", e.what());
	}
}

static void HotkeyPressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	std::string key = static_cast<const char *>(data);
	/* on ne bloque pas le thread des raccourcis d'OBS */
	std::thread([key]() {
		RunSafe([&]() {
			if (key == "home")
				DoHome();
			else if (key == "flip")
				DoFlip();
			else if (key == "zoom_in")
				DoZoom(1);
			else if (key == "zoom_out")
				DoZoom(-1);
			else if (key.rfind("preset:", 0) == 0)
				GoPreset(key.substr(7));
		});
	}).detach();
}

/* Les chaînes passées en `data` doivent vivre aussi longtemps que le raccourci. */
static std::map<std::string, std::unique_ptr<std::string>> hotkeyKeys;

static void RegisterHotkey(const std::string &key, const std::string &description)
{
	auto it = hotkeys.find(key);
	if (it != hotkeys.end()) {
		obs_hotkey_set_description(it->second, description.c_str());
		return;
	}
	hotkeyKeys[key] = std::make_unique<std::string>(key);
	std::string name = "camerist." + key;
	std::replace(name.begin(), name.end(), ':', '.');
	obs_hotkey_id id = obs_hotkey_register_frontend(name.c_str(), description.c_str(), HotkeyPressed,
							(void *)hotkeyKeys[key]->c_str());
	hotkeys[key] = id;
	if (hotkeyData) {
		obs_data_array_t *arr = obs_data_get_array(hotkeyData, name.c_str());
		if (arr) {
			obs_hotkey_load(id, arr);
			obs_data_array_release(arr);
		}
	}
}

static void RegisterFixedHotkeys()
{
	std::lock_guard<std::mutex> lock(hotkeyMutex);
	RegisterHotkey("home", Text("Hotkey.Home"));
	RegisterHotkey("flip", Text("Hotkey.Flip"));
	RegisterHotkey("zoom_in", Text("Hotkey.ZoomIn"));
	RegisterHotkey("zoom_out", Text("Hotkey.ZoomOut"));
}

void SyncPresetHotkeys()
{
	std::lock_guard<std::mutex> lock(hotkeyMutex);
	QJsonArray presets = app().store.Presets();
	std::map<std::string, bool> keep;
	for (int i = 0; i < presets.size(); i++) {
		QJsonObject p = presets[i].toObject();
		std::string key = "preset:" + p.value("id").toString().toStdString();
		std::string desc = Text("Hotkey.Preset") + " " + std::to_string(i + 1) + " : " +
				   p.value("name").toString().toStdString();
		RegisterHotkey(key, desc);
		keep[key] = true;
	}
	for (auto it = hotkeys.begin(); it != hotkeys.end();) {
		if (it->first.rfind("preset:", 0) == 0 && !keep.count(it->first)) {
			obs_hotkey_unregister(it->second);
			hotkeyKeys.erase(it->first);
			it = hotkeys.erase(it);
		} else {
			++it;
		}
	}
}

void SaveHotkeys()
{
	std::lock_guard<std::mutex> lock(hotkeyMutex);
	if (!hotkeyData)
		hotkeyData = obs_data_create();
	for (auto &kv : hotkeys) {
		std::string name = "camerist." + kv.first;
		std::replace(name.begin(), name.end(), ':', '.');
		obs_data_array_t *arr = obs_hotkey_save(kv.second);
		obs_data_set_array(hotkeyData, name.c_str(), arr);
		obs_data_array_release(arr);
	}
	obs_data_save_json_safe(hotkeyData, HotkeysPath().c_str(), "tmp", "bak");
}

struct BindingSearch {
	obs_hotkey_id id;
	std::string text;
};

std::string HotkeyText(const std::string &key)
{
	obs_hotkey_id id;
	{
		std::lock_guard<std::mutex> lock(hotkeyMutex);
		auto it = hotkeys.find(key);
		if (it == hotkeys.end())
			return "";
		id = it->second;
	}
	BindingSearch search{id, ""};
	obs_enum_hotkey_bindings(
		[](void *data, size_t, obs_hotkey_binding_t *binding) {
			auto *s = static_cast<BindingSearch *>(data);
			if (obs_hotkey_binding_get_hotkey_id(binding) != s->id)
				return true;
			struct dstr str = {0};
			obs_key_combination_to_str(obs_hotkey_binding_get_key_combination(binding), &str);
			if (str.array && *str.array) {
				if (!s->text.empty())
					s->text += ", ";
				s->text += str.array;
			}
			dstr_free(&str);
			return true;
		},
		&search);
	return search.text;
}

bool SetHotkey(const std::string &key, const QJsonObject &combo)
{
	obs_hotkey_id id;
	{
		std::lock_guard<std::mutex> lock(hotkeyMutex);
		auto it = hotkeys.find(key);
		if (it == hotkeys.end())
			return false;
		id = it->second;
	}
	if (combo.isEmpty()) {
		obs_hotkey_load_bindings(id, nullptr, 0);
	} else {
		obs_key_combination_t c = {};
		c.key = obs_key_from_name(combo.value("key").toString().toUtf8().constData());
		if (c.key == OBS_KEY_NONE)
			return false;
		if (combo.value("shift").toBool())
			c.modifiers |= INTERACT_SHIFT_KEY;
		if (combo.value("ctrl").toBool())
			c.modifiers |= INTERACT_CONTROL_KEY;
		if (combo.value("alt").toBool())
			c.modifiers |= INTERACT_ALT_KEY;
		if (combo.value("meta").toBool())
			c.modifiers |= INTERACT_COMMAND_KEY;
		obs_hotkey_load_bindings(id, &c, 1);
	}
	SaveHotkeys();
	return true;
}

/* ---------------------------------------------------------------------- */
/* Dock dans OBS                                                           */
/* ---------------------------------------------------------------------- */
static void CreateDock()
{
	if (!app().port)
		return;
	std::string url = "http://127.0.0.1:" + std::to_string(app().port) + "/";
	QWidget *content = nullptr;
	QCef *cef = obs_browser_init_panel();
	if (cef) {
		cef->init_browser();
		cef->wait_for_browser_init();
		content = cef->create_widget(nullptr, url);
	}
	if (!content) {
		/* obs-browser absent : on indique au moins l'adresse */
		auto *label = new QLabel(QString::fromStdString(Text("Dock.NoBrowser") + "\n" + url));
		label->setWordWrap(true);
		label->setTextInteractionFlags(Qt::TextSelectableByMouse);
		label->setMargin(12);
		content = label;
	}
	content->setMinimumSize(300, 300);
	if (!obs_frontend_add_dock_by_id("camerist", Text("Dock.Title").c_str(), content))
		obs_log(LOG_WARNING, "could not add the dock");
}

/* ---------------------------------------------------------------------- */
/* Évènements d'OBS                                                        */
/* ---------------------------------------------------------------------- */
static void OnSceneChanged()
{
	obs_source_t *scene = obs_frontend_get_current_scene();
	if (!scene)
		return;
	std::string name = obs_source_get_name(scene);
	obs_source_release(scene);
	std::string preset = app().store.PresetForScene(name);
	if (preset.empty())
		return;
	std::thread([preset]() { RunSafe([&]() { GoPreset(preset); }); }).detach();
}

static void OnFrontendEvent(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		CreateDock();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		OnSceneChanged();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		SaveHotkeys();
		break;
	default:
		break;
	}
}

/* ---------------------------------------------------------------------- */
/* Chargement du module                                                    */
/* ---------------------------------------------------------------------- */
bool obs_module_load(void)
{
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	char *cfgPath = obs_module_config_path("camerist.json");
	app().store.Load(cfgPath ? cfgPath : "");
	bfree(cfgPath);

	hotkeyData = obs_data_create_from_json_file_safe(HotkeysPath().c_str(), "bak");

	QJsonObject cfg = app().store.Get();
	QJsonObject js = cfg.value("jog_speed").toObject();
	for (int a = 0; a < AXIS_COUNT; a++)
		if (js.contains(AXIS_NAMES[a]))
			app().cam.SetJogSpeed(a, js.value(AXIS_NAMES[a]).toDouble());
	app().cam.Start(app().store.CameraName());

	StartServer(app().store.Port());

	RegisterFixedHotkeys();
	SyncPresetHotkeys();
	obs_frontend_add_event_callback(OnFrontendEvent, nullptr);

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(OnFrontendEvent, nullptr);
	StopServer();
	app().cam.Stop();
	if (hotkeyData) {
		obs_data_release(hotkeyData);
		hotkeyData = nullptr;
	}
	obs_log(LOG_INFO, "plugin unloaded");
}
