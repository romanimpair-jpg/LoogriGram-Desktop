/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {

class Tray;

[[nodiscard]] bool HasMonochromeSetting();

} // namespace Platform

// Platform dependent implementations.

#include "platform/win/tray_win.h"
