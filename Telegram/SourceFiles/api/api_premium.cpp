/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_premium.h"

#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "core/loogrigram_lang.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/text/format_values.h"

namespace Api {

// LoogriGram: this also kept the premium sticker list (never read) and the
// premium-only cloud sticker set, refetching both whenever the account's
// premium status changed. The account is never premium. It also loaded the
// greeting stickers offered in an empty chat, which are gone.
Premium::Premium(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
}

rpl::producer<> Premium::someMessageMoneyRestrictionsResolved() const {
	return _someMessageMoneyRestrictionsResolved.events();
}

void Premium::resolveMessageMoneyRestrictions(not_null<UserData*> user) {
	_resolveMessageMoneyRequiredUsers.emplace(user);
	if (!_messageMoneyRequestScheduled
		&& _resolveMessageMoneyRequestedUsers.empty()) {
		_messageMoneyRequestScheduled = true;
		crl::on_main(_session, [=] {
			requestPremiumRequiredSlice();
		});
	}
}

void Premium::requestPremiumRequiredSlice() {
	_messageMoneyRequestScheduled = false;
	if (!_resolveMessageMoneyRequestedUsers.empty()
		|| _resolveMessageMoneyRequiredUsers.empty()) {
		return;
	}
	constexpr auto kPerRequest = 100;
	auto users = MTP_vector_from_range(_resolveMessageMoneyRequiredUsers
		| ranges::views::transform(&UserData::inputUser));
	if (users.v.size() > kPerRequest) {
		auto shortened = users.v;
		shortened.resize(kPerRequest);
		users = MTP_vector<MTPInputUser>(std::move(shortened));
		const auto from = begin(_resolveMessageMoneyRequiredUsers);
		_resolveMessageMoneyRequestedUsers = { from, from + kPerRequest };
		_resolveMessageMoneyRequiredUsers.erase(from, from + kPerRequest);
	} else {
		_resolveMessageMoneyRequestedUsers
			= base::take(_resolveMessageMoneyRequiredUsers);
	}
	const auto finish = [=](const QVector<MTPRequirementToContact> &list) {

		auto index = 0;
		for (const auto &user : base::take(_resolveMessageMoneyRequestedUsers)) {
			const auto set = [&](bool requirePremium, bool requirePayment) {
				using Flag = UserDataFlag;
				constexpr auto me = Flag::RequiresPremiumToWrite;
				constexpr auto known = Flag::MessageMoneyRestrictionsKnown;
				constexpr auto hasPrem = Flag::HasRequirePremiumToWrite;
				constexpr auto pay = Flag::RequiresPaymentToWrite;
				constexpr auto hasPay = Flag::HasRequirePaymentToWrite;
				user->setFlags((user->flags() & ~me & ~pay)
					| known
					| (requirePremium ? (me | hasPrem) : Flag())
					| (requirePayment ? (pay | hasPay) : Flag()));
			};
			if (index >= list.size()) {
				set(false, false);
				continue;
			}
			// LoogriGram: paying Stars per message used to be answered by
			// remembering the price. Nothing here pays, so the user is
			// locked instead, like one who only accepts Premium senders.
			list[index++].match([&](const MTPDrequirementToContactEmpty &) {
				set(false, false);
			}, [&](const MTPDrequirementToContactPremium &) {
				set(true, false);
			}, [&](const MTPDrequirementToContactPaidMessages &data) {
				set(false, true);
			});
		}
		if (!_messageMoneyRequestScheduled
			&& !_resolveMessageMoneyRequiredUsers.empty()) {
			_messageMoneyRequestScheduled = true;
			crl::on_main(_session, [=] {
				requestPremiumRequiredSlice();
			});
		}
		_someMessageMoneyRestrictionsResolved.fire({});
	};
	_session->api().request(
		MTPusers_GetRequirementsToContact(std::move(users))
	).done([=](const MTPVector<MTPRequirementToContact> &result) {
		finish(result.v);
	}).fail([=] {
		finish({});
	}).send();
}

QString LockPaymentRequired(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		using Flag = UserDataFlag;
		user->setFlags(user->flags()
			| Flag::MessageMoneyRestrictionsKnown
			| Flag::HasRequirePaymentToWrite
			| Flag::RequiresPaymentToWrite);
	}
	return LoogriGram::Lang::PaidMessagesLocked({ peer->shortName() }).text;
}

MessageMoneyRestriction ResolveMessageMoneyRestrictions(
		not_null<PeerData*> peer,
		History *maybeHistory) {
	if (peer->isChannel()) {
		return { .known = true };
	}
	const auto user = peer->asUser();
	if (!user) {
		return { .known = true };
	} else if (user->messageMoneyRestrictionsKnown()) {
		return {
			.premiumRequired = user->requiresPremiumToWrite(),
			.paymentRequired = user->requiresPaymentToWrite(),
			.known = true,
		};
	} else if (user->hasRequirePaymentToWrite()) {
		// Messages they sent us say nothing about what they charge us.
		return {};
	} else if (!user->hasRequirePremiumToWrite()) {
		return { .known = true };
	} else if (user->flags() & UserDataFlag::MutualContact) {
		return { .known = true };
	} else if (!maybeHistory) {
		return {};
	}
	const auto update = [&](bool require) {
		using Flag = UserDataFlag;
		constexpr auto known = Flag::MessageMoneyRestrictionsKnown;
		constexpr auto me = Flag::RequiresPremiumToWrite;
		user->setFlags((user->flags() & ~me)
			| known
			| (require ? me : Flag()));
	};
	// We allow this potentially-heavy loop because in case we've opened
	// the chat and have a lot of messages `requires_premium` will be known.
	for (const auto &block : maybeHistory->blocks) {
		for (const auto &view : block->messages) {
			const auto item = view->data();
			if (!item->out() && !item->isService()) {
				update(false);
				return { .known = true };
			}
		}
	}
	if (user->isContact() // Here we know, that we're not in his contacts.
		&& maybeHistory->loadedAtTop() // And no incoming messages.
		&& maybeHistory->loadedAtBottom()) {
		return {
			.premiumRequired = true,
			.known = true,
		};
	}
	return {};
}

} // namespace Api
