/*
Camerist - camera control for OBS Studio
Copyright (C) 2026 tomi_tomeh

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/* Tout ce qui parle directement à OBS : source caméra, réglages, aperçu, scènes. */

#include "app.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <graphics/vec4.h>

#include <QBuffer>
#include <QImage>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <stdexcept>

static const char *DSHOW_ID = "dshow_input";

static std::string Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

std::string Text(const char *key)
{
	return obs_module_text(key);
}

std::string ObsLocale()
{
	const char *loc = obs_get_locale();
	return loc ? loc : "en-US";
}

/* ---------------------------------------------------------------------- */
/* Sources caméra                                                          */
/* ---------------------------------------------------------------------- */
struct SourceInfo {
	std::string name;
	std::string device;
};

static std::vector<SourceInfo> CameraSources()
{
	std::vector<SourceInfo> out;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			auto *list = static_cast<std::vector<SourceInfo> *>(param);
			const char *id = obs_source_get_unversioned_id(source);
			if (id && strcmp(id, DSHOW_ID) == 0) {
				obs_data_t *s = obs_source_get_settings(source);
				list->push_back({obs_source_get_name(source), obs_data_get_string(s, "video_device_id")});
				obs_data_release(s);
			}
			return true;
		},
		&out);
	return out;
}

std::vector<std::string> ListCameraSources()
{
	std::vector<std::string> names;
	for (auto &s : CameraSources())
		names.push_back(s.name);
	return names;
}

std::string ResolveSource()
{
	std::string chosen = app().store.SourceName();
	auto sources = CameraSources();
	if (!chosen.empty()) {
		for (auto &s : sources)
			if (s.name == chosen)
				return chosen;
	}
	if (sources.empty())
		throw std::runtime_error(Text("Error.NoSource"));
	/* 1. la source qui utilise la caméra pilotée */
	std::string cam = Lower(app().cam.Status().name);
	if (!cam.empty())
		for (auto &s : sources)
			if (Lower(s.device).find(cam) != std::string::npos)
				return s.name;
	/* 2. une source dont le périphérique ou le nom ressemble à une DJI */
	for (auto &s : sources) {
		std::string d = Lower(s.device + " " + s.name);
		if (d.find("osmo") != std::string::npos || d.find("pocket") != std::string::npos ||
		    d.find("dji") != std::string::npos)
			return s.name;
	}
	return sources.front().name;
}

struct SourceRef {
	obs_source_t *src;
	explicit SourceRef(const std::string &name) : src(obs_get_source_by_name(name.c_str()))
	{
		if (!src)
			throw std::runtime_error(Text("Error.NoSource"));
	}
	~SourceRef() { obs_source_release(src); }
};

/* ---------------------------------------------------------------------- */
/* Réglages de la source                                                   */
/* ---------------------------------------------------------------------- */
static const char *LIST_PROPS[] = {"res_type", "resolution", "frame_interval", "video_format",
				   "color_space", "color_range", "buffering"};
static const char *BOOL_PROPS[] = {"flip_vertically", "autorotation", "hw_decode", "deactivate_when_not_showing"};

static QJsonValue ReadValue(obs_data_t *s, obs_property_t *p, const char *key)
{
	if (p && obs_property_get_type(p) == OBS_PROPERTY_LIST) {
		switch (obs_property_list_format(p)) {
		case OBS_COMBO_FORMAT_INT:
			return QJsonValue((qint64)obs_data_get_int(s, key));
		case OBS_COMBO_FORMAT_FLOAT:
			return QJsonValue(obs_data_get_double(s, key));
		default:
			return QJsonValue(QString::fromUtf8(obs_data_get_string(s, key)));
		}
	}
	return QJsonValue(QString::fromUtf8(obs_data_get_string(s, key)));
}

QJsonObject GetSourceSettings()
{
	std::string name = ResolveSource();
	SourceRef ref(name);
	obs_data_t *settings = obs_source_get_settings(ref.src);
	obs_properties_t *props = obs_source_properties(ref.src);
	if (props)
		obs_properties_apply_settings(props, settings);

	QJsonObject values, options;
	for (const char *key : LIST_PROPS) {
		obs_property_t *p = props ? obs_properties_get(props, key) : nullptr;
		values[key] = ReadValue(settings, p, key);
		QJsonArray items;
		if (p && obs_property_get_type(p) == OBS_PROPERTY_LIST) {
			enum obs_combo_format fmt = obs_property_list_format(p);
			size_t count = obs_property_list_item_count(p);
			for (size_t i = 0; i < count; i++) {
				QJsonObject it;
				it["label"] = QString::fromUtf8(obs_property_list_item_name(p, i));
				if (fmt == OBS_COMBO_FORMAT_INT)
					it["value"] = (qint64)obs_property_list_item_int(p, i);
				else if (fmt == OBS_COMBO_FORMAT_FLOAT)
					it["value"] = obs_property_list_item_float(p, i);
				else
					it["value"] = QString::fromUtf8(obs_property_list_item_string(p, i));
				it["enabled"] = !obs_property_list_item_disabled(p, i);
				items.append(it);
			}
		}
		options[key] = items;
		/* libellé d'OBS, déjà traduit dans la langue d'OBS */
		if (p)
			options[QString(key) + "_label"] = QString::fromUtf8(obs_property_description(p));
	}
	QJsonObject labels;
	for (const char *key : BOOL_PROPS) {
		values[key] = obs_data_get_bool(settings, key);
		obs_property_t *p = props ? obs_properties_get(props, key) : nullptr;
		if (p)
			labels[key] = QString::fromUtf8(obs_property_description(p));
	}
	for (const char *key : LIST_PROPS) {
		QString lk = QString(key) + "_label";
		if (options.contains(lk)) {
			labels[key] = options.value(lk);
			options.remove(lk);
		}
	}

	obs_properties_destroy(props);
	obs_data_release(settings);
	return QJsonObject{{"source", QString::fromStdString(name)},
			   {"values", values},
			   {"options", options},
			   {"labels", labels}};
}

