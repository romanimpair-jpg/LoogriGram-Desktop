/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Core::LoogriGram {

// LoogriGram: checks our own GitHub releases for a newer build, downloads it
// and swaps it in beside the running one. Upstream's updater stays disabled -
// this talks to our repository, never to Telegram, and carries nothing about
// the account. Called once per launch; further calls are ignored.
void StartUpdateCheck();

// Same check, on demand, from the main menu. Ignores the once-per-launch
// guard and the start delay, and reports the outcome even when it is "nothing
// to do" - a button that answers nothing looks broken.
void CheckForUpdatesNow();

} // namespace Core::LoogriGram
