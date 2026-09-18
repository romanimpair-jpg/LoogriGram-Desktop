/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/round_checkbox.h"

namespace Ui::Premium {

[[nodiscard]] QGradientStops ButtonGradientStops();
[[nodiscard]] QGradientStops GiftGradientStops();

[[nodiscard]] QLinearGradient ComputeGradient(
	not_null<QWidget*> content,
	int left,
	int width);

} // namespace Ui::Premium
