/*
Camerist - camera control for OBS Studio
Copyright (C) 2026 tomi_tomeh

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/* Pilotage des caméras USB (UVC) sous Windows via DirectShow.
   C'est le même chemin que l'onglet « Contrôle de la caméra » de
   « Configurer la vidéo » dans OBS : on ne touche pas au flux vidéo. */

#ifdef _WIN32

#include "camera.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dshow.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>

static std::string Utf8(const wchar_t *w)
{
	if (!w)
		return std::string();
	int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 1)
		return std::string();
	std::string out((size_t)len - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], len, nullptr, nullptr);
	return out;
}

static std::string Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

template<typename T> static void SafeRelease(T *&p)
{
	if (p) {
		p->Release();
		p = nullptr;
	}
}

class DShowBackend : public CameraBackend {
public:
	~DShowBackend() override { Close(); }

	std::vector<std::string> ListDevices() override
	{
		std::vector<std::string> names;
		Enumerate([&](const std::string &name, IMoniker *) {
			names.push_back(name);
			return false;
		});
		return names;
	}

	std::string Open(const std::string &wanted) override
	{
		Close();
		std::string want = Lower(wanted);
		std::string chosen;
		std::vector<std::string> seen;
		bool found = Enumerate([&](const std::string &name, IMoniker *moniker) {
			seen.push_back(name);
			std::string low = Lower(name);
			bool match = want.empty() ? (low.find("osmo") != std::string::npos ||
						     low.find("pocket") != std::string::npos ||
						     low.find("dji") != std::string::npos)
						  : low.find(want) != std::string::npos;
			if (!match)
				return false;
			if (FAILED(moniker->BindToObject(nullptr, nullptr, IID_IBaseFilter, (void **)&filter)))
				return false;
			/* Une webcam fixe n'a pas cette interface : on la garde quand même. */
			if (FAILED(filter->QueryInterface(IID_IAMCameraControl, (void **)&control)))
				control = nullptr;
			chosen = name;
			return true;
		});
		if (!found) {
			std::string list;
			for (auto &n : seen)
				list += (list.empty() ? "" : ", ") + n;
			throw std::runtime_error("camera not found (seen by Windows: " +
						 (list.empty() ? std::string("none") : list) + ")");
		}
		return chosen;
	}

	bool GetRange(int axis, AxisRange &r) override
	{
		if (!control)
			return false;
		long mn, mx, st, df, fl;
		if (FAILED(control->GetRange(axis, &mn, &mx, &st, &df, &fl)))
			return false;
		r.min = mn;
		r.max = mx;
		r.step = st;
		r.def = df;
		return true;
	}

	bool Get(int axis, long &value) override
	{
		if (!control)
			return false;
		long flags = 0;
		return SUCCEEDED(control->Get(axis, &value, &flags));
	}

	bool Set(int axis, long value) override
	{
		if (!control)
			return false;
		return SUCCEEDED(control->Set(axis, value, CameraControl_Flags_Manual));
	}

	void Close() override
	{
		SafeRelease(control);
		SafeRelease(filter);
	}

private:
	/* fn(nom, moniker) renvoie true pour s'arrêter. */
	template<typename F> bool Enumerate(F fn)
	{
		ICreateDevEnum *devEnum = nullptr;
		IEnumMoniker *enumMoniker = nullptr;
		bool stopped = false;
		if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum,
					    (void **)&devEnum)))
			return false;
		if (devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumMoniker, 0) == S_OK) {
			IMoniker *moniker = nullptr;
			while (!stopped && enumMoniker->Next(1, &moniker, nullptr) == S_OK) {
				std::string name = "?";
				IPropertyBag *bag = nullptr;
				if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, (void **)&bag))) {
					VARIANT var;
					VariantInit(&var);
					if (SUCCEEDED(bag->Read(L"FriendlyName", &var, nullptr)) && var.vt == VT_BSTR)
						name = Utf8(var.bstrVal);
					VariantClear(&var);
					bag->Release();
				}
				stopped = fn(name, moniker);
				SafeRelease(moniker);
			}
			SafeRelease(enumMoniker);
		}
		SafeRelease(devEnum);
		return stopped;
	}

	IBaseFilter *filter = nullptr;
	IAMCameraControl *control = nullptr;
};

std::unique_ptr<CameraBackend> CreateCameraBackend()
{
	return std::make_unique<DShowBackend>();
}

void CameraThreadInit()
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
}

void CameraThreadUninit()
{
	CoUninitialize();
}

#endif
