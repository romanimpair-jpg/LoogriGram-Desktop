/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

class History;
class ApiWrap;

namespace Main {
class Session;
} // namespace Main

namespace Api {

class Premium final {
public:
	explicit Premium(not_null<ApiWrap*> api);

	[[nodiscard]] auto someMessageMoneyRestrictionsResolved() const
		-> rpl::producer<>;
	void resolveMessageMoneyRestrictions(not_null<UserData*> user);

private:
	void requestPremiumRequiredSlice();

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	rpl::event_stream<> _someMessageMoneyRestrictionsResolved;
	base::flat_set<not_null<UserData*>> _resolveMessageMoneyRequiredUsers;
	base::flat_set<not_null<UserData*>> _resolveMessageMoneyRequestedUsers;
	bool _messageMoneyRequestScheduled = false;

};


struct MessageMoneyRestriction {
	bool premiumRequired = false;
	bool paymentRequired = false;
	bool known = false;

	explicit operator bool() const {
		return premiumRequired || paymentRequired;
	}

	friend inline bool operator==(
		const MessageMoneyRestriction &,
		const MessageMoneyRestriction &) = default;
};
[[nodiscard]] MessageMoneyRestriction ResolveMessageMoneyRestrictions(
	not_null<PeerData*> peer,
	History *maybeHistory);

// LoogriGram: the server refused a send because the peer charges for it.
// Locks a user from now on and returns the text to show.
[[nodiscard]] QString LockPaymentRequired(not_null<PeerData*> peer);

} // namespace Api
