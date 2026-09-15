/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace style {
struct MediaSlider;
} // namespace style

namespace Main {
class Session;
} // namespace Main

namespace Ui::Premium {
class BubbleWidget;
} // namespace Ui::Premium

namespace Ui {

class AbstractButton;
class BoxContent;
class GenericBox;
class RpWidget;
class VerticalLayout;

// LoogriGram: the paid reaction box and its top-payers leaderboard were
// declared here. Deleted; what remains is the amount-of-stars picker they
// were built from, still used by the gift auction.

struct StarSelectDiscreter {
	Fn<int(float64)> ratioToValue;
	Fn<float64(int)> valueToRatio;
};

[[nodiscard]] StarSelectDiscreter StarSelectDiscreterForMax(int max);

void PaidReactionSlider(
	not_null<VerticalLayout*> container,
	const style::MediaSlider &st,
	int min,
	int explicitlyAllowed,
	rpl::producer<int> current,
	int max,
	Fn<void(int)> changed,
	Fn<QColor(int)> activeFgOverride = nullptr);

void AddStarSelectBalance(
	not_null<GenericBox*> box,
	not_null<Main::Session*> session,
	rpl::producer<CreditsAmount> balanceValue,
	bool dark = false);

not_null<Premium::BubbleWidget*> AddStarSelectBubble(
	not_null<VerticalLayout*> container,
	rpl::producer<> showFinishes,
	rpl::producer<int> value,
	int max,
	Fn<QColor(int)> activeFgOverride = nullptr);

struct StarSelectInfoBlock {
	rpl::producer<TextWithEntities> title;
	rpl::producer<QString> subtext;
	Fn<void()> click;
};
[[nodiscard]] object_ptr<RpWidget> MakeStarSelectInfoBlocks(
	not_null<RpWidget*> parent,
	std::vector<StarSelectInfoBlock> blocks,
	Text::MarkedContext context,
	bool dark = false);

} // namespace Ui
