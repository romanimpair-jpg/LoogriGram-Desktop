/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/round_checkbox.h"

namespace style {
struct PremiumLimits;
} // namespace style

namespace tr {
template <typename ...>
struct phrase;
} // namespace tr

enum lngtag_count : int;

namespace Data {
} // namespace Data

namespace style {
struct RoundImageCheckbox;
struct PremiumOption;
struct TextStyle;
struct PremiumBubble;
} // namespace style

namespace Ui {

class GenericBox;
class RadiobuttonGroup;
class VerticalLayout;

namespace Premium {

[[nodiscard]] QString Svg();
[[nodiscard]] QByteArray ColorizedSvg(const QGradientStops &gradientStops);

[[nodiscard]] QGradientStops ButtonGradientStops();
[[nodiscard]] QGradientStops GiftGradientStops();

[[nodiscard]] QLinearGradient ComputeGradient(
	not_null<QWidget*> content,
	int left,
	int width);

} // namespace Premium
} // namespace Ui
