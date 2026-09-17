/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
struct EmojiGroup;
} // namespace Ui

namespace Data {

class DocumentMedia;
class Session;

struct EmojiStatusData {
	EmojiStatusId id;
	TimeId until = 0;
};

class EmojiStatuses final {
public:
	explicit EmojiStatuses(not_null<Session*> owner);
	~EmojiStatuses();

	[[nodiscard]] Session &owner() const {
		return *_owner;
	}
	[[nodiscard]] Main::Session &session() const;

	void refreshChannelDefault();
	void refreshChannelColored();

	enum class Type {
		ChannelDefault,
		ChannelColored,
	};
	[[nodiscard]] const std::vector<EmojiStatusId> &list(Type type) const;

	[[nodiscard]] EmojiStatusData parse(const MTPEmojiStatus &status);

	void set(
		not_null<ChannelData*> channel,
		EmojiStatusId id,
		TimeId until = 0);

	void registerAutomaticClear(not_null<PeerData*> peer, TimeId until);
	[[nodiscard]] TimeId automaticClearAt(not_null<PeerData*> peer) const;

	using Groups = std::vector<Ui::EmojiGroup>;
	[[nodiscard]] rpl::producer<Groups> emojiGroupsValue() const;
	[[nodiscard]] rpl::producer<Groups> stickerGroupsValue() const;
	[[nodiscard]] rpl::producer<Groups> profilePhotoGroupsValue() const;
	void requestEmojiGroups();
	void requestStickerGroups();
	void requestProfilePhotoGroups();

private:
	struct GroupsType {
		rpl::variable<Groups> data;
		mtpRequestId requestId = 0;
		int32 hash = 0;
	};

	void requestChannelDefault();
	void requestChannelColored();

	void updateChannelDefault(const MTPDaccount_emojiStatuses &data);
	void updateChannelColored(const MTPDmessages_stickerSet &data);

	void processClearingIn(TimeId wait);
	void processClearing();

	[[nodiscard]] std::vector<EmojiStatusId> parse(
		const MTPDaccount_emojiStatuses &data);

	template <typename Request>
	void requestGroups(not_null<GroupsType*> type, Request &&request);

	const not_null<Session*> _owner;

	std::vector<EmojiStatusId> _channelDefault;
	std::vector<EmojiStatusId> _channelColored;

	mtpRequestId _channelDefaultRequestId = 0;
	uint64 _channelDefaultHash = 0;

	mtpRequestId _channelColoredRequestId = 0;

	base::flat_map<not_null<ChannelData*>, mtpRequestId> _sentRequests;

	base::flat_map<not_null<PeerData*>, TimeId> _clearing;
	base::Timer _clearingTimer;

	GroupsType _emojiGroups;
	GroupsType _stickerGroups;
	GroupsType _profilePhotoGroups;

};

} // namespace Data
