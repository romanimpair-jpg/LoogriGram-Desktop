/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/boost_box.h"

#include "info/profile/info_profile_icon.h"
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/fireworks_animation.h"
#include "ui/effects/premium_bubble.h"
#include "ui/effects/premium_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/painter.h"
#include "ui/round_rect.h"
#include "ui/widgets/labels.h"
#include "ui/rect.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Ui {
namespace {

[[nodiscard]] BoostCounters AdjustByReached(BoostCounters data) {
	const auto exact = (data.boosts == data.thisLevelBoosts);
	const auto reached = !data.nextLevelBoosts || (exact && data.mine > 0);
	if (reached) {
		--data.level;
		data.boosts = data.nextLevelBoosts = std::max({
			data.boosts,
			data.thisLevelBoosts,
			1
		});
		data.thisLevelBoosts = 0;
	} else {
		data.boosts = std::max(data.thisLevelBoosts, data.boosts);
		data.nextLevelBoosts = std::max(
			data.nextLevelBoosts,
			data.boosts + 1);
	}
	return data;
}

// LoogriGram: MakeTitle drew the boost screen's "Level N" header and had
// no other caller.

[[nodiscard]] object_ptr<Ui::FlatLabel> MakeFeaturesBadge(
		not_null<QWidget*> parent,
		rpl::producer<QString> text) {
	return MakeBoostFeaturesBadge(parent, std::move(text), [](QRect rect) {
		auto gradient = QLinearGradient(
			rect.topLeft(),
			rect.topRight());
		gradient.setStops(Ui::Premium::GiftGradientStops());
		return QBrush(gradient);
	});
}

void AddFeaturesList(
		not_null<Ui::VerticalLayout*> container,
		const Ui::BoostFeatures &features,
		int startFromLevel,
		bool group) {
	const auto add = [&](
			rpl::producer<TextWithEntities> text,
			const style::icon &st) {
		const auto label = container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				std::move(text),
				st::boostFeatureLabel),
			st::boostFeaturePadding);
		object_ptr<Info::Profile::FloatingIcon>(
			label,
			st,
			st::boostFeatureIconPosition);
	};
	const auto lowMax = std::max({
		features.linkLogoLevel,
		features.profileIconLevel,
		features.autotranslateLevel,
		features.transcribeLevel,
		features.emojiPackLevel,
		features.emojiStatusLevel,
		features.wallpaperLevel,
		features.customWallpaperLevel,
		(features.nameColorsByLevel.empty()
			? 0
			: features.nameColorsByLevel.back().first),
		(features.linkStylesByLevel.empty()
			? 0
			: features.linkStylesByLevel.back().first),
		(features.profileColorsByLevel.empty()
			? 0
			: features.profileColorsByLevel.back().first),
	});
	const auto highMax = std::max(lowMax, features.sponsoredLevel);
	auto nameColors = 0;
	auto linkStyles = 0;
	auto profileColors = 0;
	for (auto i = std::max(startFromLevel, 1); i <= highMax; ++i) {
		if ((i > lowMax) && (i < highMax)) {
			continue;
		}
		const auto unlocks = (i == startFromLevel);
		{
			const auto badge = container->add(
				MakeFeaturesBadge(
					container,
					(unlocks
						? tr::lng_boost_level_unlocks
						: tr::lng_boost_level)(
							lt_count,
							rpl::single(float64(i)))),
				st::boostLevelBadgePadding,
				style::al_top);
			const auto padding = st::boxRowPadding;
			const auto line = Ui::CreateChild<Ui::RpWidget>(container);
			badge->geometryValue() | rpl::on_next([=](const QRect &r) {
				line->setGeometry(
					padding.left(),
					r.y(),
					container->width() - rect::m::sum::h(padding),
					r.height());
			}, line->lifetime());
			const auto shift = st::lineWidth * 10;
			line->paintRequest() | rpl::on_next([=] {
				auto p = QPainter(line);
				p.setPen(st::windowSubTextFg);
				const auto y = line->height() / 2;
				const auto left = badge->x() - shift - padding.left();
				const auto right = left + badge->width() + shift * 2;
				if (left > 0) {
					p.drawLine(0, y, left, y);
				}
				if (right < line->width()) {
					p.drawLine(right, y, line->width(), y);
				}
			}, line->lifetime());
		}
		if (i >= features.sponsoredLevel) {
			add(
				tr::lng_channel_earn_off(tr::rich),
				st::boostFeatureOffSponsored);
		}
		if (i >= features.customWallpaperLevel) {
			add(
				(group
					? tr::lng_feature_custom_background_group
					: tr::lng_feature_custom_background_channel)(tr::rich),
				st::boostFeatureCustomBackground);
		}
		if (i >= features.wallpaperLevel) {
			add(
				(group
					? tr::lng_feature_backgrounds_group
					: tr::lng_feature_backgrounds_channel)(
						lt_count,
						rpl::single(float64(features.wallpapersCount)),
						tr::rich),
				st::boostFeatureBackground);
		}
		if (i >= features.emojiStatusLevel) {
			add(
				tr::lng_feature_emoji_status(tr::rich),
				st::boostFeatureEmojiStatus);
		}
		if (const auto j = features.profileColorsByLevel.find(i)
			; j != end(features.profileColorsByLevel)) {
			profileColors += j->second;
		}
		if (i >= features.profileIconLevel) {
			add(
				(group
					? tr::lng_feature_profile_icon_group
					: tr::lng_feature_profile_icon_channel)(tr::rich),
				st::boostFeatureProfileIcon);
		}
		if (profileColors > 0) {
			add((group
				? tr::lng_feature_profile_color_group
				: tr::lng_feature_profile_color_channel)(
					lt_count,
					rpl::single(float64(profileColors)),
					tr::rich
				), st::boostFeatureProfileColor);
		}
		if (!group) {
			if (const auto j = features.linkStylesByLevel.find(i)
				; j != end(features.linkStylesByLevel)) {
				linkStyles += j->second;
			}
			if (i >= features.linkLogoLevel) {
				add(
					tr::lng_feature_link_emoji(tr::rich),
					st::boostFeatureCustomLink);
			}
			if (linkStyles > 0) {
				add(tr::lng_feature_link_style_channel(
					lt_count,
					rpl::single(float64(linkStyles)),
					tr::rich
				), st::boostFeatureLink);
			}
			if (const auto j = features.nameColorsByLevel.find(i)
				; j != end(features.nameColorsByLevel)) {
				nameColors += j->second;
			}
			if (nameColors > 0) {
				add(tr::lng_feature_name_color_channel(
					lt_count,
					rpl::single(float64(nameColors)),
					tr::rich
				), st::boostFeatureName);
			}
			add(tr::lng_feature_reactions(
				lt_count,
				rpl::single(float64(i)),
				tr::rich
			), st::boostFeatureCustomReactions);
		}
		add(
			tr::lng_feature_stories(lt_count, rpl::single(1. * i), tr::rich),
			st::boostFeatureStories);
		if (!group && i >= features.autotranslateLevel) {
			add(
				tr::lng_feature_autotranslate(tr::rich),
				st::boostFeatureAutoTranslate);
		}
		if (group && i >= features.transcribeLevel) {
			add(
				tr::lng_feature_transcribe(tr::rich),
				st::boostFeatureTranscribe);
		}
		if (group && i >= features.emojiPackLevel) {
			add(
				tr::lng_feature_custom_emoji_pack(tr::rich),
				st::boostFeatureCustomEmoji);
		}
	}
}

} // namespace

