/*
Camerist - camera control for OBS Studio
Copyright (C) 2026 tomi_tomeh

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "camera.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

const char *const AXIS_NAMES[AXIS_COUNT] = {"pan", "tilt", "roll", "zoom"};

int AxisFromName(const std::string &name)
{
	for (int i = 0; i < AXIS_COUNT; i++)
		if (name == AXIS_NAMES[i])
			return i;
	return -1;
}

static double Now()
{
	using namespace std::chrono;
	return duration<double>(steady_clock::now().time_since_epoch()).count();
}

/* Profil de vitesse « trapèze » : courte accélération, vitesse constante,
   courte décélération. Avec une courbe en S classique, le début et la fin
   sont si lents que la nacelle avance par petits sauts de 1° bien visibles ;
   ici la vitesse reste régulière presque tout le trajet. */
static double Ease(double u)
{
	const double r = 0.2;             // 20 % du temps pour accélérer, 20 % pour freiner
	const double v = 1.0 / (1.0 - r); // vitesse de croisière
	if (u <= 0)
		return 0;
	if (u >= 1)
		return 1;
	if (u < r)
		return 0.5 * v * u * u / r;
	if (u < 1 - r)
		return v * (u - r / 2);
	double w = 1 - u;
	return 1 - 0.5 * v * w * w / r;
}

static constexpr double TICK = 0.025; // 40 envois par seconde au maximum

CameraWorker::CameraWorker() {}

CameraWorker::~CameraWorker()
{
	Stop();
}

void CameraWorker::Start(const std::string &dev)
{
	if (running)
		return;
	device = dev;
	running = true;
	thread = std::thread(&CameraWorker::Run, this);
}

void CameraWorker::Stop()
{
	if (!running)
		return;
	running = false;
	qCond.notify_all();
	if (thread.joinable())
		thread.join();
}

void CameraWorker::Call(std::function<void()> fn)
{
	if (!running)
		throw std::runtime_error("camera thread stopped");
	auto task = std::make_shared<std::packaged_task<void()>>(std::move(fn));
	std::future<void> fut = task->get_future();
	{
		std::lock_guard<std::mutex> lock(qMutex);
		queue.emplace_back([task]() { (*task)(); });
	}
	qCond.notify_one();
	if (fut.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
		throw std::runtime_error("camera not responding");
	fut.get(); // relance l'exception éventuelle
}

CameraStatus CameraWorker::Status()
{
	std::lock_guard<std::mutex> lock(stateMutex);
	CameraStatus s;
	s.connected = connected;
	s.name = connected ? name : std::string();
	s.error = error;
	s.has = has;
	s.ranges = ranges;
	s.pos = pos;
	s.moving = motion != nullptr || !jogs.empty();
	s.controllable = connected && std::any_of(has.begin(), has.end(), [](bool b) { return b; });
	return s;
}

void CameraWorker::Require()
{
	if (!connected)
		throw std::runtime_error(error.empty() ? "camera not connected" : error);
}

double CameraWorker::Clamp(int axis, double v) const
{
	if (!has[axis])
		return v;
	return std::max<double>((double)ranges[axis].min, std::min<double>((double)ranges[axis].max, v));
}

void CameraWorker::GoTo(const std::map<int, double> &target, double duration)
{
	Call([this, target, duration]() {
		Require();
		std::lock_guard<std::mutex> lock(stateMutex);
		std::map<int, double> tgt;
		for (auto &kv : target)
			if (kv.first >= 0 && kv.first < AXIS_COUNT && has[kv.first])
				tgt[kv.first] = Clamp(kv.first, kv.second);
		jogs.clear();
		if (duration <= 0) {
			motion.reset();
			for (auto &kv : tgt)
				pos[kv.first] = kv.second;
			Push();
		} else {
			motion = std::make_unique<Motion>();
			motion->start = pos;
			motion->target = tgt;
			motion->t0 = Now();
			motion->duration = duration;
		}
	});
}

double CameraWorker::Nudge(int axis, double delta)
{
	double result = 0;
	Call([&]() {
		Require();
		std::lock_guard<std::mutex> lock(stateMutex);
		if (!has[axis])
			throw std::runtime_error("axis not available");
		motion.reset();
		pos[axis] = Clamp(axis, pos[axis] + delta);
		Push();
		result = pos[axis];
	});
	return result;
}

void CameraWorker::JogStart(int axis, double direction)
{
	Call([this, axis, direction]() {
		Require();
		std::lock_guard<std::mutex> lock(stateMutex);
		if (!has[axis])
			return;
		motion.reset();
		jogs[axis] = Jog{direction, Now() + 0.45};
	});
}

void CameraWorker::JogStop(int axis)
{
	Call([this, axis]() {
		std::lock_guard<std::mutex> lock(stateMutex);
		if (axis < 0)
			jogs.clear();
		else
			jogs.erase(axis);
	});
}

void CameraWorker::Home(double duration)
{
	std::map<int, double> target;
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		for (int a = 0; a < AXIS_COUNT; a++)
			if (has[a])
				target[a] = (double)ranges[a].def;
	}
	GoTo(target, duration);
}

