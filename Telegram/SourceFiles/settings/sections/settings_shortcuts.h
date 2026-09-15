/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

// LoogriGram: ShortcutsHighlightId went with the AI compose shortcut, the
// only command a search result could highlight.
[[nodiscard]] Type ShortcutsId();

} // namespace Settings
