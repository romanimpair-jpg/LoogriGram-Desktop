/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Core::LoogriGram {

// LoogriGram: what the updater is doing, so the main menu row can say so.
//
// A check that finds nothing ends back at None; one that finds something runs
// Checking -> Downloading -> Ready and stays at Ready, because the new build
// is already on disk and only a restart is left.
enum class UpdateState : uchar {
	None,
	Checking,
	Downloading,
	Ready,
};

// total is 0 until the server sends a content length, which it may never do.
// Callers must handle that rather than dividing by it.
struct UpdateProgress final {
	int64 ready = 0;
	int64 total = 0;

	friend inline constexpr bool operator==(
		UpdateProgress,
		UpdateProgress) = default;
};

// LoogriGram: checks our own GitHub releases for a newer build, downloads it
// and swaps it in beside the running one. Upstream's updater stays disabled -
// this talks to our repository, never to Telegram, and carries nothing about
// the account. Called once per launch; further calls are ignored.
void StartUpdateCheck();

// Same check, on demand, from the main menu. Ignores the once-per-launch
// guard and the start delay, and reports the outcome even when it is "nothing
// to do" - a button that answers nothing looks broken.
//
// Ignored while a check or a download is already running, so leaning on the
// button cannot start a second 220MB download over the top of the first.
void CheckForUpdatesNow();

// Both start with the current value, so a main menu built midway through a
// download shows the download rather than waiting for the next change.
[[nodiscard]] rpl::producer<UpdateState> UpdateStateValue();
[[nodiscard]] rpl::producer<UpdateProgress> UpdateProgressValue();

} // namespace Core::LoogriGram
