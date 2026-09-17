/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/boost_box.h"

#include "lang/lang_keys.h"
#include "ui/effects/fireworks_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"

namespace Ui {

void StartFireworks(not_null<QWidget*> parent) {
	const auto result = Ui::CreateChild<RpWidget>(parent.get());
	result->setAttribute(Qt::WA_TransparentForMouseEvents);
	result->setGeometry(parent->rect());
	result->show();

	auto &lifetime = result->lifetime();
	const auto animation = lifetime.make_state<FireworksAnimation>([=] {
		result->update();
	});
	result->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(result);
		if (!animation->paint(p, result->rect())) {
			crl::on_main(result, [=] { delete result; });
		}
	}, lifetime);
}

TextWithEntities AskBoostReasonText(const AskBoostReason &reason) {
	return v::match(reason.data, [](AskBoostChannelColor data) {
		return tr::lng_boost_channel_needs_level_color(
			tr::now,
			lt_count,
			data.requiredLevel,
			tr::rich);
	}, [](AskBoostAutotranslate data) {
		return tr::lng_boost_channel_needs_level_autotranslate(
			tr::now,
			lt_count,
			data.requiredLevel,
			tr::rich);
	}, [](AskBoostWallpaper data) {
		return (data.group
			? tr::lng_boost_group_needs_level_wallpaper
			: tr::lng_boost_channel_needs_level_wallpaper)(
				tr::now,
				lt_count,
				data.requiredLevel,
				tr::rich);
	}, [](AskBoostEmojiStatus data) {
		return (data.group
			? tr::lng_boost_group_needs_level_status
			: tr::lng_boost_channel_needs_level_status)(
				tr::now,
				lt_count,
				data.requiredLevel,
				tr::rich);
	}, [](AskBoostEmojiPack data) {
		return tr::lng_boost_group_needs_level_emoji(
			tr::now,
			lt_count,
			data.requiredLevel,
			tr::rich);
	}, [](AskBoostCustomReactions data) {
		return tr::lng_boost_channel_needs_level_reactions(
			tr::now,
			lt_count,
			data.count,
			lt_same_count,
			TextWithEntities{ QString::number(data.count) },
			tr::rich);
	});
}

} // namespace Ui
