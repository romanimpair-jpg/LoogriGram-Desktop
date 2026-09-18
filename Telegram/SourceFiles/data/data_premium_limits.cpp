/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_premium_limits.h"

#include "main/main_app_config.h"
#include "main/main_session.h"

namespace Data {

PremiumLimits::PremiumLimits(not_null<Main::Session*> session)
: _session(session) {
}

int PremiumLimits::channelsCurrent() const {
	return appConfigLimit("channels_limit_default", 500);
}

int PremiumLimits::similarChannelsCurrent() const {
	return appConfigLimit("recommended_channels_limit_default", 10);
}

int PremiumLimits::gifsCurrent() const {
	return appConfigLimit("saved_gifs_limit_default", 200);
}

int PremiumLimits::stickersFavedCurrent() const {
	return appConfigLimit("stickers_faved_limit_default", 5);
}

int PremiumLimits::dialogFiltersCurrent() const {
	return appConfigLimit("dialog_filters_limit_default", 10);
}

int PremiumLimits::dialogShareableFiltersCurrent() const {
	return appConfigLimit("chatlists_joined_limit_default", 2);
}

int PremiumLimits::dialogFiltersChatsCurrent() const {
	return appConfigLimit("dialog_filters_chats_limit_default", 100);
}
int PremiumLimits::dialogFiltersChatsPremium() const {
	return appConfigLimit("dialog_filters_chats_limit_premium", 200);
}

int PremiumLimits::dialogFiltersLinksCurrent() const {
	return appConfigLimit("chatlist_invites_limit_default", 3);
}

int PremiumLimits::dialogsPinnedCurrent() const {
	return appConfigLimit("dialogs_pinned_limit_default", 5);
}
int PremiumLimits::dialogsPinnedPremium() const {
	return appConfigLimit("dialogs_pinned_limit_premium", 10);
}

int PremiumLimits::dialogsFolderPinnedCurrent() const {
	return appConfigLimit("dialogs_folder_pinned_limit_default", 100);
}
int PremiumLimits::dialogsFolderPinnedPremium() const {
	return appConfigLimit("dialogs_folder_pinned_limit_premium", 200);
}

int PremiumLimits::topicsPinnedCurrent() const {
	return appConfigLimit("topics_pinned_limit", 5);
}

int PremiumLimits::savedSublistsPinnedCurrent() const {
	return appConfigLimit("saved_dialogs_pinned_limit_default", 5);
}
int PremiumLimits::savedSublistsPinnedPremium() const {
	return appConfigLimit("saved_dialogs_pinned_limit_premium", 100);
}

int PremiumLimits::channelsPublicCurrent() const {
	return appConfigLimit("channels_public_limit_default", 10);
}

int PremiumLimits::captionLengthCurrent() const {
	return appConfigLimit("caption_length_limit_default", 1024);
}

int PremiumLimits::messageLengthCurrent() const {
	return appConfigLimit("message_length_limit_default", 4096);
}

int PremiumLimits::uploadMaxCurrent() const {
	return appConfigLimit("upload_max_fileparts_default", 4000);
}

int PremiumLimits::aboutLengthCurrent() const {
	return appConfigLimit("about_length_limit_default", 70);
}

int PremiumLimits::contactNoteLengthCurrent() const {
	return appConfigLimit("contact_note_length_limit", 128);
}

int PremiumLimits::maxBoostLevel() const {
	return appConfigLimit(
		u"boosts_channel_level_max"_q,
		_session->isTestMode() ? 9 : 99);
}

int PremiumLimits::botsCreateCurrent() const {
	return appConfigLimit("bots_create_limit_default", 20);
}

int PremiumLimits::appConfigLimit(
		const QString &key,
		int fallback) const {
	return _session->appConfig().get<int>(key, fallback);
}

LevelLimits::LevelLimits(not_null<Main::Session*> session)
: _session(session) {
}

int LevelLimits::channelBgIconLevelMin() const {
	return _session->appConfig().get<int>(
		u"channel_bg_icon_level_min"_q,
		4);
}

int LevelLimits::channelProfileBgIconLevelMin() const {
	return _session->appConfig().get<int>(
		u"channel_profile_bg_icon_level_min"_q,
		7);
}

int LevelLimits::channelEmojiStatusLevelMin() const {
	return _session->appConfig().get<int>(
		u"channel_emoji_status_level_min"_q,
		8);
}

int LevelLimits::channelWallpaperLevelMin() const {
	return _session->appConfig().get<int>(
		u"channel_wallpaper_level_min"_q,
		9);
}

int LevelLimits::channelCustomWallpaperLevelMin() const {
	return _session->appConfig().get<int>(
		u"channel_custom_wallpaper_level_min"_q,
		10);
}

int LevelLimits::groupTranscribeLevelMin() const {
	return _session->appConfig().get<int>(
		u"group_transcribe_level_min"_q,
		6);
}

int LevelLimits::groupEmojiStickersLevelMin() const {
	return _session->appConfig().get<int>(
		u"group_emoji_stickers_level_min"_q,
		4);
}

int LevelLimits::groupProfileBgIconLevelMin() const {
	return _session->appConfig().get<int>(
		u"group_profile_bg_icon_level_min"_q,
		5);
}

int LevelLimits::groupEmojiStatusLevelMin() const {
	return _session->appConfig().get<int>(
		u"group_emoji_status_level_min"_q,
		8);
}

int LevelLimits::groupCustomWallpaperLevelMin() const {
	return _session->appConfig().get<int>(
		u"group_custom_wallpaper_level_min"_q,
		10);
}

} // namespace Data
