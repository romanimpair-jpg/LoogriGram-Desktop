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

} // namespace Core::LoogriGram
