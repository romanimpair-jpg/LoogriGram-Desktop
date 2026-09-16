/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_global_privacy.h"

#include "apiwrap.h"
#include "data/components/promo_suggestions.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "main/main_app_config.h"

namespace Api {

GlobalPrivacy::GlobalPrivacy(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
}

void GlobalPrivacy::reload(Fn<void()> callback) {
	if (callback) {
		_callbacks.push_back(std::move(callback));
	}
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPaccount_GetGlobalPrivacySettings(
	)).done([=](const MTPGlobalPrivacySettings &result) {
		_requestId = 0;
		apply(result);
		for (const auto &callback : base::take(_callbacks)) {
			callback();
		}
	}).fail([=] {
		_requestId = 0;
		for (const auto &callback : base::take(_callbacks)) {
			callback();
		}
	}).send();

	_session->appConfig().value(
	) | rpl::on_next([=] {
		_showArchiveAndMute = _session->appConfig().get<bool>(
			u"autoarchive_setting_available"_q,
			false);
	}, _session->lifetime());
}

bool GlobalPrivacy::archiveAndMuteCurrent() const {
	return _archiveAndMute.current();
}

rpl::producer<bool> GlobalPrivacy::archiveAndMute() const {
	return _archiveAndMute.value();
}

UnarchiveOnNewMessage GlobalPrivacy::unarchiveOnNewMessageCurrent() const {
	return _unarchiveOnNewMessage.current();
}

auto GlobalPrivacy::unarchiveOnNewMessage() const
-> rpl::producer<UnarchiveOnNewMessage> {
	return _unarchiveOnNewMessage.value();
}

rpl::producer<bool> GlobalPrivacy::showArchiveAndMute() const {
	using namespace rpl::mappers;

	return rpl::combine(
		archiveAndMute(),
		_showArchiveAndMute.value(),
		_1 || _2);
}

rpl::producer<> GlobalPrivacy::suggestArchiveAndMute() const {
	return _session->promoSuggestions().requested(u"AUTOARCHIVE_POPULAR"_q);
}

void GlobalPrivacy::dismissArchiveAndMuteSuggestion() {
	_session->promoSuggestions().dismiss(u"AUTOARCHIVE_POPULAR"_q);
}

void GlobalPrivacy::updateHideReadTime(bool hide) {
	update(
		archiveAndMuteCurrent(),
		unarchiveOnNewMessageCurrent(),
		hide,
		newRequirePremiumCurrent(),
		disallowedGiftTypesCurrent());
}

bool GlobalPrivacy::hideReadTimeCurrent() const {
	return _hideReadTime.current();
}

rpl::producer<bool> GlobalPrivacy::hideReadTime() const {
	return _hideReadTime.value();
}

bool GlobalPrivacy::newRequirePremiumCurrent() const {
	return _newRequirePremium.current();
}

rpl::producer<bool> GlobalPrivacy::newRequirePremium() const {
	return _newRequirePremium.value();
}

void GlobalPrivacy::updateMessagesPrivacy(bool requirePremium) {
	update(
		archiveAndMuteCurrent(),
		unarchiveOnNewMessageCurrent(),
		hideReadTimeCurrent(),
		requirePremium,
		disallowedGiftTypesCurrent());
}

DisallowedGiftTypes GlobalPrivacy::disallowedGiftTypesCurrent() const {
	return _disallowedGiftTypes.current();
}

auto GlobalPrivacy::disallowedGiftTypes() const
		-> rpl::producer<DisallowedGiftTypes> {
	return _disallowedGiftTypes.value();
}

void GlobalPrivacy::updateDisallowedGiftTypes(DisallowedGiftTypes types) {
	update(
		archiveAndMuteCurrent(),
		unarchiveOnNewMessageCurrent(),
		hideReadTimeCurrent(),
		newRequirePremiumCurrent(),
		types);
}

// LoogriGram: a paid reaction could be shown as coming from you, from one of
// your channels or anonymously, and the choice was a server-side privacy
// setting. Paid reactions are deleted, so there is nothing to attribute.

void GlobalPrivacy::updateArchiveAndMute(bool value) {
	update(
		value,
		unarchiveOnNewMessageCurrent(),
		hideReadTimeCurrent(),
		newRequirePremiumCurrent(),
		disallowedGiftTypesCurrent());
}

void GlobalPrivacy::updateUnarchiveOnNewMessage(
		UnarchiveOnNewMessage value) {
	update(
		archiveAndMuteCurrent(),
		value,
		hideReadTimeCurrent(),
		newRequirePremiumCurrent(),
		disallowedGiftTypesCurrent());
}

