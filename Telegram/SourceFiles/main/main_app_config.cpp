/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_app_config.h"

#include "api/api_authorizations.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "data/data_session.h"
#include "main/main_account.h"
#include "main/main_session.h"

namespace Main {
namespace {

constexpr auto kRefreshTimeout = 3600 * crl::time(1000);

} // namespace

AppConfig::AppConfig(not_null<Account*> account) : _account(account) {
	account->sessionChanges(
	) | rpl::filter([=](Session *session) {
		return (session != nullptr);
	}) | rpl::on_next([=] {
		_lastFrozenRefresh = 0;
		refresh();
	}, _lifetime);
}

AppConfig::~AppConfig() = default;

void AppConfig::start() {
	_account->mtpMainSessionValue(
	) | rpl::on_next([=](not_null<MTP::Instance*> instance) {
		_api.emplace(instance);
		_requestId = 0;
		refresh();

		_frozenTrackLifetime = instance->frozenErrorReceived(
		) | rpl::on_next([=] {
			if (!get<int>(u"freeze_since_date"_q, 0)) {
				const auto now = crl::now();
				if (!_lastFrozenRefresh
					|| now > _lastFrozenRefresh + kRefreshTimeout) {
					_lastFrozenRefresh = now;
					refresh();
				}
			}
		});
	}, _lifetime);
}

int AppConfig::quoteLengthMax() const {
	return get<int>(u"quote_length_max"_q, 1024);
}

int AppConfig::noForwardsRequestExpirePeriod() const {
	return get<int>(
		u"no_forwards_request_expire_period"_q,
		_account->mtp().isTestMode() ? 300 : 86400);
}

// LoogriGram: four affiliate-program server limits were read here. The
// module that asked for them is deleted.

bool AppConfig::callsDisabledForSession() const {
	const auto authorizations = _account->sessionExists()
		? &_account->session().api().authorizations()
		: nullptr;
	return get<bool>(
		u"call_requests_disabled"_q,
		authorizations->callsDisabledHere());
}

int AppConfig::confcallSizeLimit() const {
	return get<int>(
		u"conference_call_size_limit"_q,
		_account->mtp().isTestMode() ? 5 : 100);
}

bool AppConfig::confcallPrioritizeVP8() const {
	return get<bool>(u"confcall_use_vp8"_q, false);
}

int AppConfig::pollOptionsLimit() const {
	return get<int>(u"poll_answers_max"_q, 12);
}

int AppConfig::pollAnswerDeletePeriod() const {
	return get<int>(u"poll_answer_delete_period"_q, 300);
}

int AppConfig::pollCountriesMax() const {
	return get<int>(u"poll_countries_max"_q, 12);
}

QString AppConfig::phoneCountryIso2() const {
	return get<QString>(u"phone_country_iso2"_q, QString());
}

bool AppConfig::ageVerifyNeeded() const {
	return get<bool>(u"need_age_video_verification"_q, false);
}

QString AppConfig::ageVerifyCountry() const {
	return get<QString>(u"verify_age_country"_q, QString());
}

int AppConfig::ageVerifyMinAge() const {
	return get<int>(u"verify_age_min"_q, 18);
}

QString AppConfig::ageVerifyBotUsername() const {
	return get<QString>(u"verify_age_bot_username"_q, QString());
}

int AppConfig::storiesAlbumLimit() const {
	return get<int>(u"stories_album_stories_limit"_q, 1000);
}

int AppConfig::groupCallMessageLengthLimit() const {
	return get<int>(u"group_call_message_length_limit"_q, 128);
}

TimeId AppConfig::groupCallMessageTTL() const {
	return get<int>(u"group_call_message_ttl"_q, 10);
}

int AppConfig::passkeysAccountPasskeysMax() const {
	return get<int>(u"passkeys_account_passkeys_max"_q, 10);
}

bool AppConfig::settingsDisplayPasskeys() const {
	return get<bool>(u"settings_display_passkeys"_q, false);
}

void AppConfig::refresh(bool force) {
	if (_requestId || !_api) {
		if (force) {
			_pendingRefresh = true;
		}
		return;
	}
	_pendingRefresh = false;
	_requestId = _api->request(MTPhelp_GetAppConfig(
		MTP_int(_hash)
	)).done([=](const MTPhelp_AppConfig &result) {
		_requestId = 0;
		result.match([&](const MTPDhelp_appConfig &data) {
			_hash = data.vhash().v;

			const auto &config = data.vconfig();
			if (config.type() != mtpc_jsonObject) {
				LOG(("API Error: Unexpected config type."));
				return;
			}
			auto was = ignoredRestrictionReasons();

			_data.clear();
			for (const auto &element : config.c_jsonObject().vvalue().v) {
				element.match([&](const MTPDjsonObjectValue &data) {
					_data.emplace_or_assign(qs(data.vkey()), data.vvalue());
				});
			}
			updateIgnoredRestrictionReasons(std::move(was));

			DEBUG_LOG(("getAppConfig result handled."));
			_refreshed.fire({});
		}, [](const MTPDhelp_appConfigNotModified &) {});

		if (base::take(_pendingRefresh)) {
			refresh();
		} else {
			refreshDelayed();
		}
	}).fail([=] {
		_requestId = 0;
		refreshDelayed();
	}).send();
}

void AppConfig::refreshDelayed() {
	base::call_delayed(kRefreshTimeout, _account, [=] {
		refresh();
	});
}

void AppConfig::updateIgnoredRestrictionReasons(std::vector<QString> was) {
	_ignoreRestrictionReasons = get<std::vector<QString>>(
		u"ignore_restriction_reasons"_q,
		std::vector<QString>());
	ranges::sort(_ignoreRestrictionReasons);
	if (_ignoreRestrictionReasons != was) {
		for (const auto &reason : _ignoreRestrictionReasons) {
			const auto i = ranges::remove(was, reason);
			if (i != end(was)) {
				was.erase(i, end(was));
			} else {
				was.push_back(reason);
			}
		}
		_ignoreRestrictionChanges.fire(std::move(was));
	}
}

rpl::producer<> AppConfig::refreshed() const {
	return _refreshed.events();
}

rpl::producer<> AppConfig::value() const {
	return _refreshed.events_starting_with({});
}

template <typename Extractor>
auto AppConfig::getValue(const QString &key, Extractor &&extractor) const {
	const auto i = _data.find(key);
	return extractor((i != end(_data))
		? i->second
		: MTPJSONValue(MTP_jsonNull()));
}

bool AppConfig::getBool(const QString &key, bool fallback) const {
	return getValue(key, [&](const MTPJSONValue &value) {
		return value.match([&](const MTPDjsonBool &data) {
			return mtpIsTrue(data.vvalue());
		}, [&](const auto &data) {
			return fallback;
		});
	});
}

double AppConfig::getDouble(const QString &key, double fallback) const {
	return getValue(key, [&](const MTPJSONValue &value) {
		return value.match([&](const MTPDjsonNumber &data) {
			return data.vvalue().v;
		}, [&](const auto &data) {
			return fallback;
		});
	});
}

QString AppConfig::getString(
		const QString &key,
		const QString &fallback) const {
	return getValue(key, [&](const MTPJSONValue &value) {
		return value.match([&](const MTPDjsonString &data) {
			return qs(data.vvalue());
		}, [&](const auto &data) {
			return fallback;
		});
	});
}

std::vector<QString> AppConfig::getStringArray(
		const QString &key,
		std::vector<QString> &&fallback) const {
	return getValue(key, [&](const MTPJSONValue &value) {
		return value.match([&](const MTPDjsonArray &data) {
			auto result = std::vector<QString>();
			result.reserve(data.vvalue().v.size());
			for (const auto &entry : data.vvalue().v) {
				if (entry.type() != mtpc_jsonString) {
					return std::move(fallback);
				}
				result.push_back(qs(entry.c_jsonString().vvalue()));
			}
			return result;
		}, [&](const auto &data) {
			return std::move(fallback);
		});
	});
}

base::flat_map<QString, QString> AppConfig::getStringMap(
		const QString &key,
		base::flat_map<QString, QString> &&fallback) const {
	return getValue(key, [&](const MTPJSONValue &value) {
		return value.match([&](const MTPDjsonObject &data) {
			auto result = base::flat_map<QString, QString>();
			result.reserve(data.vvalue().v.size());
			for (const auto &entry : data.vvalue().v) {
				const auto &data = entry.data();
				const auto &value = data.vvalue();
				if (value.type() != mtpc_jsonString) {
					return std::move(fallback);
				}
				result.emplace(
					qs(data.vkey()),
					qs(value.c_jsonString().vvalue()));
			}
			return result;
		}, [&](const auto &data) {
			return std::move(fallback);
		});
	});
}

bool AppConfig::newRequirePremiumFree() const {
	return get<bool>(
		u"new_noncontact_peers_require_premium_without_ownpremium"_q,
		false);
}

} // namespace Main
