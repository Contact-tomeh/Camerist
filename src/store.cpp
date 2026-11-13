/*
Camerist - camera control for OBS Studio
Copyright (C) 2026 tomi_tomeh

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "app.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

static QJsonObject Defaults()
{
	return QJsonObject{
		{"port", 8765},
		{"camera_name", ""},
		{"source", ""},
		{"move_duration", 1.5},
		{"jog_speed", QJsonObject{{"pan", 40}, {"tilt", 40}, {"roll", 20}, {"zoom", 120}}},
		{"zoom_step", 25},
		{"nudge_step", 5},
		{"presets", QJsonArray()},
		{"scenes", QJsonObject()},
		{"ui", QJsonObject{{"theme", "auto"}, {"accent", "bleu"}}},
	};
}

static QJsonObject Merge(const QJsonObject &base, const QJsonObject &extra)
{
	QJsonObject out = base;
	for (auto it = extra.begin(); it != extra.end(); ++it) {
		if (it.value().isObject() && base.value(it.key()).isObject() && it.key() != "scenes")
			out[it.key()] = Merge(base.value(it.key()).toObject(), it.value().toObject());
		else
			out[it.key()] = it.value();
	}
	return out;
}

void Store::Load(const std::string &p)
{
	std::lock_guard<std::mutex> lock(mutex);
	path = p;
	data = Defaults();
	QFile f(QString::fromStdString(path));
	if (f.open(QIODevice::ReadOnly)) {
		QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
		if (doc.isObject())
			data = Merge(data, doc.object());
	}
}

void Store::Save()
{
	QByteArray bytes;
	std::string p;
	{
		std::lock_guard<std::mutex> lock(mutex);
		bytes = QJsonDocument(data).toJson(QJsonDocument::Indented);
		p = path;
	}
	if (p.empty())
		return;
	QSaveFile f(QString::fromStdString(p));
	if (f.open(QIODevice::WriteOnly)) {
		f.write(bytes);
		f.commit();
	}
}

QJsonObject Store::Get()
{
	std::lock_guard<std::mutex> lock(mutex);
	return data;
}

double Store::MoveDuration()
{
	std::lock_guard<std::mutex> lock(mutex);
	return data.value("move_duration").toDouble(1.5);
}

std::string Store::CameraName()
{
	std::lock_guard<std::mutex> lock(mutex);
	return data.value("camera_name").toString().toStdString();
}

std::string Store::SourceName()
{
	std::lock_guard<std::mutex> lock(mutex);
	return data.value("source").toString().toStdString();
}

int Store::Port()
{
	std::lock_guard<std::mutex> lock(mutex);
	return data.value("port").toInt(8765);
}

QJsonArray Store::Presets()
{
	std::lock_guard<std::mutex> lock(mutex);
	return data.value("presets").toArray();
}

QJsonObject Store::FindPreset(const std::string &key)
{
	QString k = QString::fromStdString(key);
	QJsonArray list = Presets();
	for (int i = 0; i < list.size(); i++) {
		QJsonObject p = list[i].toObject();
		if (p.value("id").toString() == k || QString::number(i + 1) == k ||
		    p.value("name").toString().compare(k, Qt::CaseInsensitive) == 0)
			return p;
	}
	return QJsonObject();
}

std::string Store::PresetForScene(const std::string &scene)
{
	std::lock_guard<std::mutex> lock(mutex);
	return data.value("scenes").toObject().value(QString::fromStdString(scene)).toString().toStdString();
}
