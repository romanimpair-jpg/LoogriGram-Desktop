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
constexpr auto kCheckingUpdates = Entry{ .en = "Checking for Updates..." };
constexpr auto kDownloadingUpdate = Entry{ .en = "Downloading Update" };
constexpr auto kDownloadingUpdatePercent = Entry{
	.en = "Downloading Update %1%",
};
constexpr auto kRestartToUpdate = Entry{ .en = "Restart to Update" };
constexpr auto kTranscribeTrialsOver = Entry{
	.en = "You have used all your free transcriptions this week. "
		"Wait until %1 to use it again.",
};
constexpr auto kPaidMessagesLocked = Entry{
	.en = "%1 only accepts paid messages, which LoogriGram doesn't send.",
};

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

rpl::producer<QString> CheckingUpdates() {
	return Value(kCheckingUpdates);
}

rpl::producer<QString> RestartToUpdate() {
	return Value(kRestartToUpdate);
}

rpl::producer<QString> DownloadingUpdate(rpl::producer<int> percent) {
	return rpl::combine(
		Value(kDownloadingUpdate),
		Value(kDownloadingUpdatePercent),
		std::move(percent)
	) | rpl::map([](
			const QString &plain,
			const QString &counted,
			int percent) {
		return (percent < 0) ? plain : counted.arg(percent);
	});
}

namespace {

// Puts one piece of formatted text where the entry says %1.
[[nodiscard]] TextWithEntities Substitute(
		const Entry &entry,
		TextWithEntities value) {
	const auto text = Pick(entry, ::Lang::GetInstance().id());
	const auto position = text.indexOf(u"%1"_q);
	Assert(position >= 0);

	return TextWithEntities{ text.mid(0, position) }.append(
		std::move(value)
	).append(text.mid(position + 2));
}

} // namespace

TextWithEntities TranscribeTrialsOver(TextWithEntities date) {
	return Substitute(kTranscribeTrialsOver, std::move(date));
}

TextWithEntities PaidMessagesLocked(TextWithEntities user) {
	return Substitute(kPaidMessagesLocked, std::move(user));
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
