/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Ui {

class RpWidget;

// LoogriGram: ShowOrPremiumBox is gone. It had two callers, both the same
// bargain - reveal your own last seen, or your own read time, to be allowed
// to see the other person's, or else subscribe. Ghost mode hides both on
// purpose and premium cannot be bought, so neither half was on offer.
//
// What is left is the "or" divider it drew between the two halves, which is
// a plain labelled rule with nothing premium about it, kept because the
// forbidden-invite cover also uses one.
[[nodiscard]] object_ptr<RpWidget> MakeShowOrLabel(
	not_null<RpWidget*> parent,
	rpl::producer<QString> text);

} // namespace Ui
