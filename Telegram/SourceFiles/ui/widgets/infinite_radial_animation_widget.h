/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {
struct InfiniteRadialAnimation;
struct TextStyle;
} // namespace style

namespace Ui {

class RpWidget;

// LoogriGram: these lived in boost_badge.h under the giveaway statistics,
// in Info::Statistics, though nothing about them is a boost or a giveaway.

[[nodiscard]] not_null<Ui::RpWidget*> InfiniteRadialAnimationWidget(
	not_null<Ui::RpWidget*> parent,
	int size,
	const style::InfiniteRadialAnimation *st = nullptr);

void AddChildToWidgetCenter(
	not_null<Ui::RpWidget*> parent,
	not_null<Ui::RpWidget*> child);

} // namespace Ui