void StartFireworks(not_null<QWidget*> parent) {
	const auto result = Ui::CreateChild<RpWidget>(parent.get());
	result->setAttribute(Qt::WA_TransparentForMouseEvents);
	result->setGeometry(parent->rect());
	result->show();

	auto &lifetime = result->lifetime();
	const auto animation = lifetime.make_state<FireworksAnimation>([=] {
		result->update();
	});
	result->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(result);
		if (!animation->paint(p, result->rect())) {
			crl::on_main(result, [=] { delete result; });
		}
	}, lifetime);
}

// LoogriGram: five boxes stood here, and all five existed to move a
// Telegram Premium subscription somewhere. BoostBox was the boost screen
// itself; BoostBoxAlready, GiftForBoostsBox and GiftedNoBoostsBox were the
// three ways of saying no slot was available, two of them offering to gift
// a subscription to get more; PremiumForBoostsBox asked outright whether to
// subscribe. AskBoostBox below is a different thing and stays - it tells a
// channel's own admin what level their channel needs for a feature.

// LoogriGram: restored after the credits deletion took it with the gift
// code boxes. AskBoostBox, which is kept, shows the boost link in it.
object_ptr<Ui::RpWidget> MakeLinkLabel(
		not_null<QWidget*> parent,
		rpl::producer<QString> text,
		rpl::producer<QString> link,
		std::shared_ptr<Ui::Show> show,
		object_ptr<Ui::RpWidget> right) {
	auto result = object_ptr<Ui::AbstractButton>(parent);
	const auto raw = result.data();

	const auto rawRight = right.release();
	if (rawRight) {
		rawRight->setParent(raw);
		rawRight->show();
	}

	struct State {
		State(
			not_null<QWidget*> parent,
			rpl::producer<QString> value,
			rpl::producer<QString> link)
		: text(std::move(value))
		, link(std::move(link))
		, label(parent, text.value(), st::giveawayGiftCodeLink)
		, bg(st::roundRadiusLarge, st::windowBgOver) {
		}

		rpl::variable<QString> text;
		rpl::variable<QString> link;
		Ui::FlatLabel label;
		Ui::RoundRect bg;
	};

	const auto state = raw->lifetime().make_state<State>(
		raw,
		rpl::duplicate(text),
		std::move(link));
	state->label.setSelectable(true);

	rpl::combine(
		raw->widthValue(),
		std::move(text)
	) | rpl::on_next([=](int outer, const auto&) {
		const auto textWidth = state->label.textMaxWidth();
		const auto skipLeft = st::giveawayGiftCodeLink.margin.left();
		const auto skipRight = rawRight
			? rawRight->width()
			: st::giveawayGiftCodeLink.margin.right();
		const auto available = outer - skipRight - skipLeft;
		const auto use = std::min(textWidth, available);
		state->label.resizeToWidth(use);
		const auto forCenter = (outer - use) / 2;
		const auto x = (forCenter < skipLeft)
			? skipLeft
			: (forCenter > outer - skipRight - use)
			? (outer - skipRight - use)
			: forCenter;
		state->label.moveToLeft(x, st::giveawayGiftCodeLink.margin.top());
	}, raw->lifetime());

	raw->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(raw);
		state->bg.paint(p, raw->rect());
	}, raw->lifetime());

	state->label.setAttribute(Qt::WA_TransparentForMouseEvents);

	raw->resize(raw->width(), st::giveawayGiftCodeLinkHeight);
	if (rawRight) {
		raw->widthValue() | rpl::on_next([=](int width) {
			rawRight->move(width - rawRight->width(), 0);
		}, raw->lifetime());
	}
	raw->setClickedCallback([=] {
		QGuiApplication::clipboard()->setText(state->link.current());
		show->showToast({
			.text = { tr::lng_username_copied(tr::now) },
			.iconLottie = u"toast/voip_invite"_q,
			.iconLottieSize = st::toastLottieIconSize,
		});
	});

	return result;
}

