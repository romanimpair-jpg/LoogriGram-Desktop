/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Calls::Group::Ui {

using namespace ::Ui;

// LoogriGram: this also carried how long a paid comment stayed pinned and
// how much text and how many emoji its price bought. Nothing is priced any
// more, so only the colour and the threshold it starts at are left.
struct StarsColoring {
	int bgLight = 0;
	int bgDark = 0;
	int fromStars = 0;

	friend inline auto operator<=>(
		const StarsColoring &,
		const StarsColoring &) = default;
	friend inline bool operator==(
		const StarsColoring &,
		const StarsColoring &) = default;
};

[[nodiscard]] StarsColoring StarsColoringForCount(
	const std::vector<StarsColoring> &colorings,
	int stars);

} // namespace Calls::Group::Ui
