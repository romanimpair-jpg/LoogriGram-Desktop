/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// LoogriGram: the release tag this binary was built from.
//
// CI rewrites this file with the real tag just before configuring, so the
// value committed here is only ever what a local build gets. The updater
// compares it against the newest release tag on our own GitHub and treats any
// difference as "there is a newer build" - a plain string comparison, never
// version arithmetic, so it cannot drift against upstream's AppVersion.
//
// "dev" never matches a published tag, which is deliberate: a locally built
// binary would see an update available on the first check. That is why the
// updater refuses to run unless the tag looks like a real one.
#define LOOGRIGRAM_BUILD_TAG "dev"