void AskBoostBox(
		not_null<GenericBox*> box,
		AskBoostBoxData data,
		Fn<void()> openStatistics,
		Fn<void()> startGiveaway) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::boostBox);
	box->setNoContentMargin(true);
	box->addSkip(st::boxRowPadding.left());

	FillBoostLimit(
		BoxShowFinishes(box),
		box->verticalLayout(),
		rpl::single(data.boost),
		st::boxRowPadding);

	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	auto title = v::match(data.reason.data, [](AskBoostChannelColor) {
		return tr::lng_boost_channel_title_color();
	}, [](AskBoostAutotranslate) {
		return tr::lng_boost_channel_title_autotranslate();
	}, [](AskBoostWallpaper) {
		return tr::lng_boost_channel_title_wallpaper();
	}, [](AskBoostEmojiStatus) {
		return tr::lng_boost_channel_title_status();
	}, [](AskBoostEmojiPack) {
		return tr::lng_boost_group_title_emoji();
	}, [](AskBoostCustomReactions) {
		return tr::lng_boost_channel_title_reactions();
	}, [](AskBoostCpm) {
		return tr::lng_boost_channel_title_cpm();
	}, [](AskBoostWearCollectible) {
		return tr::lng_boost_channel_title_wear();
	});
	auto isGroup = false;
	auto reasonText = v::match(data.reason.data, [&](
			AskBoostChannelColor data) {
		return tr::lng_boost_channel_needs_level_color(
			lt_count,
			rpl::single(float64(data.requiredLevel)),
			tr::rich);
	}, [&](AskBoostAutotranslate data) {
		return tr::lng_boost_channel_needs_level_autotranslate(
			lt_count,
			rpl::single(float64(data.requiredLevel)),
			tr::rich);
	}, [&](AskBoostWallpaper data) {
		isGroup = data.group;
		return (data.group
			? tr::lng_boost_group_needs_level_wallpaper
			: tr::lng_boost_channel_needs_level_wallpaper)(
				lt_count,
				rpl::single(float64(data.requiredLevel)),
				tr::rich);
	}, [&](AskBoostEmojiStatus data) {
		isGroup = data.group;
		return (data.group
			? tr::lng_boost_group_needs_level_status
			: tr::lng_boost_channel_needs_level_status)(
				lt_count,
				rpl::single(float64(data.requiredLevel)),
				tr::rich);
	}, [&](AskBoostEmojiPack data) {
		isGroup = true;
		return tr::lng_boost_group_needs_level_emoji(
			lt_count,
			rpl::single(float64(data.requiredLevel)),
			tr::rich);
	}, [&](AskBoostCustomReactions data) {
		return tr::lng_boost_channel_needs_level_reactions(
			lt_count,
			rpl::single(float64(data.count)),
			lt_same_count,
			rpl::single(TextWithEntities{ QString::number(data.count) }),
			tr::rich);
	}, [&](AskBoostCpm data) {
		return tr::lng_boost_channel_needs_level_cpm(
			lt_count,
			rpl::single(float64(data.requiredLevel)),
			tr::rich);
	}, [&](AskBoostWearCollectible data) {
		return tr::lng_boost_channel_needs_level_wear(
			lt_count,
			rpl::single(float64(data.requiredLevel)),
			tr::rich);
	});
	auto text = rpl::combine(
		std::move(reasonText),
		(isGroup ? tr::lng_boost_group_ask : tr::lng_boost_channel_ask)(
			tr::rich)
	) | rpl::map([](TextWithEntities &&text, TextWithEntities &&ask) {
		return text.append(u"\n\n"_q).append(std::move(ask));
	});
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(title),
			st::boostCenteredTitle),
		st::boxRowPadding + QMargins(0, st::boostTitleSkip, 0, 0),
		style::al_top);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(text),
			st::boostText),
		(st::boxRowPadding
			+ QMargins(0, st::boostTextSkip, 0, st::boostBottomSkip)),
		style::al_top);

	auto stats = object_ptr<Ui::IconButton>(box, st::boostLinkStatsButton);
	stats->setClickedCallback(openStatistics);
	box->addRow(MakeLinkLabel(
		box,
		rpl::single(data.link),
		rpl::single(data.link),
		box->uiShow(),
		std::move(stats)));

	AddFeaturesList(
		box->verticalLayout(),
		data.features,
		data.boost.level + (data.boost.nextLevelBoosts ? 1 : 0),
		data.group);

	auto submit = tr::lng_boost_channel_ask_button();
	box->addButton(rpl::duplicate(submit), [=] {
		QGuiApplication::clipboard()->setText(data.link);
		box->uiShow()->showToast({
			.text = { tr::lng_username_copied(tr::now) },
			.iconLottie = u"toast/voip_invite"_q,
			.iconLottieSize = st::toastLottieIconSize,
		});
	});
}

