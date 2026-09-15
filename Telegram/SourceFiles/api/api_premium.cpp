/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_premium.h"

#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "base/random.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h" // ColorCollectible
#include "ui/text/format_values.h"

namespace Api {
namespace {

[[nodiscard]] int FindStarsForResale(const MTPVector<MTPStarsAmount> *list) {
	if (!list) {
		return 0;
	}
	for (const auto &amount : list->v) {
		if (amount.type() == mtpc_starsAmount) {
			return int(amount.c_starsAmount().vamount().v);
		}
	}
	return 0;
}

[[nodiscard]] int64 FindTonForResale(const MTPVector<MTPStarsAmount> *list) {
	if (!list) {
		return 0;
	}
	for (const auto &amount : list->v) {
		if (amount.type() == mtpc_starsTonAmount) {
			return int64(amount.c_starsTonAmount().vamount().v);
		}
	}
	return 0;
}

} // namespace

Premium::Premium(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
	crl::on_main(_session, [=] {
		// You can't use _session->user() in the constructor,
		// only queued, because it is not constructed yet.
		Data::AmPremiumValue(
			_session
		) | rpl::on_next([=] {
			reload();
			if (_session->premium()) {
				reloadCloudSet();
			}
		}, _session->lifetime());
	});
}

auto Premium::stickers() const
-> const std::vector<not_null<DocumentData*>> & {
	return _stickers;
}

rpl::producer<> Premium::stickersUpdated() const {
	return _stickersUpdated.events();
}

auto Premium::cloudSet() const
-> const std::vector<not_null<DocumentData*>> & {
	return _cloudSet;
}

rpl::producer<> Premium::cloudSetUpdated() const {
	return _cloudSetUpdated.events();
}

auto Premium::helloStickers() const
-> const std::vector<not_null<DocumentData*>> & {
	if (_helloStickers.empty()) {
		const_cast<Premium*>(this)->reloadHelloStickers();
	}
	return _helloStickers;
}

rpl::producer<> Premium::helloStickersUpdated() const {
	return _helloStickersUpdated.events();
}

// LoogriGram: reload() also fetched help.getPremiumPromo, which is the
// subscription's price list, its pitch text and the videos shown beside it.
// The section that displayed all three is deleted.
void Premium::reload() {
	reloadStickers();
}

void Premium::reloadStickers() {
	if (_stickersRequestId) {
		return;
	}
	_stickersRequestId = _api.request(MTPmessages_GetStickers(
		MTP_string("\xe2\xad\x90\xef\xb8\x8f\xe2\xad\x90\xef\xb8\x8f"),
		MTP_long(_stickersHash)
	)).done([=](const MTPmessages_Stickers &result) {
		_stickersRequestId = 0;
		result.match([&](const MTPDmessages_stickersNotModified &) {
		}, [&](const MTPDmessages_stickers &data) {
			_stickersHash = data.vhash().v;
			const auto owner = &_session->data();
			_stickers.clear();
			for (const auto &sticker : data.vstickers().v) {
				const auto document = owner->processDocument(sticker);
				if (document->isPremiumSticker()) {
					_stickers.push_back(document);
				}
			}
			_stickersUpdated.fire({});
		});
	}).fail([=] {
		_stickersRequestId = 0;
	}).send();
}

void Premium::reloadCloudSet() {
	if (_cloudSetRequestId) {
		return;
	}
	_cloudSetRequestId = _api.request(MTPmessages_GetStickers(
		MTP_string("\xf0\x9f\x93\x82\xe2\xad\x90\xef\xb8\x8f"),
		MTP_long(_cloudSetHash)
	)).done([=](const MTPmessages_Stickers &result) {
		_cloudSetRequestId = 0;
		result.match([&](const MTPDmessages_stickersNotModified &) {
		}, [&](const MTPDmessages_stickers &data) {
			_cloudSetHash = data.vhash().v;
			const auto owner = &_session->data();
			_cloudSet.clear();
			for (const auto &sticker : data.vstickers().v) {
				const auto document = owner->processDocument(sticker);
				if (document->isPremiumSticker()) {
					_cloudSet.push_back(document);
				}
			}
			_cloudSetUpdated.fire({});
		});
	}).fail([=] {
		_cloudSetRequestId = 0;
	}).send();
}

void Premium::reloadHelloStickers() {
	if (_helloStickersRequestId) {
		return;
	}
	_helloStickersRequestId = _api.request(MTPmessages_GetStickers(
		MTP_string("\xf0\x9f\x91\x8b\xe2\xad\x90\xef\xb8\x8f"),
		MTP_long(_helloStickersHash)
	)).done([=](const MTPmessages_Stickers &result) {
		_helloStickersRequestId = 0;
		result.match([&](const MTPDmessages_stickersNotModified &) {
		}, [&](const MTPDmessages_stickers &data) {
			_helloStickersHash = data.vhash().v;
			const auto owner = &_session->data();
			_helloStickers.clear();
			for (const auto &sticker : data.vstickers().v) {
				const auto document = owner->processDocument(sticker);
				if (document->sticker()) {
					_helloStickers.push_back(document);
				}
			}
			_helloStickersUpdated.fire({});
		});
	}).fail([=] {
		_helloStickersRequestId = 0;
	}).send();
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
			const auto set = [&](bool requirePremium) {
				using Flag = UserDataFlag;
				constexpr auto me = Flag::RequiresPremiumToWrite;
				constexpr auto known = Flag::MessageMoneyRestrictionsKnown;
				constexpr auto hasPrem = Flag::HasRequirePremiumToWrite;
				user->setFlags((user->flags() & ~me)
					| known
					| (requirePremium ? (me | hasPrem) : Flag()));
			};
			if (index >= list.size()) {
				set(false);
				continue;
			}
			// LoogriGram: a third requirement, paying stars per message,
			// used to be answered by remembering the price. Nothing here
			// pays, so it reads the same as no requirement at all.
			list[index++].match([&](const MTPDrequirementToContactEmpty &) {
				set(false);
			}, [&](const MTPDrequirementToContactPremium &) {
				set(true);
			}, [&](const MTPDrequirementToContactPaidMessages &data) {
				set(false);
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
			.premiumRequired = (user->requiresPremiumToWrite()
				&& !user->session().premium()),
			.known = true,
		};
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
			.premiumRequired = !user->session().premium(),
			.known = true,
		};
	}
	return {};
}

