/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/loogrigram_lang.h"

#include "lang/lang_instance.h"

namespace LoogriGram::Lang {
namespace {

// One entry per string. English is required and is the fallback for every
// language without its own text, which is currently all of them - the point
// of the structure is that adding a locale later costs a column here rather
// than a rebuild of the tree.
struct Entry {
	const char *en = nullptr;
};

constexpr auto kGhostMode = Entry{ .en = "Ghost Mode" };
constexpr auto kCheckUpdates = Entry{ .en = "Check for Updates" };

[[nodiscard]] QString Pick(const Entry &entry, const QString &languageId) {
	// Nothing is translated yet, so the id is unused. When a locale is added,
	// switch on it here and fall through to entry.en for the rest.
	Expects(entry.en != nullptr);

	return QString::fromUtf8(entry.en);
}

// Re-emits when the language changes, so a row built from this updates in
// place exactly as a tr:: one does. ::Lang is Telegram's, spelled from the
// global namespace because we are inside LoogriGram::Lang.
[[nodiscard]] rpl::producer<QString> Value(const Entry &entry) {
	return rpl::single(
		::Lang::GetInstance().id()
	) | rpl::then(
		::Lang::GetInstance().idChanges()
	) | rpl::map([entry](const QString &id) {
		return Pick(entry, id);
	});
}

} // namespace

rpl::producer<QString> GhostMode() {
	return Value(kGhostMode);
}

rpl::producer<QString> CheckUpdates() {
	return Value(kCheckUpdates);
}

} // namespace LoogriGram::Lang