void FillBoostLimit(
		rpl::producer<> showFinished,
		not_null<VerticalLayout*> container,
		rpl::producer<BoostCounters> data,
		style::margins limitLinePadding) {
	const auto addSkip = [&](int skip) {
		container->add(object_ptr<Ui::FixedHeightWidget>(container, skip));
	};

	const auto ratio = [=](BoostCounters counters) {
		const auto min = counters.thisLevelBoosts;
		const auto max = counters.nextLevelBoosts;

		Assert(counters.boosts >= min && counters.boosts <= max);
		const auto count = (max - min);
		const auto index = (counters.boosts - min);
		if (!index) {
			return 0.;
		} else if (index == count) {
			return 1.;
		} else if (count == 2) {
			return 0.5;
		}
		const auto available = st::boxWideWidth
			- st::boxPadding.left()
			- st::boxPadding.right();
		const auto average = available / float64(count);
		const auto levelWidth = [&](int add) {
			return st::normalFont->width(
				tr::lng_boost_level(
					tr::now,
					lt_count,
					counters.level + add));
		};
		const auto paddings = 2 * st::premiumLineTextSkip;
		const auto labelLeftWidth = paddings + levelWidth(0);
		const auto labelRightWidth = paddings + levelWidth(1);
		const auto first = std::max(average, labelLeftWidth * 1.);
		const auto last = std::max(average, labelRightWidth * 1.);
		const auto other = (available - first - last) / (count - 2);
		return (first + (index - 1) * other) / available;
	};

	auto adjustedData = rpl::duplicate(data) | rpl::map(AdjustByReached);

	auto bubbleRowState = rpl::duplicate(
		adjustedData
	) | rpl::combine_previous(
		BoostCounters()
	) | rpl::map([=](BoostCounters previous, BoostCounters counters) {
		return Premium::BubbleRowState{
			.counter = counters.boosts,
			.ratio = ratio(counters),
			.animateFromZero = (counters.level != previous.level),
			.dynamic = true,
		};
	});
	Premium::AddBubbleRow(
		container,
		st::boostBubble,
		std::move(showFinished),
		rpl::duplicate(bubbleRowState),
		Premium::BubbleType::Premium,
		nullptr,
		&st::premiumIconBoost,
		limitLinePadding);
	addSkip(st::premiumLineTextSkip);

	const auto level = [](int level) {
		return tr::lng_boost_level(tr::now, lt_count, level);
	};
	auto limitState = std::move(
		bubbleRowState
	) | rpl::map([](const Premium::BubbleRowState &state) {
		return Premium::LimitRowState{
			.ratio = state.ratio,
			.animateFromZero = state.animateFromZero,
			.dynamic = state.dynamic
		};
	});
	auto left = rpl::duplicate(
		adjustedData
	) | rpl::map([=](BoostCounters counters) {
		return level(counters.level);
	});
	auto right = rpl::duplicate(
		adjustedData
	) | rpl::map([=](BoostCounters counters) {
		return level(counters.level + 1);
	});
	Premium::AddLimitRow(
		container,
		st::boostLimits,
		Premium::LimitRowLabels{
			.leftLabel = std::move(left),
			.rightLabel = std::move(right),
		},
		std::move(limitState),
		limitLinePadding);
}