void GlobalPrivacy::update(
		bool archiveAndMute,
		UnarchiveOnNewMessage unarchiveOnNewMessage,
		bool hideReadTime,
		bool newRequirePremium,
		DisallowedGiftTypes disallowedGiftTypes) {
	using Flag = MTPDglobalPrivacySettings::Flag;
	using DisallowedFlag = MTPDdisallowedGiftsSettings::Flag;

	_api.request(_requestId).cancel();
	const auto newRequirePremiumAllowed
		= _session->appConfig().newRequirePremiumFree();
	const auto showGiftIcon
		= (disallowedGiftTypes & DisallowedGiftType::SendHide);
	const auto flags = Flag()
		| (archiveAndMute
			? Flag::f_archive_and_mute_new_noncontact_peers
			: Flag())
		| (unarchiveOnNewMessage == UnarchiveOnNewMessage::None
			? Flag::f_keep_archived_unmuted
			: Flag())
		| (unarchiveOnNewMessage != UnarchiveOnNewMessage::AnyUnmuted
			? Flag::f_keep_archived_folders
			: Flag())
		| (hideReadTime ? Flag::f_hide_read_marks : Flag())
		| ((newRequirePremium && newRequirePremiumAllowed)
			? Flag::f_new_noncontact_peers_require_premium
			: Flag())
		| (showGiftIcon ? Flag::f_display_gifts_button : Flag())
		| Flag::f_disallowed_gifts;
	const auto disallowedFlags = DisallowedFlag()
		| ((disallowedGiftTypes & DisallowedGiftType::Premium)
			? DisallowedFlag::f_disallow_premium_gifts
			: DisallowedFlag())
		| ((disallowedGiftTypes & DisallowedGiftType::Unlimited)
			? DisallowedFlag::f_disallow_unlimited_stargifts
			: DisallowedFlag())
		| ((disallowedGiftTypes & DisallowedGiftType::Limited)
			? DisallowedFlag::f_disallow_limited_stargifts
			: DisallowedFlag())
		| ((disallowedGiftTypes & DisallowedGiftType::Unique)
			? DisallowedFlag::f_disallow_unique_stargifts
			: DisallowedFlag())
		| ((disallowedGiftTypes & DisallowedGiftType::FromChannels)
			? DisallowedFlag::f_disallow_stargifts_from_channels
			: DisallowedFlag());
	const auto typesWas = _disallowedGiftTypes.current();
	const auto typesChanged = (typesWas != disallowedGiftTypes);
	_requestId = _api.request(MTPaccount_SetGlobalPrivacySettings(
		MTP_globalPrivacySettings(
			MTP_flags(flags),
			MTP_long(0),
			MTP_disallowedGiftsSettings(MTP_flags(disallowedFlags)))
	)).done([=](const MTPGlobalPrivacySettings &result) {
		_requestId = 0;
		apply(result);
		if (typesChanged) {
			_session->user()->updateFullForced();
		}
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		if (error.type() == u"PREMIUM_ACCOUNT_REQUIRED"_q) {
			update(
				archiveAndMute,
				unarchiveOnNewMessage,
				hideReadTime,
				false,
				DisallowedGiftTypes());
		}
	}).send();
	_archiveAndMute = archiveAndMute;
	_unarchiveOnNewMessage = unarchiveOnNewMessage;
	_hideReadTime = hideReadTime;
	_newRequirePremium = newRequirePremium;
	_disallowedGiftTypes = disallowedGiftTypes;
}

void GlobalPrivacy::apply(const MTPGlobalPrivacySettings &settings) {
	const auto &data = settings.data();
	_archiveAndMute = data.is_archive_and_mute_new_noncontact_peers();
	_unarchiveOnNewMessage = data.is_keep_archived_unmuted()
		? UnarchiveOnNewMessage::None
		: data.is_keep_archived_folders()
		? UnarchiveOnNewMessage::NotInFoldersUnmuted
		: UnarchiveOnNewMessage::AnyUnmuted;
	_hideReadTime = data.is_hide_read_marks();
	_newRequirePremium = data.is_new_noncontact_peers_require_premium();
	if (const auto gifts = data.vdisallowed_gifts()) {
		const auto &disallow = gifts->data();
		_disallowedGiftTypes = DisallowedGiftType()
			| (disallow.is_disallow_unlimited_stargifts()
				? DisallowedGiftType::Unlimited
				: DisallowedGiftType())
			| (disallow.is_disallow_limited_stargifts()
				? DisallowedGiftType::Limited
				: DisallowedGiftType())
			| (disallow.is_disallow_unique_stargifts()
				? DisallowedGiftType::Unique
				: DisallowedGiftType())
			| (disallow.is_disallow_premium_gifts()
				? DisallowedGiftType::Premium
				: DisallowedGiftType())
			| (disallow.is_disallow_stargifts_from_channels()
				? DisallowedGiftType::FromChannels
				: DisallowedGiftType())
			| (data.is_display_gifts_button()
				? DisallowedGiftType::SendHide
				: DisallowedGiftType());
	} else {
		_disallowedGiftTypes = data.is_display_gifts_button()
			? DisallowedGiftType::SendHide
			: DisallowedGiftType();
	}
}

} // namespace Api
