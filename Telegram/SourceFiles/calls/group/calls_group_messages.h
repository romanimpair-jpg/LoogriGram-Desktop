/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"

namespace Calls {
class GroupCall;
} // namespace Calls

namespace Data {
class GroupCall;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace MTP {
class Sender;
struct Response;
} // namespace MTP

namespace Calls::Group {

struct Message {
	MsgId id = 0;
	TimeId date = 0;
	not_null<PeerData*> peer;
	TextWithEntities text;
	bool failed = false;
	bool admin = false;
	bool mine = false;
};

struct MessageIdUpdate {
	MsgId localId = 0;
	MsgId realId = 0;
};

struct MessageDeleteRequest {
	MsgId id = 0;
	PeerData *deleteAllFrom = nullptr;
	PeerData *ban = nullptr;
	bool reportSpam = false;
};

// LoogriGram: paying stars bought a live stream comment colour, a pin at
// the top and a place in a donor leaderboard. All of it is deleted, so the
// donor list, the running total and the price on a message go too.

class Messages final : public base::has_weak_ptr {
public:
	Messages(not_null<GroupCall*> call, not_null<MTP::Sender*> api);
	~Messages();

	void send(TextWithTags text);

	void setApplyingInitial(bool value);
	void received(const MTPDupdateGroupCallMessage &data);
	void received(const MTPDupdateGroupCallEncryptedMessage &data);
	void deleted(const MTPDupdateDeleteGroupCallMessages &data);
	void sent(const MTPDupdateMessageID &data);

	[[nodiscard]] rpl::producer<std::vector<Message>> listValue() const;
	[[nodiscard]] rpl::producer<MessageIdUpdate> idUpdates() const;

	void requestHiddenShow() {
		_hiddenShowRequests.fire({});
	}
	[[nodiscard]] rpl::producer<> hiddenShowRequested() const {
		return _hiddenShowRequests.events();
	}

	void deleteConfirmed(MessageDeleteRequest request);

private:
	struct Pending {
		TextWithTags text;
	};

	[[nodiscard]] bool ready() const;
	void sendPending();
	void pushChanges();
	void checkDestroying(bool afterChanges = false);

	void received(
		MsgId id,
		const MTPPeer &from,
		const MTPTextWithEntities &message,
		TimeId date,
		bool fromAdmin,
		bool checkCustomEmoji = false);
	void sent(uint64 randomId, const MTP::Response &response);
	void sent(uint64 randomId, MsgId realId);
	void failed(uint64 randomId, const MTP::Response &response);

	[[nodiscard]] bool skipMessage(const TextWithEntities &text) const;

	const not_null<GroupCall*> _call;
	const not_null<Main::Session*> _session;
	const not_null<MTP::Sender*> _api;

	MsgId _conferenceIdAutoIncrement = 0;
	base::flat_map<uint64, MsgId> _conferenceIdByRandomId;

	base::flat_map<uint64, MsgId> _sendingIdByRandomId;

	Data::GroupCall *_real = nullptr;

	std::vector<Pending> _pending;

	base::Timer _destroyTimer;
	std::vector<Message> _messages;
	base::flat_set<MsgId> _skippedIds;
	rpl::event_stream<std::vector<Message>> _changes;
	rpl::event_stream<MessageIdUpdate> _idUpdates;
	bool _applyingInitial = false;

	TimeId _ttl = 0;
	bool _changesScheduled = false;

	rpl::event_stream<> _hiddenShowRequests;

	rpl::lifetime _lifetime;

};

} // namespace Calls::Group