object_ptr<Ui::FlatLabel> MakeBoostFeaturesBadge(
		not_null<QWidget*> parent,
		rpl::producer<QString> text,
		Fn<QBrush(QRect)> bg) {
	auto result = object_ptr<Ui::FlatLabel>(
		parent,
		std::move(text),
		st::boostLevelBadge);
	const auto label = result.data();

	label->show();
	label->paintRequest() | rpl::on_next([=] {
		const auto size = label->textMaxWidth();
		const auto rect = QRect(
			(label->width() - size) / 2,
			st::boostLevelBadge.margin.top(),
			size,
			st::boostLevelBadge.style.font->height
		).marginsAdded(st::boostLevelBadge.margin);
		auto p = QPainter(label);
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(bg(rect));
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(rect, rect.height() / 2., rect.height() / 2.);

		const auto &lineFg = st::windowBgRipple;
		const auto line = st::boostLevelBadgeLine;
		const auto top = st::boostLevelBadge.margin.top()
			+ ((st::boostLevelBadge.style.font->height - line) / 2);
		const auto left = 0;
		const auto skip = st::boostLevelBadgeSkip;
		if (const auto right = rect.x() - skip; right > left) {
			p.fillRect(left, top, right - left, line, lineFg);
		}
		const auto right = label->width();
		if (const auto left = rect.x() + rect.width() + skip
			; left < right) {
			p.fillRect(left, top, right - left, line, lineFg);
		}
	}, label->lifetime());

	return result;
}

} // namespace Ui
