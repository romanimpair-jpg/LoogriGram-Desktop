/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/business/data_business_common.h"

namespace Data {

class Session;

class BusinessInfo final {
public:
	explicit BusinessInfo(not_null<Session*> owner);
	~BusinessInfo();

	// LoogriGram: this also saved our own business hours, location, intro,
	// away message and greeting message. Those are Premium Business
	// settings; what is left is the timezone list that other users'
	// business hours are shown against.
	[[nodiscard]] rpl::producer<Timezones> timezonesValue() const;

private:
	void preloadTimezones();

	const not_null<Session*> _owner;

	rpl::variable<Timezones> _timezones;

	mtpRequestId _timezonesRequestId = 0;
	int32 _timezonesHash = 0;

};

[[nodiscard]] QString FindClosestTimezoneId(
	const std::vector<Timezone> &list);

} // namespace Data
