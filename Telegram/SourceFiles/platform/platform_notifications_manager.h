/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/notifications_manager.h"

namespace Platform {
namespace Notifications {

[[nodiscard]] bool SkipToastForCustom();
void MaybePlaySoundForCustom(Fn<void()> playSound);
void MaybeFlashBounceForCustom(Fn<void()> flashBounce);
[[nodiscard]] bool WaitForInputForCustom();

[[nodiscard]] bool Supported();
[[nodiscard]] bool Enforced();
[[nodiscard]] bool ByDefault();
[[nodiscard]] bool VolumeSupported();
void Create(Window::Notifications::System *system);

} // namespace Notifications
} // namespace Platform

// Platform dependent implementations.

#include "platform/win/notifications_manager_win.h"
