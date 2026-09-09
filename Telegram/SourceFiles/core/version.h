/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/const_string.h"

#define TDESKTOP_REQUESTED_ALPHA_VERSION (0ULL)

#ifdef TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION TDESKTOP_REQUESTED_ALPHA_VERSION
#else // TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION (0ULL)
#endif // TDESKTOP_ALLOW_CLOSED_ALPHA

// LoogriGram: AppName is what psAppDataPath() appends to %APPDATA%, so
// renaming it moves this build's whole profile to %APPDATA%\LoogriGram and
// keeps it away from an official Telegram install's data. AppId is the GUID
// Windows uses for registry and uninstall entries, so it must not be the
// official one either. The API Terms also require that the app title not
// contain "Telegram".
//
// used in Updater.cpp and Setup.iss for Windows
constexpr auto AppId = "{1E5B966E-EC7F-44D7-8DCA-ED965D938926}"_cs;
constexpr auto AppNameOld = "Telegram Win (Unofficial)"_cs;
constexpr auto AppName = "LoogriGram"_cs;
constexpr auto AppFile = "LoogriGram"_cs;
constexpr auto AppVersion = 7002006;
constexpr auto AppVersionStr = "7.2.6";
constexpr auto AppBetaVersion = true;
constexpr auto AppAlphaVersion = TDESKTOP_ALPHA_VERSION;
