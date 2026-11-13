/*
Camerist - camera control for OBS Studio
Copyright (C) 2026 tomi_tomeh

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/* Hors Windows : pas encore de pilotage. Si la variable d'environnement
   CAMERIST_MOCK est définie, une fausse Osmo Pocket 3 est simulée pour
   tester l'interface. */

#ifndef _WIN32

#include "camera.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

class StubBackend : public CameraBackend {
public:
	std::vector<std::string> ListDevices() override
	{
		if (Mock())
			return {"OsmoPocket3 (simulated)"};
		return {};
	}

	std::string Open(const std::string &) override
	{
		if (!Mock())
			throw std::runtime_error("camera control is only available on Windows for now");
		return "OsmoPocket3 (simulated)";
	}

	bool GetRange(int axis, AxisRange &r) override
	{
		if (!Mock() || std::getenv("CAMERIST_MOCK_FIXED"))
			return false;
		r = kRanges[axis];
		return true;
	}

	bool Get(int axis, long &v) override
	{
		v = values[axis];
		return true;
	}

	bool Set(int axis, long v) override
	{
		values[axis] = std::max(kRanges[axis].min, std::min(kRanges[axis].max, v));
		return true;
	}

	void Close() override {}

private:
	static bool Mock() { return std::getenv("CAMERIST_MOCK") != nullptr; }
	static constexpr AxisRange kRanges[AXIS_COUNT] = {{-35, 215, 1, 0}, {-90, 90, 1, 0}, {-30, 30, 1, 0}, {100, 400, 1, 100}};
	long values[AXIS_COUNT] = {0, 0, 0, 100};
};

std::unique_ptr<CameraBackend> CreateCameraBackend()
{
	return std::make_unique<StubBackend>();
}

void CameraThreadInit() {}
void CameraThreadUninit() {}

#endif
