/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/loogrigram_lang.h"

#include "lang/lang_instance.h"

#include <algorithm>
#include <array>
#include <string_view>

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

bool KeepCompiledString(const QByteArray &key) {
	// Sorted, so this is a binary search - applyValue runs once per key in the
	// cached pack, about eleven thousand times at startup.
	using namespace std::string_view_literals;
	static constexpr auto kBranded = std::array{
		"lng_bot_share_location_unavailable"sv,
		"lng_error_start_minimized_passcoded"sv,
		"lng_group_call_mac_access"sv,
		"lng_group_call_mac_screencast_access"sv,
		"lng_language_not_ready_about"sv,
		"lng_new_version_wrap"sv,
		"lng_no_mic_permission"sv,
		"lng_open_from_tray"sv,
		"lng_outdated_now"sv,
		"lng_outdated_soon"sv,
		"lng_passcode_about"sv,
		"lng_passcode_about2"sv,
		"lng_passcode_about3"sv,
		"lng_proxy_unsupported"sv,
		"lng_proxy_web_fallback"sv,
		"lng_quit_from_tray"sv,
		"lng_settings_auto_start_disabled_uwp"sv,
		"lng_settings_passkeys_unsigned_error"sv,
		"lng_sure_save_language"sv,
		"lng_terms_delete_warning"sv,
		"lng_theme_no_desktop"sv,
		"lng_unsupported_block_text"sv,
		"lng_unsupported_message_text"sv,
		"lng_update_telegram"sv,
	};
	const auto view = std::string_view(key.constData(), key.size());
	return std::binary_search(kBranded.begin(), kBranded.end(), view);
}

} // namespace LoogriGram::Lang
