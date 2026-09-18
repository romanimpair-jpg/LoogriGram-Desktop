/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Data {

// LoogriGram: every limit here came as a Default / Premium / Current triple,
// where Current picked by the account's premium flag. The account is never
// premium, so each limit is now the one Current getter, reading the default
// key. The three *Premium getters that remain are not about this account:
// see maxPinnedChatsLimitValue() in data_session.cpp.
class PremiumLimits final {
public:
	PremiumLimits(not_null<Main::Session*> session);

	[[nodiscard]] int channelsCurrent() const;
	[[nodiscard]] int similarChannelsCurrent() const;
	[[nodiscard]] int gifsCurrent() const;
	[[nodiscard]] int stickersFavedCurrent() const;
	[[nodiscard]] int dialogFiltersCurrent() const;
	[[nodiscard]] int dialogShareableFiltersCurrent() const;

	[[nodiscard]] int dialogFiltersChatsCurrent() const;
	[[nodiscard]] int dialogFiltersChatsPremium() const;

	[[nodiscard]] int dialogFiltersLinksCurrent() const;

	[[nodiscard]] int dialogsPinnedCurrent() const;
	[[nodiscard]] int dialogsPinnedPremium() const;

	[[nodiscard]] int dialogsFolderPinnedCurrent() const;
	[[nodiscard]] int dialogsFolderPinnedPremium() const;

	[[nodiscard]] int topicsPinnedCurrent() const;

	[[nodiscard]] int savedSublistsPinnedCurrent() const;
	[[nodiscard]] int savedSublistsPinnedPremium() const;

	[[nodiscard]] int channelsPublicCurrent() const;
	[[nodiscard]] int captionLengthCurrent() const;
	[[nodiscard]] int messageLengthCurrent() const;
	[[nodiscard]] int uploadMaxCurrent() const;
	[[nodiscard]] int aboutLengthCurrent() const;
	[[nodiscard]] int contactNoteLengthCurrent() const;

	[[nodiscard]] int maxBoostLevel() const;

	[[nodiscard]] int botsCreateCurrent() const;

private:
	[[nodiscard]] int appConfigLimit(
		const QString &key,
		int fallback) const;

	const not_null<Main::Session*> _session;

};

class LevelLimits final {
public:
	LevelLimits(not_null<Main::Session*> session);

	[[nodiscard]] int channelBgIconLevelMin() const;
	[[nodiscard]] int channelProfileBgIconLevelMin() const;
	[[nodiscard]] int channelEmojiStatusLevelMin() const;
	[[nodiscard]] int channelWallpaperLevelMin() const;
	[[nodiscard]] int channelCustomWallpaperLevelMin() const;
	[[nodiscard]] int channelAutoTranslateLevelMin() const;
	[[nodiscard]] int groupTranscribeLevelMin() const;
	[[nodiscard]] int groupEmojiStickersLevelMin() const;
	[[nodiscard]] int groupProfileBgIconLevelMin() const;
	[[nodiscard]] int groupEmojiStatusLevelMin() const;
	[[nodiscard]] int groupCustomWallpaperLevelMin() const;

private:
	const not_null<Main::Session*> _session;

};

} // namespace Data
