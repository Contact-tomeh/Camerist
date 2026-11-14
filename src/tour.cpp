/*
Camerist - camera control for OBS Studio
Copyright (C) 2026 tomi_tomeh

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/* Rondes : la caméra enchaîne des positions, avec une pause sur chacune,
   une fois ou en boucle. Une seule ronde tourne à la fois. Toute commande
   manuelle (flèches, positions, raccourcis…) arrête la ronde en cours. */

#include "app.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {

std::mutex control;    // sérialise démarrage / arrêt
std::mutex stateMutex; // protège l'état ci-dessous
std::condition_variable wake;
std::thread worker;
bool stopRequested = false;
bool running = false;
std::string currentId;
std::string currentName;
int currentStep = -1;
int stepCount = 0;

void Run(QJsonObject tour)
{
	QJsonArray steps = tour.value("steps").toArray();
	bool loop = tour.value("loop").toBool(true);
	for (;;) {
		bool movedOnce = false;
		for (int i = 0; i < steps.size(); i++) {
			QJsonObject step = steps[i].toObject();
			QJsonObject preset = app().store.FindPreset(step.value("preset").toString().toStdString());
			if (preset.isEmpty())
				continue; // position supprimée entre-temps
			double travel = PresetDuration(preset);
			{
				std::lock_guard<std::mutex> lock(stateMutex);
				if (stopRequested)
					break;
				currentStep = i;
			}
			try {
				GoPresetObject(preset, travel);
			} catch (const std::exception &e) {
				obs_log(LOG_WARNING, "tour stopped: %s", e.what());
				std::lock_guard<std::mutex> lock(stateMutex);
				running = false;
				currentStep = -1;
				return;
			}
			movedOnce = true;
			double pause = std::max(0.0, step.value("pause").toDouble(3));
			auto wait = std::chrono::milliseconds((long long)((travel + pause) * 1000));
			std::unique_lock<std::mutex> lock(stateMutex);
			if (wake.wait_for(lock, wait, []() { return stopRequested; }))
				break;
		}
		std::lock_guard<std::mutex> lock(stateMutex);
		if (stopRequested || !loop || !movedOnce)
			break;
	}
	std::lock_guard<std::mutex> lock(stateMutex);
	running = false;
	currentStep = -1;
}

/* À appeler avec `control` verrouillé. */
void StopLocked()
{
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		stopRequested = true;
	}
	wake.notify_all();
	if (worker.joinable())
		worker.join();
	std::lock_guard<std::mutex> lock(stateMutex);
	running = false;
	currentStep = -1;
	stopRequested = false;
}

} // namespace

void TourStart(const std::string &key)
{
	QJsonObject tour = app().store.FindTour(key);
	if (tour.isEmpty())
		throw std::runtime_error(Text("Error.TourNotFound"));
	int usable = 0;
	for (auto v : tour.value("steps").toArray())
		if (!app().store.FindPreset(v.toObject().value("preset").toString().toStdString()).isEmpty())
			usable++;
	if (usable == 0)
		throw std::runtime_error(Text("Error.TourEmpty"));

	std::lock_guard<std::mutex> guard(control);
	StopLocked();
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		running = true;
		currentId = tour.value("id").toString().toStdString();
		currentName = tour.value("name").toString().toStdString();
		currentStep = -1;
		stepCount = tour.value("steps").toArray().size();
	}
	worker = std::thread(Run, tour);
}

void TourStop()
{
	std::lock_guard<std::mutex> guard(control);
	StopLocked();
}

bool TourToggle(const std::string &key)
{
	QJsonObject tour = app().store.FindTour(key);
	if (tour.isEmpty())
		throw std::runtime_error(Text("Error.TourNotFound"));
	bool isRunning;
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		isRunning = running && currentId == tour.value("id").toString().toStdString();
	}
	if (isRunning) {
		TourStop();
		return false;
	}
	TourStart(key);
	return true;
}

QJsonObject TourStatus()
{
	std::lock_guard<std::mutex> lock(stateMutex);
	return QJsonObject{{"running", running},
			   {"id", running ? QString::fromStdString(currentId) : QString()},
			   {"name", running ? QString::fromStdString(currentName) : QString()},
			   {"step", currentStep},
			   {"count", stepCount}};
}
