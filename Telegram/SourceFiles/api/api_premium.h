/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_star_gift.h"
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

	[[nodiscard]] auto helloStickers() const
		-> const std::vector<not_null<DocumentData*>> &;
	[[nodiscard]] rpl::producer<> helloStickersUpdated() const;

	[[nodiscard]] auto someMessageMoneyRestrictionsResolved() const
		-> rpl::producer<>;
	void resolveMessageMoneyRestrictions(not_null<UserData*> user);

private:
	void reloadHelloStickers();
	void requestPremiumRequiredSlice();

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	mtpRequestId _helloStickersRequestId = 0;
	uint64 _helloStickersHash = 0;
	std::vector<not_null<DocumentData*>> _helloStickers;
	rpl::event_stream<> _helloStickersUpdated;

	rpl::event_stream<> _someMessageMoneyRestrictionsResolved;
	base::flat_set<not_null<UserData*>> _resolveMessageMoneyRequiredUsers;
	base::flat_set<not_null<UserData*>> _resolveMessageMoneyRequestedUsers;
	bool _messageMoneyRequestScheduled = false;

};


struct MessageMoneyRestriction {
	bool premiumRequired = false;
	bool known = false;

	explicit operator bool() const {
		return premiumRequired;
	}

	friend inline bool operator==(
		const MessageMoneyRestriction &,
		const MessageMoneyRestriction &) = default;
};
[[nodiscard]] MessageMoneyRestriction ResolveMessageMoneyRestrictions(
	not_null<PeerData*> peer,
	History *maybeHistory);

[[nodiscard]] std::optional<Data::StarGift> FromTL(
	not_null<Main::Session*> session,
	const MTPstarGift &gift);
[[nodiscard]] std::optional<Data::SavedStarGift> FromTL(
	not_null<PeerData*> to,
	const MTPsavedStarGift &gift);

[[nodiscard]] Data::UniqueGiftModel FromTL(
	not_null<Main::Session*> session,
	const MTPDstarGiftAttributeModel &data);
[[nodiscard]] Data::UniqueGiftPattern FromTL(
	not_null<Main::Session*> session,
	const MTPDstarGiftAttributePattern &data);
[[nodiscard]] Data::UniqueGiftBackdrop FromTL(
	const MTPDstarGiftAttributeBackdrop &data);
[[nodiscard]] Data::UniqueGiftOriginalDetails FromTL(
	not_null<Main::Session*> session,
	const MTPDstarGiftAttributeOriginalDetails &data);

} // namespace Api