QJsonObject SetSourceSettings(const QJsonObject &changes)
{
	std::string name = ResolveSource();
	SourceRef ref(name);
	obs_data_t *cur = obs_source_get_settings(ref.src);
	obs_properties_t *props = obs_source_properties(ref.src);
	obs_data_t *upd = obs_data_create();
	QJsonObject applied;

	auto setList = [&](const char *key, const QJsonValue &v) {
		obs_property_t *p = props ? obs_properties_get(props, key) : nullptr;
		enum obs_combo_format fmt = p ? obs_property_list_format(p) : OBS_COMBO_FORMAT_STRING;
		if (fmt == OBS_COMBO_FORMAT_INT)
			obs_data_set_int(upd, key, (long long)v.toDouble());
		else if (fmt == OBS_COMBO_FORMAT_FLOAT)
			obs_data_set_double(upd, key, v.toDouble());
		else
			obs_data_set_string(upd, key, v.toString().toUtf8().constData());
		applied[key] = v;
	};
	for (const char *key : LIST_PROPS)
		if (changes.contains(key))
			setList(key, changes.value(key));
	for (const char *key : BOOL_PROPS)
		if (changes.contains(key)) {
			obs_data_set_bool(upd, key, changes.value(key).toBool());
			applied[key] = changes.value(key);
		}
	/* choisir une résolution, des FPS ou un format impose le mode « Personnalisée » */
	if ((changes.contains("resolution") || changes.contains("frame_interval") || changes.contains("video_format")) &&
	    !changes.contains("res_type")) {
		obs_data_set_int(upd, "res_type", 1);
		applied["res_type"] = 1;
	}
	obs_source_update(ref.src, upd);
	obs_data_release(upd);
	obs_properties_destroy(props);
	obs_data_release(cur);
	return applied;
}

/* ---------------------------------------------------------------------- */
/* Aperçu : rendu de la source dans une texture, puis JPEG                 */
/* (même méthode que la capture d'image d'obs-websocket)                   */
/* ---------------------------------------------------------------------- */
static std::mutex previewMutex;

QByteArray SourcePreviewJpeg(int width, int quality)
{
	std::lock_guard<std::mutex> lock(previewMutex);
	SourceRef ref(ResolveSource());
	uint32_t sw = obs_source_get_width(ref.src);
	uint32_t sh = obs_source_get_height(ref.src);
	if (!sw || !sh)
		throw std::runtime_error(Text("Error.NoImage"));
	uint32_t w = (uint32_t)std::max(16, std::min(width, (int)sw));
	uint32_t h = std::max<uint32_t>(16, (uint32_t)((double)w * sh / sw));

	QImage img(w, h, QImage::Format_RGBA8888);
	img.fill(0);
	bool ok = false;

	obs_enter_graphics();
	gs_texrender_t *tex = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *stage = gs_stagesurface_create(w, h, GS_RGBA);
	gs_texrender_reset(tex);
	if (gs_texrender_begin(tex, w, h)) {
		vec4 background;
		vec4_zero(&background);
		gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
		gs_ortho(0.0f, (float)sw, 0.0f, (float)sh, -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_source_inc_showing(ref.src);
		obs_source_video_render(ref.src);
		obs_source_dec_showing(ref.src);
		gs_blend_state_pop();
		gs_texrender_end(tex);
		gs_stage_texture(stage, gs_texrender_get_texture(tex));
		uint8_t *data = nullptr;
		uint32_t linesize = 0;
		if (gs_stagesurface_map(stage, &data, &linesize)) {
			int bpl = img.bytesPerLine();
			for (uint32_t y = 0; y < h; y++)
				memcpy(img.scanLine((int)y), data + y * linesize, (size_t)bpl);
			gs_stagesurface_unmap(stage);
			ok = true;
		}
	}
	gs_stagesurface_destroy(stage);
	gs_texrender_destroy(tex);
	obs_leave_graphics();

	if (!ok)
		throw std::runtime_error(Text("Error.NoImage"));
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	img.convertToFormat(QImage::Format_RGB888).save(&buffer, "JPG", std::max(30, std::min(95, quality)));
	return bytes;
}

/* ---------------------------------------------------------------------- */
/* Scènes                                                                  */
/* ---------------------------------------------------------------------- */
std::vector<std::string> ListScenes()
{
	std::vector<std::string> out;
	char **names = obs_frontend_get_scene_names();
	if (names) {
		for (char **n = names; *n; n++)
			out.emplace_back(*n);
		bfree(names);
	}
	return out;
}