std::optional<Data::StarGift> FromTL(
		not_null<Main::Session*> session,
		const MTPstarGift &gift) {
	return gift.match([&](const MTPDstarGift &data) {
		const auto document = session->data().processDocument(
			data.vsticker());
		const auto resellPrice = data.vresell_min_stars().value_or_empty();
		const auto remaining = data.vavailability_remains();
		const auto total = data.vavailability_total();
		if (!document->sticker()) {
			return std::optional<Data::StarGift>();
		}
		const auto releasedById = data.vreleased_by()
			? peerFromMTP(*data.vreleased_by())
			: PeerId();
		const auto releasedBy = releasedById
			? session->data().peer(releasedById).get()
			: nullptr;
		const auto background = [&] {
			if (!data.vbackground()) {
				return std::shared_ptr<Data::StarGiftBackground>();
			}
			const auto &fields = data.vbackground()->data();
			using namespace Ui;
			return std::make_shared<Data::StarGiftBackground>(
				Data::StarGiftBackground{
					.center = ColorFromSerialized(fields.vcenter_color()),
					.edge = ColorFromSerialized(fields.vedge_color()),
					.text = ColorFromSerialized(fields.vtext_color()),
				});
		};
		return std::optional<Data::StarGift>(Data::StarGift{
			.id = uint64(data.vid().v),
			.background = background(),
			.stars = int64(data.vstars().v),
			.starsConverted = int64(data.vconvert_stars().v),
			.starsToUpgrade = int64(data.vupgrade_stars().value_or_empty()),
			.starsResellMin = int64(resellPrice),
			.document = document,
			.releasedBy = releasedBy,
			.resellTitle = qs(data.vtitle().value_or_empty()),
			.resellCount = int(data.vavailability_resale().value_or_empty()),
			.auctionSlug = qs(data.vauction_slug().value_or_empty()),
			.auctionGiftsPerRound = data.vgifts_per_round().value_or_empty(),
			.auctionStartDate = data.vauction_start_date().value_or_empty(),
			.limitedLeft = remaining.value_or_empty(),
			.limitedCount = total.value_or_empty(),
			.perUserTotal = data.vper_user_total().value_or_empty(),
			.perUserRemains = data.vper_user_remains().value_or_empty(),
			.upgradeVariants = data.vupgrade_variants().value_or_empty(),
			.firstSaleDate = data.vfirst_sale_date().value_or_empty(),
			.lastSaleDate = data.vlast_sale_date().value_or_empty(),
			.lockedUntilDate = data.vlocked_until_date().value_or_empty(),
			.requirePremium = data.is_require_premium(),
			.peerColorAvailable = data.is_peer_color_available(),
			.upgradable = data.vupgrade_stars().has_value(),
			.birthday = data.is_birthday(),
			.soldOut = data.is_sold_out(),
		});
	}, [&](const MTPDstarGiftUnique &data) {
		const auto total = data.vavailability_total().v;
		auto model = std::optional<Data::UniqueGiftModel>();
		auto pattern = std::optional<Data::UniqueGiftPattern>();
		for (const auto &attribute : data.vattributes().v) {
			attribute.match([&](const MTPDstarGiftAttributeModel &data) {
				model = FromTL(session, data);
			}, [&](const MTPDstarGiftAttributePattern &data) {
				pattern = FromTL(session, data);
			}, [&](const MTPDstarGiftAttributeBackdrop &data) {
			}, [&](const MTPDstarGiftAttributeOriginalDetails &data) {
			});
		}
		if (!model
			|| !model->document->sticker()
			|| !pattern
			|| !pattern->document->sticker()) {
			return std::optional<Data::StarGift>();
		}
		const auto releasedById = data.vreleased_by()
			? peerFromMTP(*data.vreleased_by())
			: PeerId();
		const auto themeUserId = data.vtheme_peer()
			? peerFromMTP(*data.vtheme_peer())
			: PeerId();
		const auto releasedBy = releasedById
			? session->data().peer(releasedById).get()
			: nullptr;
		const auto themeUser = themeUserId
			? session->data().peer(themeUserId).get()
			: nullptr;
		const auto colorCollectible = (data.vpeer_color()
			&& data.vpeer_color()->type() == mtpc_peerColorCollectible)
			? std::make_shared<Ui::ColorCollectible>(
				Data::ParseColorCollectible(
					data.vpeer_color()->c_peerColorCollectible()))
			: nullptr;
		auto result = Data::StarGift{
			.id = data.vid().v,
			.unique = std::make_shared<Data::UniqueGift>(Data::UniqueGift{
				.id = data.vid().v,
				.initialGiftId = data.vgift_id().v,
				.slug = qs(data.vslug()),
				.title = qs(data.vtitle()),
				.giftAddress = qs(data.vgift_address().value_or_empty()),
				.ownerAddress = qs(data.vowner_address().value_or_empty()),
				.ownerName = qs(data.vowner_name().value_or_empty()),
				.ownerId = (data.vowner_id()
					? peerFromMTP(*data.vowner_id())
					: PeerId()),
				.hostId = (data.vhost_id()
					? peerFromMTP(*data.vhost_id())
					: PeerId()),
				.releasedBy = releasedBy,
				.themeUser = themeUser,
				.nanoTonForResale = FindTonForResale(data.vresell_amount()),
				.craftChancePermille
					= data.vcraft_chance_permille().value_or_empty(),
				.starsForResale = FindStarsForResale(data.vresell_amount()),
				.starsMinOffer = data.voffer_min_stars().value_or(-1),
				.number = data.vnum().v,
				.onlyAcceptTon = data.is_resale_ton_only(),
				.canBeTheme = data.is_theme_available(),
				.crafted = data.is_crafted(),
				.burned = data.is_burned(),
				.model = *model,
				.pattern = *pattern,
				.value = (data.vvalue_amount()
					? std::make_shared<Data::UniqueGiftValue>(
						Data::UniqueGiftValue{
							.currency = qs(
								data.vvalue_currency().value_or_empty()),
							.valuePrice = int64(
								data.vvalue_amount().value_or_empty()),
							.valuePriceUsd = int64(
								data.vvalue_usd_amount().value_or_empty()),
						})
					: nullptr),
				.peerColor = colorCollectible,
			}),
			.document = model->document,
			.releasedBy = releasedBy,
			.limitedLeft = (total - data.vavailability_issued().v),
			.limitedCount = total,
			.resellTonOnly = data.is_resale_ton_only(),
			.requirePremium = data.is_require_premium(),
		};
		const auto unique = result.unique.get();
		for (const auto &attribute : data.vattributes().v) {
			attribute.match([&](const MTPDstarGiftAttributeModel &data) {
			}, [&](const MTPDstarGiftAttributePattern &data) {
			}, [&](const MTPDstarGiftAttributeBackdrop &data) {
				unique->backdrop = FromTL(data);
			}, [&](const MTPDstarGiftAttributeOriginalDetails &data) {
				unique->originalDetails = FromTL(session, data);
			});
		}
		return std::make_optional(std::move(result));
	});
}

