/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_emoji_statuses.h"

#include "main/main_session.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_wall_paper.h"
#include "data/stickers/data_stickers.h"
#include "base/unixtime.h"
#include "apiwrap.h"
#include "ui/controls/tabbed_search.h"

namespace Data {
namespace {

constexpr auto kMaxTimeout = 6 * 60 * 60 * crl::time(1000);

} // namespace

// LoogriGram: the default and coloured lists for our own status were
// requested here at startup and every hour after, with the channel list
// refreshed on the same timer. Our own status is gone; the channel lists
// load when the channel appearance box opens, which already asks for them.
EmojiStatuses::EmojiStatuses(not_null<Session*> owner)
: _owner(owner)
, _clearingTimer([=] { processClearing(); }) {
}

EmojiStatuses::~EmojiStatuses() = default;

Main::Session &EmojiStatuses::session() const {
	return _owner->session();
}

void EmojiStatuses::refreshChannelDefault() {
	requestChannelDefault();
}

void EmojiStatuses::refreshChannelColored() {
	requestChannelColored();
}

const std::vector<EmojiStatusId> &EmojiStatuses::list(Type type) const {
	switch (type) {
	case Type::ChannelDefault: return _channelDefault;
	case Type::ChannelColored: return _channelColored;
	}
	Unexpected("Type in EmojiStatuses::list.");
}

EmojiStatusData EmojiStatuses::parse(const MTPEmojiStatus &status) {
	return status.match([](const MTPDemojiStatus &data) {
		return EmojiStatusData{
			.id = { .documentId = data.vdocument_id().v },
			.until = data.vuntil().value_or_empty(),
		};
	}, [](const MTPDemojiStatusCollectible &) {
		// LoogriGram: a collectible gift worn as a status. Not parsed; the
		// peer shows no status, as if it had none.
		return EmojiStatusData();
	}, [](const MTPDinputEmojiStatusCollectible &) {
		return EmojiStatusData();
	}, [](const MTPDemojiStatusEmpty &) {
		return EmojiStatusData();
	});
}

void EmojiStatuses::registerAutomaticClear(
		not_null<PeerData*> peer,
		TimeId until) {
	if (!until) {
		_clearing.remove(peer);
		if (_clearing.empty()) {
			_clearingTimer.cancel();
		}
	} else if (auto &already = _clearing[peer]; already != until) {
		already = until;
		const auto i = ranges::min_element(_clearing, {}, [](auto &&pair) {
			return pair.second;
		});
		if (i->first == peer) {
			const auto now = base::unixtime::now();
			if (now < until) {
				processClearingIn(until - now);
			} else {
				processClearing();
			}
		}
	}
}

TimeId EmojiStatuses::automaticClearAt(not_null<PeerData*> peer) const {
	const auto i = _clearing.find(peer);
	return (i != end(_clearing)) ? i->second : TimeId();
}

auto EmojiStatuses::emojiGroupsValue() const -> rpl::producer<Groups> {
	const_cast<EmojiStatuses*>(this)->requestEmojiGroups();
	return _emojiGroups.data.value();
}

auto EmojiStatuses::stickerGroupsValue() const -> rpl::producer<Groups> {
	const_cast<EmojiStatuses*>(this)->requestStickerGroups();
	return _stickerGroups.data.value();
}

auto EmojiStatuses::profilePhotoGroupsValue() const
-> rpl::producer<Groups> {
	const_cast<EmojiStatuses*>(this)->requestProfilePhotoGroups();
	return _profilePhotoGroups.data.value();
}

void EmojiStatuses::requestEmojiGroups() {
	requestGroups(
		&_emojiGroups,
		MTPmessages_GetEmojiGroups(MTP_int(_emojiGroups.hash)));

}

void EmojiStatuses::requestStickerGroups() {
	requestGroups(
		&_stickerGroups,
		MTPmessages_GetEmojiStickerGroups(MTP_int(_stickerGroups.hash)));
}

void EmojiStatuses::requestProfilePhotoGroups() {
	requestGroups(
		&_profilePhotoGroups,
		MTPmessages_GetEmojiProfilePhotoGroups(
			MTP_int(_profilePhotoGroups.hash)));
}

[[nodiscard]] std::vector<Ui::EmojiGroup> GroupsFromTL(
		const MTPDmessages_emojiGroups &data) {
	const auto &list = data.vgroups().v;
	auto result = std::vector<Ui::EmojiGroup>();
	result.reserve(list.size());
	for (const auto &group : list) {
		group.match([&](const MTPDemojiGroupPremium &data) {
			result.push_back({
				.iconId = QString::number(data.vicon_emoji_id().v),
				.type = Ui::EmojiGroupType::Premium,
			});
		}, [&](const auto &data) {
			auto emoticons = ranges::views::all(
				data.vemoticons().v
			) | ranges::views::transform([](const MTPstring &emoticon) {
				return qs(emoticon);
			}) | ranges::to_vector;
			result.push_back({
				.iconId = QString::number(data.vicon_emoji_id().v),
				.emoticons = std::move(emoticons),
				.type = (MTPDemojiGroupGreeting::Is<decltype(data)>()
					? Ui::EmojiGroupType::Greeting
					: Ui::EmojiGroupType::Normal),
			});
		});
	}
	return result;
}

template <typename Request>
void EmojiStatuses::requestGroups(
		not_null<GroupsType*> type,
		Request &&request) {
	if (type->requestId) {
		return;
	}
	type->requestId = _owner->session().api().request(
		std::forward<Request>(request)
	).done([=](const MTPmessages_EmojiGroups &result) {
		type->requestId = 0;
		result.match([&](const MTPDmessages_emojiGroups &data) {
			type->hash = data.vhash().v;
			type->data = GroupsFromTL(data);
		}, [](const MTPDmessages_emojiGroupsNotModified&) {
		});
	}).fail([=] {
		type->requestId = 0;
	}).send();
}

void EmojiStatuses::processClearing() {
	auto minWait = TimeId(0);
	const auto now = base::unixtime::now();
	auto clearing = base::take(_clearing);
	for (auto i = begin(clearing); i != end(clearing);) {
		const auto until = i->second;
		if (now < until) {
			const auto wait = (until - now);
			if (!minWait || minWait > wait) {
				minWait = wait;
			}
			++i;
		} else {
			i->first->setEmojiStatus(EmojiStatusId());
			i = clearing.erase(i);
		}
	}
	if (_clearing.empty()) {
		_clearing = std::move(clearing);
	} else {
		for (const auto &[user, until] : clearing) {
			_clearing.emplace(user, until);
		}
	}
	if (minWait) {
		processClearingIn(minWait);
	} else {
		_clearingTimer.cancel();
	}
}

std::vector<EmojiStatusId> EmojiStatuses::parse(
		const MTPDaccount_emojiStatuses &data) {
	const auto &list = data.vstatuses().v;
	auto result = std::vector<EmojiStatusId>();
	result.reserve(list.size());
	for (const auto &status : list) {
		const auto parsed = parse(status);
		if (!parsed.id) {
			LOG(("API Error: empty status in account.emojiStatuses."));
		} else {
			result.push_back(parsed.id);
		}
	}
	return result;
}

void EmojiStatuses::processClearingIn(TimeId wait) {
	const auto waitms = wait * crl::time(1000);
	_clearingTimer.callOnce(std::min(waitms, kMaxTimeout));
}

void EmojiStatuses::requestChannelDefault() {
	if (_channelDefaultRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_channelDefaultRequestId = api.request(MTPaccount_GetDefaultEmojiStatuses(
		MTP_long(_channelDefaultHash)
	)).done([=](const MTPaccount_EmojiStatuses &result) {
		_channelDefaultRequestId = 0;
		result.match([&](const MTPDaccount_emojiStatuses &data) {
			updateChannelDefault(data);
		}, [&](const MTPDaccount_emojiStatusesNotModified &) {
		});
	}).fail([=] {
		_channelDefaultRequestId = 0;
		_channelDefaultHash = 0;
	}).send();
}

void EmojiStatuses::requestChannelColored() {
	if (_channelColoredRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_channelColoredRequestId = api.request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetEmojiChannelDefaultStatuses(),
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		_channelColoredRequestId = 0;
		result.match([&](const MTPDmessages_stickerSet &data) {
			updateChannelColored(data);
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
	}).fail([=] {
		_channelColoredRequestId = 0;
	}).send();
}

void EmojiStatuses::updateChannelDefault(
		const MTPDaccount_emojiStatuses &data) {
	_channelDefaultHash = data.vhash().v;
	_channelDefault = parse(data);
}

void EmojiStatuses::updateChannelColored(
		const MTPDmessages_stickerSet &data) {
	const auto &list = data.vdocuments().v;
	_channelColored.clear();
	_channelColored.reserve(list.size());
	for (const auto &sticker : data.vdocuments().v) {
		_channelColored.push_back({
			.documentId = _owner->processDocument(sticker)->id,
		});
	}
}

void EmojiStatuses::set(
		not_null<ChannelData*> channel,
		EmojiStatusId id,
		TimeId until) {
	auto &api = _owner->session().api();
	auto &requestId = _sentRequests[channel];
	if (requestId) {
		api.request(base::take(requestId)).cancel();
	}
	channel->setEmojiStatus(id, until);
	using EFlag = MTPDemojiStatus::Flag;
	const auto status = !id
		? MTP_emojiStatusEmpty()
		: MTP_emojiStatus(
			MTP_flags(until ? EFlag::f_until : EFlag()),
			MTP_long(id.documentId),
			MTP_int(until));
	requestId = api.request(MTPchannels_UpdateEmojiStatus(
		channel->inputChannel(),
		status
	)).done([=] {
		_sentRequests.remove(channel);
	}).fail([=] {
		_sentRequests.remove(channel);
	}).send();
}

} // namespace Data
