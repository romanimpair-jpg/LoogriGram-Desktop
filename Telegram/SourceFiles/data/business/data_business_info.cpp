/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/business/data_business_info.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/business/data_business_common.h"
#include "data/data_session.h"
#include "main/main_session.h"

namespace Data {

BusinessInfo::BusinessInfo(not_null<Session*> owner)
: _owner(owner) {
}

BusinessInfo::~BusinessInfo() = default;

void BusinessInfo::preloadTimezones() {
	if (!_timezones.current().list.empty() || _timezonesRequestId) {
		return;
	}
	_timezonesRequestId = _owner->session().api().request(
		MTPhelp_GetTimezonesList(MTP_int(_timezonesHash))
	).done([=](const MTPhelp_TimezonesList &result) {
		result.match([&](const MTPDhelp_timezonesList &data) {
			_timezonesHash = data.vhash().v;
			const auto proj = [](const MTPtimezone &result) {
				return Timezone{
					.id = qs(result.data().vid()),
					.name = qs(result.data().vname()),
					.utcOffset = result.data().vutc_offset().v,
				};
			};
			_timezones = Timezones{
				.list = ranges::views::all(
					data.vtimezones().v
				) | ranges::views::transform(
					proj
				) | ranges::to_vector,
			};
		}, [](const MTPDhelp_timezonesListNotModified &) {
		});
	}).send();
}

rpl::producer<Timezones> BusinessInfo::timezonesValue() const {
	const_cast<BusinessInfo*>(this)->preloadTimezones();
	return _timezones.value();
}

QString FindClosestTimezoneId(const std::vector<Timezone> &list) {
	const auto local = QDateTime::currentDateTime();
	const auto utc = QDateTime(local.date(), local.time(), Qt::UTC);
	const auto shift = base::unixtime::now() - (TimeId)::time(nullptr);
	const auto delta = int(utc.toSecsSinceEpoch())
		- int(local.toSecsSinceEpoch())
		- shift;
	const auto proj = [&](const Timezone &value) {
		auto distance = value.utcOffset - delta;
		while (distance > 12 * 3600) {
			distance -= 24 * 3600;
		}
		while (distance < -12 * 3600) {
			distance += 24 * 3600;
		}
		return std::abs(distance);
	};
	return ranges::min_element(list, ranges::less(), proj)->id;
}

} // namespace Data