std::optional<Data::SavedStarGift> FromTL(
		not_null<PeerData*> to,
		const MTPsavedStarGift &gift) {
	const auto session = &to->session();
	const auto &data = gift.data();
	auto parsed = FromTL(session, data.vgift());
	if (!parsed) {
		return {};
	} else if (const auto unique = parsed->unique.get()) {
		unique->starsForTransfer = data.vtransfer_stars().value_or(-1);
		unique->exportAt = data.vcan_export_at().value_or_empty();
		unique->canTransferAt = data.vcan_transfer_at().value_or_empty();
		unique->canResellAt = data.vcan_resell_at().value_or_empty();
		unique->canCraftAt = data.vcan_craft_at().value_or_empty();
	}
	using Id = Data::SavedStarGiftId;
	const auto hasUnique = parsed->unique != nullptr;
	return Data::SavedStarGift{
		.info = std::move(*parsed),
		.manageId = (to->isUser()
			? Id::User(data.vmsg_id().value_or_empty())
			: Id::Chat(to, data.vsaved_id().value_or_empty())),
		.collectionIds = (data.vcollection_id()
			? (data.vcollection_id()->v
				| ranges::views::transform(&MTPint::v)
				| ranges::to_vector)
			: std::vector<int>()),
		.message = (data.vmessage()
			? Api::ParseTextWithEntities(
				session,
				*data.vmessage())
			: TextWithEntities()),
		.starsConverted = int64(data.vconvert_stars().value_or_empty()),
		.starsUpgradedBySender = int64(
			data.vupgrade_stars().value_or_empty()),
		.starsForDetailsRemove = int64(
			data.vdrop_original_details_stars().value_or_empty()),
		.giftPrepayUpgradeHash = qs(
			data.vprepaid_upgrade_hash().value_or_empty()),
		.fromId = (data.vfrom_id()
			? peerFromMTP(*data.vfrom_id())
			: PeerId()),
		.date = data.vdate().v,
		.giftNum = data.vgift_num().value_or_empty(),
		.upgradeSeparate = data.is_upgrade_separate(),
		.upgradable = data.is_can_upgrade(),
		.anonymous = data.is_name_hidden(),
		.pinned = data.is_pinned_to_top() && hasUnique,
		.hidden = data.is_unsaved(),
		.mine = to->isSelf(),
	};
}

