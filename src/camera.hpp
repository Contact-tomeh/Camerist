/*
Camerist - camera control for OBS Studio
Copyright (C) 2026 tomi_tomeh

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum Axis { AXIS_PAN = 0, AXIS_TILT = 1, AXIS_ROLL = 2, AXIS_ZOOM = 3, AXIS_COUNT = 4 };

extern const char *const AXIS_NAMES[AXIS_COUNT];
int AxisFromName(const std::string &name); // -1 if unknown

struct AxisRange {
	long min = 0;
	long max = 0;
	long step = 1;
	long def = 0;
};

/* Accès bas niveau à une caméra (DirectShow sous Windows). */
class CameraBackend {
public:
	virtual ~CameraBackend() = default;
	virtual std::vector<std::string> ListDevices() = 0;
	/* Ouvre la caméra dont le nom contient `wanted` (ou une DJI par défaut).
	   Lève std::runtime_error si rien n'est trouvé. */
	virtual std::string Open(const std::string &wanted) = 0;
	virtual bool GetRange(int axis, AxisRange &range) = 0;
	virtual bool Get(int axis, long &value) = 0;
	virtual bool Set(int axis, long value) = 0;
	virtual void Close() = 0;
};

/* Fourni par camera-dshow.cpp (Windows) ou camera-stub.cpp (ailleurs). */
std::unique_ptr<CameraBackend> CreateCameraBackend();
void CameraThreadInit();
void CameraThreadUninit();

struct CameraStatus {
	bool connected = false;
	bool controllable = false;
	std::string name;
	std::string error;
	std::array<bool, AXIS_COUNT> has{};
	std::array<AxisRange, AXIS_COUNT> ranges{};
	std::array<double, AXIS_COUNT> pos{};
	bool moving = false;
};

/* Un seul thread possède la caméra. Les autres lui envoient des ordres. */
class CameraWorker {
public:
	CameraWorker();
	~CameraWorker();

	void Start(const std::string &device);
	void Stop();

	CameraStatus Status();

	/* target : axe -> valeur ; duration en secondes (0 = direct) */
	void GoTo(const std::map<int, double> &target, double duration);
	double Nudge(int axis, double delta);
	void JogStart(int axis, double direction);
	void JogStop(int axis); // -1 = tous
	void Home(double duration);
	bool Reconnect(const std::string *device);
	std::vector<std::string> ListDevices();
	void SetJogSpeed(int axis, double unitsPerSecond);

private:
	void Run();
	void Call(std::function<void()> fn);
	void Require();
	double Clamp(int axis, double v) const;
	void TryConnect(bool force);
	void Disconnect(const std::string &err);
	void Push();
	void Tick();

	struct Motion {
		std::array<double, AXIS_COUNT> start{};
		std::map<int, double> target;
		double t0 = 0;
		double duration = 0;
	};
	struct Jog {
		double dir = 0;
		double expires = 0;
	};

	std::thread thread;
	std::mutex qMutex;
	std::condition_variable qCond;
	std::deque<std::function<void()>> queue;
	std::atomic<bool> running{false};

	mutable std::mutex stateMutex;
	std::unique_ptr<CameraBackend> backend;
	std::string device;
	bool connected = false;
	std::string name;
	std::string error;
	std::array<bool, AXIS_COUNT> has{};
	std::array<AxisRange, AXIS_COUNT> ranges{};
	std::array<double, AXIS_COUNT> pos{};
	std::array<long, AXIS_COUNT> sent{};
	std::array<bool, AXIS_COUNT> sentValid{};
	std::array<double, AXIS_COUNT> jogSpeed{40, 40, 20, 120};
	std::unique_ptr<Motion> motion;
	std::map<int, Jog> jogs;
	double lastTry = -100;
	double lastTick = 0;
};
