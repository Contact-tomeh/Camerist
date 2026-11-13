/*
Camerist - camera control for OBS Studio
Copyright (C) 2026 tomi_tomeh

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "camera.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>

#include <mutex>
#include <string>
#include <vector>

/* ---------------------------------------------------------------------- */
/* Réglages de l'appli et positions, enregistrés dans camerist.json        */
/* (dans le dossier de configuration des plugins d'OBS).                   */
/* ---------------------------------------------------------------------- */
class Store {
public:
	void Load(const std::string &path);
	void Save();

	QJsonObject Get(); // copie complète
	template<typename F> void Edit(F fn)
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			fn(data);
		}
		Save();
	}

	/* raccourcis pratiques */
	double MoveDuration();
	std::string CameraName();
	std::string SourceName();
	int Port();
	QJsonArray Presets();
	QJsonObject FindPreset(const std::string &key); // id, numéro (1..n) ou nom ; vide si absent
	std::string PresetForScene(const std::string &scene);

private:
	std::mutex mutex;
	std::string path;
	QJsonObject data;
};

/* ---------------------------------------------------------------------- */
/* État global du plugin                                                   */
/* ---------------------------------------------------------------------- */
struct App {
	CameraWorker cam;
	Store store;
	int port = 0; // port réellement utilisé par le serveur local
};

App &app();

/* Actions partagées (panneau, Stream Deck, raccourcis, scènes) */
QJsonObject GoPreset(const std::string &key, double duration = -1);
void DoHome(double duration = -1);
void DoFlip(double duration = -1);
void DoZoom(int direction);

/* Raccourcis OBS (plugin-main.cpp) */
void SyncPresetHotkeys();
std::string HotkeyText(const std::string &key);
bool SetHotkey(const std::string &key, const QJsonObject &combo); // combo vide = effacer
void SaveHotkeys();

/* Pont avec OBS (obs-bridge.cpp) */
std::vector<std::string> ListCameraSources();
std::string ResolveSource(); // nom de la source caméra utilisée
QJsonObject GetSourceSettings();
QJsonObject SetSourceSettings(const QJsonObject &changes);
QByteArray SourcePreviewJpeg(int width, int quality);
std::vector<std::string> ListScenes();
std::string ObsLocale();
std::string Text(const char *key); // texte traduit (fichiers data/locale)

/* Serveur local (server.cpp) */
bool StartServer(int preferredPort);
void StopServer();