int ParseRarity(const MTPStarGiftAttributeRarity &rarity) {
	return rarity.match([&](const MTPDstarGiftAttributeRarity &data) {
		return std::max(data.vpermille().v, 0);
	}, [&](const MTPDstarGiftAttributeRarityUncommon &) {
		return int(Data::UniqueGiftRarity::Uncommon);
	}, [&](const MTPDstarGiftAttributeRarityRare &) {
		return int(Data::UniqueGiftRarity::Rare);
	}, [&](const MTPDstarGiftAttributeRarityEpic &) {
		return int(Data::UniqueGiftRarity::Epic);
	}, [&](const MTPDstarGiftAttributeRarityLegendary &) {
		return int(Data::UniqueGiftRarity::Legendary);
	});
}

Data::UniqueGiftModel FromTL(
		not_null<Main::Session*> session,
		const MTPDstarGiftAttributeModel &data) {
	auto result = Data::UniqueGiftModel{
		.document = session->data().processDocument(data.vdocument()),
	};
	result.name = qs(data.vname());
	result.rarityValue = ParseRarity(data.vrarity());
	return result;
}

Data::UniqueGiftPattern FromTL(
		not_null<Main::Session*> session,
		const MTPDstarGiftAttributePattern &data) {
	auto result = Data::UniqueGiftPattern{
		.document = session->data().processDocument(data.vdocument()),
	};
	result.document->overrideEmojiUsesTextColor(true);
	result.name = qs(data.vname());
	result.rarityValue = ParseRarity(data.vrarity());
	return result;
}

Data::UniqueGiftBackdrop FromTL(const MTPDstarGiftAttributeBackdrop &data) {
	auto result = Data::UniqueGiftBackdrop{ .id = data.vbackdrop_id().v };
	result.name = qs(data.vname());
	result.rarityValue = ParseRarity(data.vrarity());
	result.centerColor = Ui::ColorFromSerialized(
		data.vcenter_color());
	result.edgeColor = Ui::ColorFromSerialized(
		data.vedge_color());
	result.patternColor = Ui::ColorFromSerialized(
		data.vpattern_color());
	result.textColor = Ui::ColorFromSerialized(
		data.vtext_color());
	return result;
}

Data::UniqueGiftOriginalDetails FromTL(
		not_null<Main::Session*> session,
		const MTPDstarGiftAttributeOriginalDetails &data) {
	auto result = Data::UniqueGiftOriginalDetails();
	result.date = data.vdate().v;
	result.senderId = data.vsender_id()
		? peerFromMTP(*data.vsender_id())
		: PeerId();
	result.recipientId = peerFromMTP(data.vrecipient_id());
	result.message = data.vmessage()
		? ParseTextWithEntities(session, *data.vmessage())
		: TextWithEntities();
	return result;
}

} // namespace Api
