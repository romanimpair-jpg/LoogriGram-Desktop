/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/ui/calls_group_stars_coloring.h"

#include "base/assertion.h"


namespace Calls::Group::Ui {

StarsColoring StarsColoringForCount(
		const std::vector<StarsColoring> &colorings,
		int stars) {
	for (auto i = begin(colorings), e = end(colorings); i != e; ++i) {
		if (i->fromStars > stars) {
			Assert(i != begin(colorings));
			return *(std::prev(i));
		}
	}
	return colorings.back();
}

// LoogriGram: a live stream comment was priced by its length and drew a
// level bar as you paid more. Nothing is priced now; what is left is the
// colour ramp, which the gift auction slider still uses.

} // namespace Calls::Group::Ui
