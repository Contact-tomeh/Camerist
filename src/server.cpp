/*
Camerist - camera control for OBS Studio
Copyright (C) 2026 tomi_tomeh

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/* Petit serveur local (127.0.0.1 uniquement) : il sert le panneau du dock
   et répond au panneau, au Stream Deck et à tout outil qui sait appeler
   une adresse web. */

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include "thirdparty/httplib.h"

#include "app.hpp"

#include <obs-module.h>
#include <plugin-support.h> // après obs-module.h (déclare blogva)

#include <QFile>
#include <QJsonDocument>
#include <QUuid>

#include <memory>
#include <stdexcept>
#include <thread>

using httplib::Request;
using httplib::Response;

static std::unique_ptr<httplib::Server> server;
static std::thread serverThread;

/* ---------------------------------------------------------------------- */
/* Outils JSON                                                             */
/* ---------------------------------------------------------------------- */
static QJsonObject Body(const Request &req)
{
	QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(req.body));
	return doc.isObject() ? doc.object() : QJsonObject();
}

static void Send(Response &res, QJsonObject obj, int status = 200)
{
	if (!obj.contains("ok"))
		obj["ok"] = status < 400;
	res.status = status;
	res.set_header("Cache-Control", "no-store");
	res.set_content(QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString(), "application/json; charset=utf-8");
}

static void Ok(Response &res, QJsonObject obj = QJsonObject())
{
	obj["ok"] = true;
	Send(res, obj);
}

class NotFound : public std::runtime_error {
	using std::runtime_error::runtime_error;
};

/* Enveloppe chaque route : toute erreur devient une réponse {ok:false,error} */
template<typename F> static httplib::Server::Handler Route(F fn)
{
	return [fn](const Request &req, Response &res) {
		try {
			fn(req, res);
		} catch (const NotFound &e) {
			Send(res, QJsonObject{{"ok", false}, {"error", QString::fromUtf8(e.what())}}, 404);
		} catch (const std::exception &e) {
			Send(res, QJsonObject{{"ok", false}, {"error", QString::fromUtf8(e.what())}}, 500);
		}
	};
}

static double ArgDuration(const Request &req)
{
	if (req.has_param("d")) {
		std::string d = req.get_param_value("d");
		if (!d.empty())
			return std::stod(d);
	}
	return -1;
}

static QJsonArray ToArray(const std::vector<std::string> &v)
{
	QJsonArray a;
	for (auto &s : v)
		a.append(QString::fromStdString(s));
	return a;
}

/* ---------------------------------------------------------------------- */
/* État complet pour le panneau                                            */
/* ---------------------------------------------------------------------- */
static QJsonObject CameraJson()
{
	CameraStatus s = app().cam.Status();
	QJsonObject ranges, pos;
	for (int a = 0; a < AXIS_COUNT; a++) {
		pos[AXIS_NAMES[a]] = std::round(s.pos[a]);
		if (s.has[a])
			ranges[AXIS_NAMES[a]] = QJsonObject{{"min", (qint64)s.ranges[a].min},
							    {"max", (qint64)s.ranges[a].max},
							    {"step", (qint64)s.ranges[a].step},
							    {"default", (qint64)s.ranges[a].def}};
	}
	return QJsonObject{{"connected", s.connected},
			   {"controllable", s.controllable},
			   {"name", QString::fromStdString(s.name)},
			   {"error", QString::fromStdString(s.error)},
			   {"ranges", ranges},
			   {"pos", pos},
			   {"moving", s.moving}};
}

static QJsonObject StateJson()
{
	QJsonObject cfg = app().store.Get();
	QJsonArray presets = cfg.value("presets").toArray();
	QJsonObject hotkeys;
	for (const char *k : {"home", "flip", "zoom_in", "zoom_out"})
		hotkeys[k] = QString::fromStdString(HotkeyText(k));
	for (auto v : presets) {
		std::string id = v.toObject().value("id").toString().toStdString();
		hotkeys[QString::fromStdString("preset:" + id)] = QString::fromStdString(HotkeyText("preset:" + id));
	}
	QJsonObject settings;
	for (const char *k : {"move_duration", "jog_speed", "zoom_step", "nudge_step", "camera_name", "source"})
		settings[k] = cfg.value(k);
	return QJsonObject{{"camera", CameraJson()},
			   {"presets", presets},
			   {"scenes", cfg.value("scenes")},
			   {"settings", settings},
			   {"hotkeys", hotkeys},
			   {"ui", cfg.value("ui")},
			   {"locale", QString::fromStdString(ObsLocale())},
			   {"version", PLUGIN_VERSION},
			   {"obs", obs_get_version_string()},
			   {"port", app().port},
			   {"links", QJsonObject{{"compat", "https://github.com/Contact-tomeh/camerist#compatible-cameras"}}}};
}

