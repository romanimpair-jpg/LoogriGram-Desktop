/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace LoogriGram::Lang {

// LoogriGram: strings this fork adds, kept out of Telegram's lang.strings.
//
// That file feeds a code generator whose key indices are positional, so
// inserting one string renumbers every key after it. Combined with the
// incremental build tree that produced a binary whose translation units
// disagreed about the key table: it asserted on "key < _values.size()" when
// opening the main menu, and silently returned off-by-one strings elsewhere.
// Only five objects had been rebuilt, and lang_instance.cpp - which sizes the
// value array from kKeysCount - was not one of them.
//
// Keeping our strings here means adding or removing one touches this file and
// nothing else: no code generation, no renumbering, no invalidated objects.
// It also keeps lang.strings pristine against upstream, which makes rebasing
// cleaner. Add a new string by adding an accessor; that is the whole process.
//
// Contact with Telegram's lang system is read-only - the current language id
// and a signal when it changes - so these follow a language switch the same
// way tr:: strings do.

[[nodiscard]] rpl::producer<QString> GhostMode();

// The four faces of the main menu's update row, one per updater state.
[[nodiscard]] rpl::producer<QString> CheckUpdates();
[[nodiscard]] rpl::producer<QString> CheckingUpdates();
[[nodiscard]] rpl::producer<QString> RestartToUpdate();

// Reads "Downloading Update 42%", or drops the number for as long as the
// download has no known total to measure itself against - which is why this
// takes the percentage as a producer rather than a value, and why a negative
// one is the way to say "not yet known" instead of pretending it is 0%.
[[nodiscard]] rpl::producer<QString> DownloadingUpdate(
	rpl::producer<int> percent);

// The toast after this week's free voice transcriptions are used up.
// Telegram's text for it ends by suggesting a subscription; this is the same
// sentence without that ending. The date is passed already formatted, so the
// caller decides how it is emphasised.
[[nodiscard]] TextWithEntities TranscribeTrialsOver(TextWithEntities date);

// Why a user who charges Stars per message is locked: nothing here pays.
// The name is passed in, so the caller decides how it is emphasised.
[[nodiscard]] TextWithEntities PaidMessagesLocked(TextWithEntities user);

// Keys whose compiled text must survive the cloud language pack.
//
// Editing lang.strings changes only the compiled default. On startup the
// cached cloud pack is replayed over it - ten thousand keys of Telegram's own
// English - and Lang::Instance::applyValue overwrites every value it carries.
// So renaming the app inside lang.strings looks right in the source and has no
// effect at all at runtime: the tray still said "Quit Telegram".
//
// These are the keys that name this program. Their cloud value is ignored so
// the compiled one stands. Everything else still follows Telegram's pack,
// including strings that mention Telegram the service, which are not ours to
// rewrite.
[[nodiscard]] bool KeepCompiledString(const QByteArray &key);

} // namespace LoogriGram::Lang