bool CameraWorker::Reconnect(const std::string *dev)
{
	bool ok = false;
	Call([&]() {
		if (dev)
			device = *dev;
		Disconnect("");
		TryConnect(true);
		ok = connected;
	});
	return ok;
}

std::vector<std::string> CameraWorker::ListDevices()
{
	std::vector<std::string> list;
	Call([&]() {
		if (!backend)
			backend = CreateCameraBackend();
		list = backend->ListDevices();
	});
	return list;
}

void CameraWorker::SetJogSpeed(int axis, double v)
{
	std::lock_guard<std::mutex> lock(stateMutex);
	if (axis >= 0 && axis < AXIS_COUNT && v > 0)
		jogSpeed[axis] = v;
}

void CameraWorker::Disconnect(const std::string &err)
{
	std::lock_guard<std::mutex> lock(stateMutex);
	connected = false;
	error = err;
	motion.reset();
	jogs.clear();
	sentValid.fill(false);
	if (backend)
		backend->Close();
}

void CameraWorker::TryConnect(bool force)
{
	double now = Now();
	if (!force && now - lastTry < 3)
		return;
	lastTry = now;
	try {
		if (!backend)
			backend = CreateCameraBackend();
		std::string n = backend->Open(device);
		std::array<bool, AXIS_COUNT> h{};
		std::array<AxisRange, AXIS_COUNT> r{};
		std::array<double, AXIS_COUNT> p{};
		for (int a = 0; a < AXIS_COUNT; a++) {
			long v = 0;
			if (backend->GetRange(a, r[a]) && r[a].max > r[a].min) {
				h[a] = true;
				if (r[a].step <= 0)
					r[a].step = 1;
				p[a] = backend->Get(a, v) ? (double)v : (double)r[a].def;
			}
		}
		std::lock_guard<std::mutex> lock(stateMutex);
		name = n;
		has = h;
		ranges = r;
		pos = p;
		for (int a = 0; a < AXIS_COUNT; a++) {
			sent[a] = std::lround(p[a]);
			sentValid[a] = h[a];
		}
		connected = true;
		error.clear();
	} catch (const std::exception &e) {
		std::lock_guard<std::mutex> lock(stateMutex);
		connected = false;
		error = e.what();
	}
}

/* À appeler avec stateMutex verrouillé. */
void CameraWorker::Push()
{
	for (int a = 0; a < AXIS_COUNT; a++) {
		if (!has[a])
			continue;
		long v = std::lround(pos[a]);
		if (sentValid[a] && sent[a] == v)
			continue;
		if (!backend->Set(a, v)) {
			connected = false;
			error = "camera lost";
			motion.reset();
			jogs.clear();
			return;
		}
		sent[a] = v;
		sentValid[a] = true;
	}
}

void CameraWorker::Tick()
{
	std::lock_guard<std::mutex> lock(stateMutex);
	double now = Now();
	bool finishing = motion && now - motion->t0 >= motion->duration;
	double dt = std::min(0.2, now - lastTick);
	if (dt < TICK * 0.8 && !finishing)
		return;
	lastTick = now;
	bool changed = false;
	if (motion) {
		double t = std::min(1.0, (now - motion->t0) / motion->duration);
		double k = Ease(t);
		for (auto &kv : motion->target)
			pos[kv.first] = motion->start[kv.first] + (kv.second - motion->start[kv.first]) * k;
		changed = true;
		if (t >= 1.0)
			motion.reset();
	}
	for (auto it = jogs.begin(); it != jogs.end();) {
		if (now > it->second.expires) {
			it = jogs.erase(it);
			continue;
		}
		int a = it->first;
		pos[a] = Clamp(a, pos[a] + it->second.dir * jogSpeed[a] * dt);
		changed = true;
		++it;
	}
	if (changed)
		Push();
}

void CameraWorker::Run()
{
	CameraThreadInit();
	while (running) {
		if (!connected)
			TryConnect(false);
		bool busy;
		{
			std::lock_guard<std::mutex> lock(stateMutex);
			busy = motion != nullptr || !jogs.empty();
		}
		std::function<void()> fn;
		{
			std::unique_lock<std::mutex> lock(qMutex);
			qCond.wait_for(lock, std::chrono::milliseconds(busy ? 25 : 250),
				       [this]() { return !queue.empty() || !running; });
			if (!queue.empty()) {
				fn = std::move(queue.front());
				queue.pop_front();
			}
		}
		if (fn)
			fn();
		if (connected)
			Tick();
	}
	/* vide la file pour ne pas bloquer un appelant */
	std::deque<std::function<void()>> rest;
	{
		std::lock_guard<std::mutex> lock(qMutex);
		rest.swap(queue);
	}
	for (auto &fn : rest)
		fn();
	if (backend)
		backend->Close();
	backend.reset();
	CameraThreadUninit();
}
