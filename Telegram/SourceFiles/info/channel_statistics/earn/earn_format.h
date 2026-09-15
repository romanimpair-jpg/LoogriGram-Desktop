/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/credits_amount.h"

// LoogriGram: this came from data/data_channel_earn.h, which was otherwise
// the channel revenue statistics and went with the earn pages.
namespace Data {
using EarnInt = uint64;
} // namespace Data

namespace Info::ChannelEarn {

[[nodiscard]] QString MajorPart(Data::EarnInt value);
[[nodiscard]] QString MajorPart(CreditsAmount value);
[[nodiscard]] QString MinorPart(Data::EarnInt value);
[[nodiscard]] QString MinorPart(CreditsAmount value);
[[nodiscard]] QString ToUsd(
	Data::EarnInt value,
	float64 rate,
	int afterFloat);
[[nodiscard]] QString ToUsd(
	CreditsAmount value,
	float64 rate,
	int afterFloat);

} // namespace Info::ChannelEarn
