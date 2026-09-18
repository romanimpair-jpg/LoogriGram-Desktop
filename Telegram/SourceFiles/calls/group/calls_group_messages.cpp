/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_messages.h"

#include "apiwrap.h"
#include "api/api_blocked_peers.h"
#include "api/api_chat_participants.h"
#include "api/api_text_entities.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_message_encryption.h"
#include "data/data_channel.h"
#include "data/data_group_call.h"
#include "data/data_message_reactions.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"

namespace Calls::Group {
namespace {

constexpr auto kMaxShownVideoStreamMessages = 100;
} // namespace

Messages::Messages(not_null<GroupCall*> call, not_null<MTP::Sender*> api)
: _call(call)
, _session(&call->peer()->session())
, _api(api)
, _destroyTimer([=] { checkDestroying(); })
, _ttl(_session->appConfig().groupCallMessageTTL()) {
	Ui::PostponeCall(_call, [=] {
		_call->real(
		) | rpl::on_next([=](not_null<Data::GroupCall*> call) {
			_real = call;
			if (ready()) {
				sendPending();
			} else {
				Unexpected("Not ready call.");
			}
		}, _lifetime);
	});
}

Messages::~Messages() = default;

bool Messages::ready() const {
	return _real && (!_call->conference() || _call->e2eEncryptDecrypt());
}

void Messages::send(TextWithTags text) {
	if (text.empty()) {
		return;
	} else if (!ready()) {
		_pending.push_back({ std::move(text) });
		return;
	}

	auto prepared = TextWithEntities{
		text.text,
		TextUtilities::ConvertTextTagsToEntities(text.tags)
	};
	auto serialized = MTPTextWithEntities(MTP_textWithEntities(
		MTP_string(prepared.text),
		Api::EntitiesToMTP(
			&_real->session(),
			prepared.entities,
			Api::ConvertOption::SkipLocal)));

	const auto localId = _call->peer()->owner().nextLocalMessageId();
	const auto randomId = base::RandomValue<uint64>();
	_sendingIdByRandomId.emplace(randomId, localId);

	const auto from = _call->messagesFrom();
	const auto creator = _real->creator();
	const auto skip = skipMessage(prepared);
	if (skip) {
		_skippedIds.emplace(localId);
	} else {
		_messages.push_back({
			.id = localId,
			.peer = from,
			.text = std::move(prepared),
			.admin = (from == _call->peer()) || (creator && from->isSelf()),
			.mine = true,
		});
	}
	if (!_call->conference()) {
		using Flag = MTPphone_SendGroupCallMessage::Flag;
		_api->request(MTPphone_SendGroupCallMessage(
			MTP_flags(Flag::f_send_as),
			_call->inputCall(),
			MTP_long(randomId),
			serialized,
			MTP_long(0),
			from->input()
		)).done([=](
				const MTPUpdates &result,
				const MTP::Response &response) {
			_session->api().applyUpdates(result, randomId);
		}).fail([=](const MTP::Error &, const MTP::Response &response) {
			failed(randomId, response);
		}).send();
	} else {
		const auto bytes = SerializeMessage({ randomId, serialized });
		auto v = std::vector<std::uint8_t>(bytes.size());
		bytes::copy(bytes::make_span(v), bytes::make_span(bytes));

		const auto userId = peerToUser(from->id).bare;
		const auto encrypt = _call->e2eEncryptDecrypt();
		const auto encrypted = encrypt(v, int64_t(userId), true, 0);

		_api->request(MTPphone_SendGroupCallEncryptedMessage(
			_call->inputCall(),
			MTP_bytes(bytes::make_span(encrypted))
		)).done([=](const MTPBool &, const MTP::Response &response) {
			sent(randomId, response);
		}).fail([=](const MTP::Error &, const MTP::Response &response) {
			failed(randomId, response);
		}).send();
	}

	if (!skip) {
		checkDestroying(true);
	}
}

void Messages::setApplyingInitial(bool value) {
	_applyingInitial = value;
}

void Messages::received(const MTPDupdateGroupCallMessage &data) {
	if (!ready()) {
		return;
	}
	const auto &fields = data.vmessage().data();
	received(
		fields.vid().v,
		fields.vfrom_id(),
		fields.vmessage(),
		fields.vdate().v,
		fields.is_from_admin());
}

void Messages::received(const MTPDupdateGroupCallEncryptedMessage &data) {
	if (!ready()) {
		return;
	}
	const auto fromId = data.vfrom_id();
	const auto &bytes = data.vencrypted_message().v;
	auto v = std::vector<std::uint8_t>(bytes.size());
	bytes::copy(bytes::make_span(v), bytes::make_span(bytes));

	const auto userId = peerToUser(peerFromMTP(fromId)).bare;
	const auto decrypt = _call->e2eEncryptDecrypt();
	const auto decrypted = decrypt(v, int64_t(userId), false, 0);

	const auto deserialized = DeserializeMessage(QByteArray::fromRawData(
		reinterpret_cast<const char*>(decrypted.data()),
		decrypted.size()));
	if (!deserialized) {
		LOG(("API Error: Can't parse decrypted message"));
		return;
	}
	const auto realId = ++_conferenceIdAutoIncrement;
	const auto randomId = deserialized->randomId;
	if (!_conferenceIdByRandomId.emplace(randomId, realId).second) {
		// Already received.
		return;
	}
	received(
		realId,
		fromId,
		deserialized->message,
		base::unixtime::now(), // date
		false,
		true); // checkCustomEmoji
}

void Messages::deleted(const MTPDupdateDeleteGroupCallMessages &data) {
	const auto was = _messages.size();
	for (const auto &id : data.vmessages().v) {
		const auto i = ranges::find(_messages, id.v, &Message::id);
		if (i != end(_messages)) {
			_messages.erase(i);
		}
	}
	if (_messages.size() < was) {
		pushChanges();
	}
}

void Messages::sent(const MTPDupdateMessageID &data) {
	sent(data.vrandom_id().v, data.vid().v);
}

void Messages::sent(uint64 randomId, const MTP::Response &response) {
	const auto realId = ++_conferenceIdAutoIncrement;
	_conferenceIdByRandomId.emplace(randomId, realId);
	sent(randomId, realId);

	const auto i = ranges::find(_messages, realId, &Message::id);
	if (i != end(_messages) && !i->date) {
		i->date = Api::UnixtimeFromMsgId(response.outerMsgId);
		checkDestroying(true);
	}
}

void Messages::sent(uint64 randomId, MsgId realId) {
	const auto i = _sendingIdByRandomId.find(randomId);
	if (i == end(_sendingIdByRandomId)) {
		return;
	}
	const auto localId = i->second;
	_sendingIdByRandomId.erase(i);

	const auto j = ranges::find(_messages, localId, &Message::id);
	if (j == end(_messages)) {
		_skippedIds.emplace(realId);
		return;
	}
	j->id = realId;
	crl::on_main(this, [=] {
		const auto i = ranges::find(_messages, realId, &Message::id);
		if (i != end(_messages) && !i->date) {
			i->date = base::unixtime::now();
			checkDestroying(true);
		}
	});
	_idUpdates.fire({ .localId = localId, .realId = realId });
}

void Messages::received(
		MsgId id,
		const MTPPeer &from,
		const MTPTextWithEntities &message,
		TimeId date,
		bool fromAdmin,
		bool checkCustomEmoji) {
	const auto peer = _call->peer();
	const auto i = ranges::find(_messages, id, &Message::id);
	if (i != end(_messages)) {
		const auto fromId = peerFromMTP(from);
		const auto me1 = peer->session().userPeerId();
		const auto me2 = _call->messagesFrom()->id;
		if (((fromId == me1) || (fromId == me2)) && !i->date) {
			i->date = date;
			checkDestroying(true);
		}
		return;
	} else if (_skippedIds.contains(id)) {
		return;
	}
	auto allowedEntityTypes = std::vector<EntityType>{
		EntityType::Code,
		EntityType::Bold,
		EntityType::Semibold,
		EntityType::Spoiler,
		EntityType::StrikeOut,
		EntityType::Underline,
		EntityType::Italic,
		EntityType::CustomEmoji,
	};
	// LoogriGram: they were also kept when the peer had Premium.
	if (checkCustomEmoji && !peer->isSelf()) {
		allowedEntityTypes.pop_back();
	}
	const auto author = peer->owner().peer(peerFromMTP(from));

	auto text = Ui::Text::Filtered(
		Api::ParseTextWithEntities(&author->session(), message),
		allowedEntityTypes);
	const auto mine = author->isSelf()
		|| (author->isChannel() && author->asChannel()->amCreator());
	const auto skip = skipMessage(text);
	if (skip) {
		_skippedIds.emplace(id);
	} else {
		// Should check by sendAsPeers() list instead, but it may not be
		// loaded here yet.
		_messages.push_back({
			.id = id,
			.date = date,
			.peer = author,
			.text = std::move(text),
			.admin = fromAdmin,
			.mine = mine,
		});
		ranges::sort(_messages, ranges::less(), &Message::id);
	}
	if (!skip) {
		checkDestroying(true);
	}
}

// LoogriGram: an empty message was a pure stars donation, kept only when it
// paid at least the stream's minimum. Nothing pays now, so an empty message
// is just empty.
bool Messages::skipMessage(const TextWithEntities &text) const {
	return text.empty();
}

void Messages::checkDestroying(bool afterChanges) {
	auto next = TimeId();
	const auto now = base::unixtime::now();
	const auto initial = int(_messages.size());
	if (_call->videoStream()) {
		if (initial > kMaxShownVideoStreamMessages) {
			// LoogriGram: a paid comment was held past this trim until its
			// pin expired. Nothing is pinned, so the oldest sent messages
			// go and the ones still sending stay.
			const auto remove = initial - kMaxShownVideoStreamMessages;
			auto i = begin(_messages);
			for (auto k = 0; k != remove; ++k) {
				if (i->date) {
					i = _messages.erase(i);
				} else {
					++i;
				}
			}
		}
	} else for (auto i = begin(_messages); i != end(_messages);) {
		const auto date = i->date;
		const auto ttl = _ttl;
		if (!date) {
			if (i->id < 0) {
				++i;
			} else {
				i = _messages.erase(i);
			}
		} else if (date + ttl <= now) {
			i = _messages.erase(i);
		} else if (!next || next > date + ttl - now) {
			next = date + ttl - now;
			++i;
		} else {
			++i;
		}
	}
	if (!next) {
		_destroyTimer.cancel();
	} else {
		const auto delay = next * crl::time(1000);
		if (!_destroyTimer.isActive()
			|| (_destroyTimer.remainingTime() > delay)) {
			_destroyTimer.callOnce(delay);
		}
	}
	if (afterChanges || (_messages.size() < initial)) {
		pushChanges();
	}
}

rpl::producer<std::vector<Message>> Messages::listValue() const {
	return _changes.events_starting_with_copy(_messages);
}

rpl::producer<MessageIdUpdate> Messages::idUpdates() const {
	return _idUpdates.events();
}

void Messages::sendPending() {
	Expects(_real != nullptr);

	for (auto &pending : base::take(_pending)) {
		send(std::move(pending.text));
	}
}

void Messages::pushChanges() {
	if (_changesScheduled) {
		return;
	}
	_changesScheduled = true;
	Ui::PostponeCall(this, [=] {
		_changesScheduled = false;
		_changes.fire_copy(_messages);
	});
}

void Messages::failed(uint64 randomId, const MTP::Response &response) {
	const auto i = _sendingIdByRandomId.find(randomId);
	if (i == end(_sendingIdByRandomId)) {
		return;
	}
	const auto localId = i->second;
	_sendingIdByRandomId.erase(i);

	const auto j = ranges::find(_messages, localId, &Message::id);
	if (j != end(_messages) && !j->date) {
		j->date = Api::UnixtimeFromMsgId(response.outerMsgId);
		j->failed = true;
		checkDestroying(true);
	}
}

// LoogriGram: a paid reaction could be sent into a live stream - batched,
// scheduled, then flushed as a stars-only message - and the client kept a
// running donor total to show beside it. All deleted.

void Messages::deleteConfirmed(MessageDeleteRequest request) {
	const auto eraseFrom = [&](auto iterator) {
		if (iterator != end(_messages)) {
			_messages.erase(iterator, end(_messages));
			pushChanges();
		}
	};
	const auto peer = _call->peer();
	if (const auto from = request.deleteAllFrom) {
		using Flag = MTPphone_DeleteGroupCallParticipantMessages::Flag;
		_api->request(MTPphone_DeleteGroupCallParticipantMessages(
			MTP_flags(request.reportSpam ? Flag::f_report_spam : Flag()),
			_call->inputCall(),
			from->input()
		)).send();
		eraseFrom(ranges::remove(_messages, not_null(from), &Message::peer));
	} else {
		using Flag = MTPphone_DeleteGroupCallMessages::Flag;
		_api->request(MTPphone_DeleteGroupCallMessages(
			MTP_flags(request.reportSpam ? Flag::f_report_spam : Flag()),
			_call->inputCall(),
			MTP_vector<MTPint>(1, MTP_int(request.id.bare))
		)).send();
		eraseFrom(ranges::remove(_messages, request.id, &Message::id));
	}
	if (const auto ban = request.ban) {
		if (const auto channel = peer->asChannel()) {
			ban->session().api().chatParticipants().kick(
				channel,
				ban,
				ChatRestrictionsInfo());
		} else {
			ban->session().api().blockedPeers().block(ban);
		}
	}
}

} // namespace Calls::Group
