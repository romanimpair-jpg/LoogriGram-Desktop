/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_reaction_box.h"

#include "base/qt/qt_compare.h"
#include "calls/group/ui/calls_group_stars_coloring.h"
#include "lang/lang_keys.h"
#include "ui/boxes/boost_box.h" // MakeBoostFeaturesBadge.
#include "ui/controls/who_reacted_context_action.h"
#include "ui/effects/premium_bubble.h"
#include "ui/effects/ministar_particles.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/color_int_conversion.h"
#include "ui/dynamic_image.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "styles/style_calls.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_info_levels.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"

namespace Settings {
[[nodiscard]] not_null<Ui::RpWidget*> AddBalanceWidget(
	not_null<Ui::RpWidget*> parent,
	not_null<Main::Session*> session,
	rpl::producer<CreditsAmount> balanceValue,
	bool rightAlign,
	rpl::producer<float64> opacityValue = nullptr,
	bool dark = false);
} // namespace Settings

namespace Ui {

// LoogriGram: a box for buying paid reactions with stars stood here, with
// its leaderboard of top payers and the badge it drew for each. Deleted.
// What is left below is the amount-of-stars picker it was built from,
// which the gift auction still uses.

StarSelectDiscreter StarSelectDiscreterForMax(int max) {
	Expects(max >= 2);

	// 1/8 of width is 1..10
	// 1/3 of width is 1..100
	// 2/3 of width is 1..1000

	auto thresholds = base::flat_map<float64, int>();
	thresholds.emplace(0., 1);
	if (max <= 40) {
		thresholds.emplace(1., max);
	} else if (max <= 300) {
		thresholds.emplace(1. / 4, 10);
		thresholds.emplace(1., max);
	} else if (max <= 600) {
		thresholds.emplace(1. / 8, 10);
		thresholds.emplace(1. / 2, 100);
		thresholds.emplace(1., max);
	} else if (max <= 1900) {
		thresholds.emplace(1. / 8, 10);
		thresholds.emplace(1. / 3, 100);
		thresholds.emplace(1., max);
	} else if (max <= 10000) {
		thresholds.emplace(1. / 8, 10);
		thresholds.emplace(1. / 3, 100);
		thresholds.emplace(2. / 3, 1000);
		thresholds.emplace(1., max);
	} else {
		thresholds.emplace(1. / 10, 10);
		thresholds.emplace(1. / 6, 100);
		thresholds.emplace(1. / 3, 1000);
		thresholds.emplace(1., max);
	}

	const auto ratioToValue = [=](float64 ratio) {
		ratio = std::clamp(ratio, 0., 1.);
		const auto j = thresholds.lower_bound(ratio);
		if (j == begin(thresholds)) {
			return 1;
		}
		const auto i = j - 1;
		const auto progress = (ratio - i->first) / (j->first - i->first);
		const auto value = i->second + (j->second - i->second) * progress;
		return int(base::SafeRound(value));
	};
	const auto valueToRatio = [=](int value) {
		value = std::clamp(value, 1, max);
		auto i = begin(thresholds);
		auto j = i + 1;
		while (j->second < value) {
			i = j++;
		}
		const auto progress = (value - i->second)
			/ float64(j->second - i->second);
		return i->first + (j->first - i->first) * progress;
	};
	return {
		.ratioToValue = ratioToValue,
		.valueToRatio = valueToRatio,
	};
}

void PaidReactionSlider(
		not_null<VerticalLayout*> container,
		const style::MediaSlider &st,
		int min,
		int explicitlyAllowed,
		rpl::producer<int> current,
		int max,
		Fn<void(int)> changed,
		Fn<QColor(int)> activeFgOverride) {
	Expects(explicitlyAllowed <= max);

	if (!explicitlyAllowed) {
		explicitlyAllowed = min;
	}
	const auto slider = container->add(
		object_ptr<MediaSlider>(container, st),
		st::boxRowPadding + QMargins(0, st::paidReactSliderTop, 0, 0));
	slider->resize(slider->width(), st::paidReactSlider.seekSize.height());

	const auto update = [=](int count) {
		if (activeFgOverride) {
			const auto color = activeFgOverride(count);
			slider->setColorOverrides({
				.activeBg = color,
				.activeBorder = color,
				.seekFg = st::groupCallMembersFg->c,
				.seekBorder = color,
				.inactiveBorder = Qt::transparent,
			});
		}
	};

	const auto discreter = StarSelectDiscreterForMax(max);
	slider->setAlwaysDisplayMarker(true);
	slider->setDirection(ContinuousSlider::Direction::Horizontal);

	const auto ratioToValue = [=](float64 ratio) {
		const auto value = discreter.ratioToValue(ratio);
		return (value <= explicitlyAllowed && explicitlyAllowed < min)
			? explicitlyAllowed
			: std::max(value, min);
	};

	std::move(current) | rpl::on_next([=](int value) {
		value = std::clamp(value, 1, max);
		if (discreter.ratioToValue(slider->value()) != value) {
			slider->setValue(discreter.valueToRatio(value));
			update(value);
		}
	}, slider->lifetime());

	slider->setAdjustCallback([=](float64 ratio) {
		return discreter.valueToRatio(ratioToValue(ratio));
	});
	const auto callback = [=](float64 ratio) {
		const auto value = ratioToValue(ratio);
		update(value);
		changed(value);
	};
	slider->setChangeProgressCallback(callback);
	slider->setChangeFinishedCallback(callback);



	struct State {
		StarParticles particles = StarParticles(
			StarParticles::Type::Right,
			200,
			st::lineWidth * 7);
		Ui::Animations::Basic animation;
	};
	const auto state = slider->lifetime().make_state<State>();

	const auto stars = Ui::CreateChild<Ui::RpWidget>(slider->parentWidget());
	stars->show();
	stars->raise();
	slider->geometryValue() | rpl::on_next([=](QRect rect) {
		stars->setGeometry(rect);
	}, stars->lifetime());

	state->animation.init([=] { stars->update(); });
	stars->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto seekSize = st::paidReactSlider.seekSize.width();
	const auto seekRadius = seekSize / 2.;
	stars->paintRequest() | rpl::on_next([=] {
		if (!state->animation.animating()) {
			state->animation.start();
		}
		auto p = QPainter(stars);
		auto hq = PainterHighQualityEnabler(p);
		const auto progress = slider->value();
		const auto rect = stars->rect();
		const auto availableWidth = rect.width() - seekSize;
		const auto seekCenter = seekRadius + availableWidth * progress;

		state->particles.setSpeed(.1 + progress * .3);
		state->particles.setVisible(.25 + .65 * progress);

		auto fullPath = QPainterPath();
		fullPath.addRoundedRect(QRectF(rect), seekRadius, seekRadius);
		auto circlePath = QPainterPath();
		circlePath.addEllipse(
			QPointF(seekCenter, rect.height() / 2.),
			seekRadius,
			seekRadius);
		auto rightRect = QPainterPath();
		rightRect.addRect(
			QRectF(seekCenter, 0, rect.width() - seekCenter, rect.height()));

		p.setClipPath(fullPath.subtracted(circlePath));
		state->particles.setColor(Qt::white);
		state->particles.paint(p, rect, crl::now(), false);
		p.setClipping(false);

		p.setClipPath(fullPath.intersected(circlePath.united(rightRect)));
		state->particles.setColor(activeFgOverride
			? st::groupCallMemberInactiveIcon->c
			: st::creditsBg3->c);
		state->particles.paint(p, rect, crl::now(), false);
	}, stars->lifetime());
}

void AddStarSelectBalance(
		not_null<GenericBox*> box,
		not_null<Main::Session*> session,
		rpl::producer<CreditsAmount> balanceValue,
		bool dark) {
	const auto balance = Settings::AddBalanceWidget(
		box->verticalLayout(),
		session,
		std::move(balanceValue),
		false,
		nullptr,
		dark);
	rpl::combine(
		balance->sizeValue(),
		box->widthValue()
	) | rpl::on_next([=] {
		balance->moveToLeft(
			st::creditsHistoryRightSkip * 2,
			st::creditsHistoryRightSkip);
		balance->update();
	}, balance->lifetime());
}

not_null<Premium::BubbleWidget*> AddStarSelectBubble(
		not_null<VerticalLayout*> container,
		rpl::producer<> showFinishes,
		rpl::producer<int> value,
		int max,
		Fn<QColor(int)> activeFgOverride) {
	const auto valueToRatio = StarSelectDiscreterForMax(max).valueToRatio;
	auto bubbleRowState = rpl::duplicate(value) | rpl::map([=](int value) {
		const auto full = st::boxWideWidth
			- st::boxRowPadding.left()
			- st::boxRowPadding.right();
		const auto marker = st::paidReactSlider.seekSize.width();
		const auto start = marker / 2;
		const auto inner = full - marker;
		const auto correct = start + inner * valueToRatio(value);
		return Premium::BubbleRowState{
			.counter = value,
			.ratio = correct / full,
		};
	});

	const auto bubble = Premium::AddBubbleRow(
		container,
		st::boostBubble,
		std::move(showFinishes),
		std::move(bubbleRowState),
		Premium::BubbleType::Credits,
		nullptr,
		&st::paidReactBubbleIcon,
		st::boxRowPadding);
	bubble->show();
	if (activeFgOverride) {
		std::move(value) | rpl::on_next([=](int count) {
			bubble->setBrushOverride(activeFgOverride(count));
		}, bubble->lifetime());
	}
	return bubble;
}

object_ptr<RpWidget> MakeStarSelectInfoBlocks(
		not_null<RpWidget*> parent,
		std::vector<StarSelectInfoBlock> blocks,
		Text::MarkedContext context,
		bool dark) {
	Expects(!blocks.empty());

	auto result = object_ptr<RpWidget>(parent.get());
	const auto raw = result.data();

	struct State {
		std::vector<not_null<RpWidget*>> blocks;
	};
	const auto state = raw->lifetime().make_state<State>();

	for (auto &info : blocks) {
		state->blocks.push_back(MakeStarSelectInfoBlock(
			raw,
			std::move(info.title),
			std::move(info.subtext),
			std::move(info.click),
			context,
			dark));
	}
	raw->resize(raw->width(), state->blocks.front()->height());
	raw->widthValue() | rpl::on_next([=](int width) {
		const auto count = int(state->blocks.size());
		const auto skip = (st::boxRowPadding.left() / 2);
		const auto single = (width - skip * (count - 1)) / float64(count);
		if (single < 1.) {
			return;
		}
		auto x = 0.;
		const auto w = int(base::SafeRound(single));
		for (const auto &block : state->blocks) {
			block->resizeToWidth(w);
			block->moveToLeft(int(base::SafeRound(x)), 0);
			x += single + skip;
		}
	}, raw->lifetime());

	return result;
}

} // namespace Ui
