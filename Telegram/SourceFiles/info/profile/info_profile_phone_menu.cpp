/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_phone_menu.h"

#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "menu/menu_checked_action.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_menu_icons.h"

namespace Info {
namespace Profile {

void AddPhoneSpoilerMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<UserData*> user) {
	if (!user->isSelf()) {
		return;
	}
	const auto session = &user->session();
	const auto toggle = [=] {
		auto &settings = session->settings();
		settings.setPhoneNumberHidden(!settings.phoneNumberHidden());
		session->saveSettingsDelayed();
	};
	Menu::AddCheckedAction(
		menu,
		tr::lng_context_spoiler_effect(tr::now),
		toggle,
		&st::menuIconSpoiler,
		session->settings().phoneNumberHidden());
}

} // namespace Profile
} // namespace Info