/* ---------------------------------------------------------------------- */
/* Routes                                                                  */
/* ---------------------------------------------------------------------- */
static void SetupRoutes(httplib::Server &s)
{
	/* le panneau */
	s.Get("/", Route([](const Request &, Response &res) {
		QFile f(QString::fromUtf8(obs_module_file("web/index.html")));
		if (!f.open(QIODevice::ReadOnly))
			throw std::runtime_error("web/index.html missing");
		res.set_header("Cache-Control", "no-store");
		res.set_content(f.readAll().toStdString(), "text/html; charset=utf-8");
	}));

	s.Get("/api/state", Route([](const Request &, Response &res) { Ok(res, StateJson()); }));

	/* --- mouvements --- */
	s.Post("/api/jog", Route([](const Request &req, Response &res) {
		QJsonObject b = Body(req);
		int axis = AxisFromName(b.value("axis").toString().toStdString());
		if (b.value("stop").toBool())
			app().cam.JogStop(axis);
		else if (axis >= 0)
			app().cam.JogStart(axis, b.value("dir").toDouble(1));
		Ok(res);
	}));

	auto nudge = Route([](const Request &req, Response &res) {
		std::string axisName = req.get_param_value("axis");
		std::string delta = req.get_param_value("delta");
		QJsonObject b = Body(req);
		if (axisName.empty())
			axisName = b.value("axis").toString().toStdString();
		int axis = AxisFromName(axisName);
		if (axis < 0)
			throw std::runtime_error("unknown axis");
		QJsonObject cfg = app().store.Get();
		double step = cfg.value(axis == AXIS_ZOOM ? "zoom_step" : "nudge_step").toDouble(5);
		double d = !delta.empty() ? std::stod(delta) : b.contains("delta") ? b.value("delta").toDouble() : step;
		Ok(res, QJsonObject{{"value", app().cam.Nudge(axis, d)}});
	});
	s.Get("/api/nudge", nudge);
	s.Post("/api/nudge", nudge);

	s.Post("/api/goto", Route([](const Request &req, Response &res) {
		QJsonObject b = Body(req);
		std::map<int, double> target;
		for (int a = 0; a < AXIS_COUNT; a++)
			if (b.contains(AXIS_NAMES[a]))
				target[a] = b.value(AXIS_NAMES[a]).toDouble();
		app().cam.GoTo(target, b.value("duration").toDouble(0));
		Ok(res);
	}));

	auto home = Route([](const Request &req, Response &res) {
		DoHome(ArgDuration(req));
		Ok(res);
	});
	s.Get("/api/home", home);
	s.Post("/api/home", home);

	auto flip = Route([](const Request &req, Response &res) {
		DoFlip(ArgDuration(req));
		Ok(res);
	});
	s.Get("/api/flip", flip);
	s.Post("/api/flip", flip);

	auto zoom = Route([](const Request &req, Response &res) {
		DoZoom(req.matches[1] == "in" ? 1 : -1);
		Ok(res);
	});
	s.Get(R"(/api/zoom/(in|out))", zoom);
	s.Post(R"(/api/zoom/(in|out))", zoom);

	/* --- positions --- */
	s.Post("/api/presets", Route([](const Request &req, Response &res) {
		QJsonObject b = Body(req);
		CameraStatus st = app().cam.Status();
		if (!st.controllable)
			throw std::runtime_error(Text("Error.NotControllable"));
		QJsonObject saved;
		app().store.Edit([&](QJsonObject &cfg) {
			QJsonArray list = cfg.value("presets").toArray();
			int idx = -1;
			QString id = b.value("id").toString();
			for (int i = 0; i < list.size() && !id.isEmpty(); i++)
				if (list[i].toObject().value("id").toString() == id)
					idx = i;
			QJsonObject p = idx >= 0 ? list[idx].toObject() : QJsonObject();
			if (idx < 0) {
				p["id"] = QUuid::createUuid().toString(QUuid::Id128).left(8);
				QString name = b.value("name").toString().trimmed();
				p["name"] = name.isEmpty() ? QString("Position %1").arg(list.size() + 1) : name;
			}
			for (int a = 0; a < AXIS_COUNT; a++)
				if (st.has[a])
					p[AXIS_NAMES[a]] = std::round(st.pos[a]);
			if (idx >= 0)
				list[idx] = p;
			else
				list.append(p);
			cfg["presets"] = list;
			saved = p;
		});
		SyncPresetHotkeys();
		Ok(res, QJsonObject{{"preset", saved}});
	}));

	s.Patch(R"(/api/presets/([\w-]+))", Route([](const Request &req, Response &res) {
		QJsonObject b = Body(req);
		QString id = QString::fromStdString(req.matches[1]);
		bool found = false;
		app().store.Edit([&](QJsonObject &cfg) {
			QJsonArray list = cfg.value("presets").toArray();
			for (int i = 0; i < list.size(); i++) {
				QJsonObject p = list[i].toObject();
				if (p.value("id").toString() != id)
					continue;
				if (b.contains("name")) {
					QString name = b.value("name").toString().trimmed();
					if (!name.isEmpty())
						p["name"] = name;
				}
				list[i] = p;
				found = true;
			}
			cfg["presets"] = list;
		});
		if (!found)
			throw NotFound(Text("Error.PresetNotFound"));
		SyncPresetHotkeys();
		Ok(res);
	}));

	s.Delete(R"(/api/presets/([\w-]+))", Route([](const Request &req, Response &res) {
		QString id = QString::fromStdString(req.matches[1]);
		app().store.Edit([&](QJsonObject &cfg) {
			QJsonArray list, old = cfg.value("presets").toArray();
			for (auto v : old)
				if (v.toObject().value("id").toString() != id)
					list.append(v);
			cfg["presets"] = list;
			/* retire aussi la position des scènes liées */
			QJsonObject scenes = cfg.value("scenes").toObject();
			for (auto k : scenes.keys())
				if (scenes.value(k).toString() == id)
					scenes.remove(k);
			cfg["scenes"] = scenes;
		});
		SyncPresetHotkeys();
		Ok(res);
	}));

	s.Post("/api/presets/order", Route([](const Request &req, Response &res) {
		QJsonArray ids = Body(req).value("ids").toArray();
		app().store.Edit([&](QJsonObject &cfg) {
			QJsonArray old = cfg.value("presets").toArray(), list;
			for (auto id : ids)
				for (auto v : old)
					if (v.toObject().value("id") == id)
						list.append(v);
			for (auto v : old)
				if (!ids.contains(v.toObject().value("id")))
					list.append(v);
			cfg["presets"] = list;
		});
		SyncPresetHotkeys(); // les numéros dans les noms de raccourcis changent
		Ok(res);
	}));

	auto presetGo = Route([](const Request &req, Response &res) {
		QJsonObject p = GoPreset(req.matches[1], ArgDuration(req));
		Ok(res, QJsonObject{{"preset", p.value("name")}});
	});
	s.Get(R"(/api/preset/([^/]+)/go)", presetGo);
	s.Post(R"(/api/preset/([^/]+)/go)", presetGo);
	s.Get(R"(/api/preset/([^/]+))", presetGo);

	/* --- scènes liées --- */
	s.Get("/api/scenes", Route([](const Request &, Response &res) {
		Ok(res, QJsonObject{{"scenes", ToArray(ListScenes())}, {"map", app().store.Get().value("scenes")}});
	}));
	s.Post("/api/scenes", Route([](const Request &req, Response &res) {
		QJsonObject b = Body(req);
		QString scene = b.value("scene").toString(), preset = b.value("preset").toString();
		app().store.Edit([&](QJsonObject &cfg) {
			QJsonObject map = cfg.value("scenes").toObject();
			if (preset.isEmpty())
				map.remove(scene);
			else
				map[scene] = preset;
			cfg["scenes"] = map;
		});
		Ok(res);
	}));

	/* --- raccourcis (enregistrés dans les raccourcis d'OBS) --- */
	s.Post("/api/hotkey", Route([](const Request &req, Response &res) {
		QJsonObject b = Body(req);
		std::string target = b.value("target").toString().toStdString();
		if (!SetHotkey(target, b.value("clear").toBool() ? QJsonObject() : b.value("combo").toObject()))
			throw std::runtime_error(Text("Error.Hotkey"));
		Ok(res, QJsonObject{{"text", QString::fromStdString(HotkeyText(target))}});
	}));

	/* --- réglages de l'appli --- */
	s.Post("/api/settings", Route([](const Request &req, Response &res) {
		QJsonObject b = Body(req);
		bool reconnect = false;
		app().store.Edit([&](QJsonObject &cfg) {
			for (const char *k : {"move_duration", "zoom_step", "nudge_step"})
				if (b.contains(k))
					cfg[k] = b.value(k).toDouble();
			if (b.contains("jog_speed")) {
				QJsonObject js = cfg.value("jog_speed").toObject(), in = b.value("jog_speed").toObject();
				for (auto k : in.keys())
					js[k] = in.value(k).toDouble();
				cfg["jog_speed"] = js;
			}
			if (b.contains("ui")) {
				QJsonObject ui = cfg.value("ui").toObject(), in = b.value("ui").toObject();
				for (auto k : {"theme", "accent"})
					if (in.contains(k))
						ui[k] = in.value(k).toString();
				cfg["ui"] = ui;
			}
			if (b.contains("source"))
				cfg["source"] = b.value("source").toString();
			if (b.contains("camera_name") && b.value("camera_name") != cfg.value("camera_name")) {
				cfg["camera_name"] = b.value("camera_name").toString();
				reconnect = true;
			}
		});
		QJsonObject js = app().store.Get().value("jog_speed").toObject();
		for (int a = 0; a < AXIS_COUNT; a++)
			if (js.contains(AXIS_NAMES[a]))
				app().cam.SetJogSpeed(a, js.value(AXIS_NAMES[a]).toDouble());
		if (reconnect) {
			std::string dev = app().store.CameraName();
			app().cam.Reconnect(&dev);
		}
		Ok(res);
	}));

	s.Post("/api/camera/reconnect", Route([](const Request &, Response &res) {
		Ok(res, QJsonObject{{"connected", app().cam.Reconnect(nullptr)}});
	}));
	s.Get("/api/camera/devices", Route([](const Request &, Response &res) {
		Ok(res, QJsonObject{{"devices", ToArray(app().cam.ListDevices())}});
	}));

	/* --- source OBS --- */
	s.Get("/api/obs/sources", Route([](const Request &, Response &res) {
		std::string current;
		try {
			current = ResolveSource();
		} catch (...) {
		}
		Ok(res, QJsonObject{{"sources", ToArray(ListCameraSources())},
				    {"current", QString::fromStdString(current)},
				    {"chosen", QString::fromStdString(app().store.SourceName())}});
	}));
	s.Get("/api/obs/preview.jpg", Route([](const Request &req, Response &res) {
		int w = req.has_param("w") ? std::stoi(req.get_param_value("w")) : 640;
		int q = req.has_param("q") ? std::stoi(req.get_param_value("q")) : 70;
		QByteArray jpg = SourcePreviewJpeg(std::max(160, std::min(1920, w)), q);
		res.set_header("Cache-Control", "no-store");
		res.set_content(jpg.toStdString(), "image/jpeg");
	}));
	s.Get("/api/obs/settings", Route([](const Request &, Response &res) { Ok(res, GetSourceSettings()); }));
	s.Post("/api/obs/settings", Route([](const Request &req, Response &res) {
		Ok(res, QJsonObject{{"applied", SetSourceSettings(Body(req))}});
	}));
}

/* ---------------------------------------------------------------------- */
bool StartServer(int preferredPort)
{
	server = std::make_unique<httplib::Server>();
	SetupRoutes(*server);
	int port = 0;
	for (int p = preferredPort; p < preferredPort + 10; p++) {
		if (server->bind_to_port("127.0.0.1", p)) {
			port = p;
			break;
		}
	}
	if (!port) {
		obs_log(LOG_ERROR, "could not open a local port (%d-%d)", preferredPort, preferredPort + 9);
		server.reset();
		return false;
	}
	app().port = port;
	serverThread = std::thread([]() { server->listen_after_bind(); });
	obs_log(LOG_INFO, "panel available at http://127.0.0.1:%d", port);
	return true;
}

void StopServer()
{
	if (server)
		server->stop();
	if (serverThread.joinable())
		serverThread.join();
	server.reset();
}
