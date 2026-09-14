/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/replace_boost_box.h"

#include "api/api_peer_colors.h"
#include "apiwrap.h"
#include "base/event_filter.h"
#include "base/unixtime.h"
#include "boxes/peer_list_box.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_premium_limits.h"
#include "data/data_channel.h"
#include "data/data_cloud_themes.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "main/session/session_show.h"
#include "ui/boxes/boost_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/chat_style.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/premium_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/empty_userpic.h"
#include "ui/dynamic_image.h"
#include "ui/painter.h"
#include "ui/top_background_gradient.h"
#include "styles/style_boxes.h"
#include "styles/style_credits.h"
#include "styles/style_premium.h"

// LoogriGram: the boost-reassignment machinery lived here - a peer list of
// the channels currently holding your Premium boost slots, so one could be
// taken back and spent somewhere else, plus the flood and single-slot
// confirmations, and it was the whole of this file's anonymous namespace.
// Giving a boost means spending a subscription slot, so the flow is gone.
// What is left has nothing to do with it: reading a channel's boost level,
// which the channel-owner screens report, and the userpic-row helpers a
// dozen unrelated callers use.

Ui::BoostCounters ParseBoostCounters(
		const MTPpremium_BoostsStatus &status) {
	const auto &data = status.data();
	const auto slots = data.vmy_boost_slots();
	return {
		.level = data.vlevel().v,
		.boosts = data.vboosts().v,
		.thisLevelBoosts = data.vcurrent_level_boosts().v,
		.nextLevelBoosts = data.vnext_level_boosts().value_or_empty(),
		.mine = slots ? int(slots->v.size()) : 0,
	};
}

Ui::BoostFeatures LookupBoostFeatures(not_null<ChannelData*> channel) {
	auto nameColorsByLevel = base::flat_map<int, int>();
	auto linkStylesByLevel = base::flat_map<int, int>();
	auto profileColorsByLevel = base::flat_map<int, int>();
	const auto group = channel->isMegagroup();
	const auto peerColors = &channel->session().api().peerColors();
	const auto &list = group
		? peerColors->requiredLevelsGroup()
		: peerColors->requiredLevelsChannel();
	const auto indices = peerColors->indicesCurrent();
	for (const auto &[index, level] : list) {
		if (!Ui::ColorPatternIndex(indices, index, false)) {
			++nameColorsByLevel[level];
		}
		++linkStylesByLevel[level];
	}
	{
		const auto profileIndices = peerColors->profileColorIndices();
		auto lowestNonZeroLevel = std::numeric_limits<int>::max();
		auto levels = std::vector<int>();
		levels.reserve(profileIndices.size());

		for (const auto index : profileIndices) {
			const auto level = peerColors->requiredLevelFor(
				channel->id,
				index,
				group,
				true);
			levels.push_back(level);
			if (level) {
				lowestNonZeroLevel = std::min(lowestNonZeroLevel, level);
			}
		}

		for (const auto level : levels) {
			++profileColorsByLevel[std::max(level, lowestNonZeroLevel)];
		}
	}

	const auto &themes = channel->owner().cloudThemes().chatThemes();
	if (themes.empty()) {
		channel->owner().cloudThemes().refreshChatThemes();
	}
	const auto levelLimits = Data::LevelLimits(&channel->session());
	return Ui::BoostFeatures{
		.nameColorsByLevel = std::move(nameColorsByLevel),
		.linkStylesByLevel = std::move(linkStylesByLevel),
		.profileColorsByLevel = std::move(profileColorsByLevel),
		.linkLogoLevel = group ? 0 : levelLimits.channelBgIconLevelMin(),
		.profileIconLevel = group
			? levelLimits.groupProfileBgIconLevelMin()
			: levelLimits.channelProfileBgIconLevelMin(),
		.autotranslateLevel = group ? 0 : levelLimits.channelAutoTranslateLevelMin(),
		.transcribeLevel = group ? levelLimits.groupTranscribeLevelMin() : 0,
		.emojiPackLevel = group ? levelLimits.groupEmojiStickersLevelMin() : 0,
		.emojiStatusLevel = group
			? levelLimits.groupEmojiStatusLevelMin()
			: levelLimits.channelEmojiStatusLevelMin(),
		.wallpaperLevel = group
			? levelLimits.groupWallpaperLevelMin()
			: levelLimits.channelWallpaperLevelMin(),
		.wallpapersCount = themes.empty() ? 8 : int(themes.size()),
		.customWallpaperLevel = group
			? levelLimits.groupCustomWallpaperLevelMin()
			: levelLimits.channelCustomWallpaperLevelMin(),
		.sponsoredLevel = levelLimits.channelRestrictSponsoredLevelMin(),
	};
}

object_ptr<Ui::RpWidget> CreateUserpicsTransfer(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<std::vector<not_null<PeerData*>>> from,
		not_null<PeerData*> to,
		UserpicsTransferType type) {
	using Type = UserpicsTransferType;
	struct State {
		std::vector<not_null<PeerData*>> from;
		std::vector<std::unique_ptr<Ui::UserpicButton>> buttons;
		QImage layer;
		rpl::variable<int> count = 0;
		bool painting = false;
	};
	const auto st = &st::boostReplaceUserpicsRow;
	const auto full = st->button.size.height()
		+ st::boostReplaceIconAdd.y()
		+ st::lineWidth;
	auto result = object_ptr<Ui::FixedHeightWidget>(parent, full);
	const auto raw = result.data();
	const auto right = CreateChild<Ui::UserpicButton>(raw, to, st->button);
	const auto overlay = CreateChild<Ui::RpWidget>(raw);
	const auto drawCornerPeer = (type == Type::ChannelFutureOwner)
		? [&]() -> PaintRoundImageCallback {
			using Peers = std::vector<not_null<PeerData*>>;
			const auto snapshot = rpl::variable<Peers>(
				rpl::duplicate(from)).current();
			if (snapshot.size() == 2) {
				return ForceRoundUserpicCallback(snapshot[1].get());
			}
			return nullptr;
		}()
		: (PaintRoundImageCallback)(nullptr);

	const auto state = raw->lifetime().make_state<State>();
	((type == Type::ChannelFutureOwner)
		? std::move(from) | rpl::map([=](
				const std::vector<not_null<PeerData*>> &list) {
			return std::vector<not_null<PeerData*>>{ list.front() };
		})
		: std::move(from)
	) | rpl::on_next([=](
			const std::vector<not_null<PeerData*>> &list) {
		auto was = base::take(state->from);
		auto buttons = base::take(state->buttons);
		state->from.reserve(list.size());
		state->buttons.reserve(list.size());
		for (const auto &peer : list) {
			state->from.push_back(peer);
			const auto i = ranges::find(was, peer);
			if (i != end(was)) {
				const auto index = int(i - begin(was));
				Assert(buttons[index] != nullptr);
				state->buttons.push_back(std::move(buttons[index]));
			} else {
				state->buttons.push_back(
					std::make_unique<Ui::UserpicButton>(raw, peer, st->button));
				const auto raw = state->buttons.back().get();
				base::install_event_filter(raw, [=](not_null<QEvent*> e) {
					return (e->type() == QEvent::Paint && !state->painting)
						? base::EventFilterResult::Cancel
						: base::EventFilterResult::Continue;
				});
			}
		}
		state->count.force_assign(int(list.size()));
		overlay->update();
	}, raw->lifetime());

	rpl::combine(
		raw->widthValue(),
		state->count.value()
	) | rpl::on_next([=](int width, int count) {
		const auto skip = st::boostReplaceUserpicsSkip;
		const auto left = width - 2 * right->width() - skip;
		const auto shift = std::min(
			st->shift,
			(count > 1 ? (left / (count - 1)) : width));
		const auto total = right->width()
			+ (count ? (skip + right->width() + (count - 1) * shift) : 0);
		auto x = (width - total) / 2;
		for (const auto &single : state->buttons) {
			single->moveToLeft(x, 0);
			x += shift;
		}
		if (count) {
			x += right->width() - shift + skip;
		}
		right->moveToLeft(x, 0);
		overlay->setGeometry(QRect(0, 0, width, raw->height()));
	}, raw->lifetime());

	overlay->paintRequest(
	) | rpl::filter([=] {
		return !state->buttons.empty();
	}) | rpl::on_next([=] {
		const auto outerw = overlay->width();
		const auto ratio = style::DevicePixelRatio();
		if (state->layer.size() != QSize(outerw, full) * ratio) {
			state->layer = QImage(
				QSize(outerw, full) * ratio,
				QImage::Format_ARGB32_Premultiplied);
			state->layer.setDevicePixelRatio(ratio);
		}
		state->layer.fill(Qt::transparent);

		auto q = Painter(&state->layer);
		auto hq = PainterHighQualityEnabler(q);
		const auto stroke = st->stroke;
		const auto half = stroke / 2.;
		auto pen = st::windowBg->p;
		pen.setWidthF(stroke * 2.);
		state->painting = true;
		for (const auto &button : state->buttons) {
			q.setPen(pen);
			q.setBrush(Qt::NoBrush);
			q.drawEllipse(button->geometry());
			const auto position = button->pos();
			button->render(&q, position, QRegion(), QWidget::DrawChildren);
		}
		state->painting = false;
		const auto last = state->buttons.back().get();
		if (type != Type::AuctionRecipient) {
			const auto boosting = (type == Type::BoostReplace);
			const auto guard = (type == Type::GuardBotReplace);
			const auto gradient = boosting || guard;
			const auto back = gradient ? last : right;
			const auto add = st::boostReplaceIconAdd;
			const auto &icon = guard
				? st::guardBotReplaceIcon
				: boosting
				? st::boostReplaceIcon
				: st::starrefJoinIcon;
			const auto skip = gradient ? st::boostReplaceIconSkip : 0;
			const auto w = icon.width() + 2 * skip;
			const auto h = icon.height() + 2 * skip;
			const auto x = back->x() + back->width() - w + add.x();
			const auto y = back->y() + back->height() - h + add.y();

			pen.setWidthF(drawCornerPeer ? stroke * 2 : stroke);
			q.setPen(pen);
			if (drawCornerPeer) {
				q.setBrush(Qt::NoBrush);
				q.drawEllipse(x - half, y - half, w + stroke, h + stroke);
				drawCornerPeer(
					q,
					x - half,
					y - half,
					w + stroke,
					w + stroke);
			} else {
				auto brush = QLinearGradient(
					QPointF(x + w, y + h),
					QPointF(x, y));
				brush.setStops(Ui::Premium::ButtonGradientStops());
				q.setBrush(brush);
				q.drawEllipse(x - half, y - half, w + stroke, h + stroke);
				icon.paint(q, x + skip, y + skip, outerw);
			}
		}
		const auto size = st::boostReplaceArrow.size();
		st::boostReplaceArrow.paint(
			q,
			(last->x()
				+ last->width()
				+ (st::boostReplaceUserpicsSkip - size.width()) / 2),
			(last->height() - size.height()) / 2,
			outerw);
		q.end();

		auto p = QPainter(overlay);
		p.drawImage(0, 0, state->layer);
	}, overlay->lifetime());
	return result;
}

object_ptr<Ui::RpWidget> CreateUserpicsWithMoreBadge(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<std::vector<not_null<PeerData*>>> peers,
		const style::UserpicsRow &st,
		int limit) {
	struct State {
		std::vector<not_null<PeerData*>> from;
		std::vector<std::unique_ptr<Ui::UserpicButton>> buttons;
		QImage layer;
		QImage badge;
		rpl::variable<int> count = 0;
		bool painting = false;
	};
	const auto full = st.button.size.height()
		+ (st.complex ? (st::boostReplaceIconAdd.y() + st::lineWidth) : 0);
	auto result = object_ptr<Ui::FixedHeightWidget>(parent, full);
	const auto raw = result.data();
	const auto overlay = CreateChild<Ui::RpWidget>(raw);

	const auto state = raw->lifetime().make_state<State>();
	std::move(
		peers
	) | rpl::on_next([=, &st](
			const std::vector<not_null<PeerData*>> &list) {
		auto was = base::take(state->from);
		auto buttons = base::take(state->buttons);
		state->from.reserve(list.size());
		state->buttons.reserve(list.size());
		for (const auto &peer : list | ranges::views::take(limit)) {
			state->from.push_back(peer);
			const auto i = ranges::find(was, peer);
			if (i != end(was)) {
				const auto index = int(i - begin(was));
				Assert(buttons[index] != nullptr);
				state->buttons.push_back(std::move(buttons[index]));
			} else {
				state->buttons.push_back(
					std::make_unique<Ui::UserpicButton>(raw, peer, st.button));
				const auto raw = state->buttons.back().get();
				base::install_event_filter(raw, [=](not_null<QEvent*> e) {
					return (e->type() == QEvent::Paint && !state->painting)
						? base::EventFilterResult::Cancel
						: base::EventFilterResult::Continue;
				});
			}
		}
		state->count.force_assign(int(list.size()));
		overlay->update();
	}, raw->lifetime());

	if (const auto count = state->count.current()) {
		const auto single = st.button.size.width();
		const auto used = std::min(count, int(state->buttons.size()));
		const auto shift = st.shift;
		raw->resize(used ? (single + (used - 1) * shift) : 0, raw->height());
	}
	rpl::combine(
		raw->widthValue(),
		state->count.value()
	) | rpl::on_next([=, &st](int width, int count) {
		const auto single = st.button.size.width();
		const auto left = width - single;
		const auto used = std::min(count, int(state->buttons.size()));
		const auto shift = std::min(
			st.shift,
			(used > 1 ? (left / (used - 1)) : width));
		const auto total = used ? (single + (used - 1) * shift) : 0;
		auto x = (width - total) / 2;
		for (const auto &single : state->buttons) {
			single->moveToLeft(x, 0);
			x += shift;
		}
		overlay->setGeometry(QRect(0, 0, width, raw->height()));
	}, raw->lifetime());

	overlay->paintRequest(
	) | rpl::filter([=] {
		return !state->buttons.empty();
	}) | rpl::on_next([=, &st] {
		const auto outerw = overlay->width();
		const auto ratio = style::DevicePixelRatio();
		if (state->layer.size() != QSize(outerw, full) * ratio) {
			state->layer = QImage(
				QSize(outerw, full) * ratio,
				QImage::Format_ARGB32_Premultiplied);
			state->layer.setDevicePixelRatio(ratio);
		}
		state->layer.fill(Qt::transparent);

		auto q = QPainter(&state->layer);
		auto hq = PainterHighQualityEnabler(q);
		const auto stroke = st.stroke;
		const auto half = stroke / 2.;
		auto pen = st.bg->p;
		pen.setWidthF(stroke * 2.);
		state->painting = true;
		const auto paintOne = [&](not_null<Ui::UserpicButton*> button) {
			q.setPen(pen);
			q.setBrush(Qt::NoBrush);
			q.drawEllipse(button->geometry());
			const auto position = button->pos();
			button->render(&q, position, QRegion(), QWidget::DrawChildren);
		};
		if (st.invert) {
			for (const auto &button : ranges::views::reverse(state->buttons)) {
				paintOne(button.get());
			}
		} else {
			for (const auto &button : state->buttons) {
				paintOne(button.get());
			}
		}
		state->painting = false;

		const auto text = (state->count.current() > limit)
			? ('+' + QString::number(state->count.current() - limit))
			: QString();
		if (st.complex && !text.isEmpty()) {
			const auto last = state->buttons.back().get();
			const auto add = st::boostReplaceIconAdd;
			const auto skip = st::boostReplaceIconSkip;
			const auto w = st::boostReplaceIcon.width() + 2 * skip;
			const auto h = st::boostReplaceIcon.height() + 2 * skip;
			const auto x = last->x() + last->width() - w + add.x();
			const auto y = last->y() + last->height() - h + add.y();
			const auto &font = st::semiboldFont;
			const auto width = font->width(text);
			const auto padded = std::max(w, width + 2 * font->spacew);
			const auto rect = QRect(x - (padded - w) / 2, y, padded, h);
			auto brush = QLinearGradient(rect.bottomRight(), rect.topLeft());
			brush.setStops(Ui::Premium::ButtonGradientStops());
			q.setBrush(brush);
			pen.setWidthF(stroke);
			q.setPen(pen);
			const auto rectf = QRectF(rect);
			const auto radius = std::min(rect.width(), rect.height()) / 2.;
			q.drawRoundedRect(
				rectf.marginsAdded(QMarginsF{ half, half, half, half }),
				radius,
				radius);
			q.setFont(font);
			q.setPen(st::premiumButtonFg);
			q.drawText(rect, Qt::AlignCenter, text);
		}
		q.end();

		auto p = QPainter(overlay);
		p.drawImage(0, 0, state->layer);
	}, overlay->lifetime());
	return result;
}

class UniqueGiftBackground final : public Ui::DynamicImage {
public:
	UniqueGiftBackground(
		not_null<Main::Session*> session,
		std::shared_ptr<Data::UniqueGift> unique)
	: _session(session)
	, _unique(std::move(unique)) {
	}

	std::shared_ptr<Ui::DynamicImage> clone() override {
		return std::make_shared<UniqueGiftBackground>(_session, _unique);
	}

	void subscribeToUpdates(Fn<void()> callback) override {
		_repaint = std::move(callback);
		if (!_repaint) {
			_patternEmoji = nullptr;
		}
	}

	QImage image(int size) override {
		if (!_patternEmoji) {
			_patternEmoji = _session->data().customEmojiManager().create(
				_unique->pattern.document,
				[=] { ready(); },
				Data::CustomEmojiSizeTag::Large);
			[[maybe_unused]] const auto preload = _patternEmoji->ready();
		}
		const auto inner = QRect(0, 0, size, size);
		const auto ratio = style::DevicePixelRatio();
		if (_backgroundCache.size() != inner.size() * ratio) {
			_backgroundCache = QImage(
				inner.size() * ratio,
				QImage::Format_ARGB32_Premultiplied);
			_backgroundCache.fill(Qt::transparent);
			_backgroundCache.setDevicePixelRatio(ratio);

			const auto radius = st::giftBoxGiftRadius;
			auto p = QPainter(&_backgroundCache);
			auto hq = PainterHighQualityEnabler(p);
			auto gradient = QRadialGradient(
				inner.center(),
				inner.width() / 2);
			gradient.setStops({
				{ 0., _unique->backdrop.centerColor },
				{ 1., _unique->backdrop.edgeColor },
			});
			p.setBrush(gradient);
			p.setPen(Qt::NoPen);
			p.drawRoundedRect(inner, radius, radius);
			_backroundPatterned = false;
		}
		if (!_backroundPatterned && _patternEmoji->ready()) {
			_backroundPatterned = true;
			auto p = QPainter(&_backgroundCache);
			p.setClipRect(inner);
			const auto skip = inner.width() / 3;
			Ui::PaintBgPoints(
				p,
				Ui::PatternBgPointsSmall(),
				_patternCache,
				_patternEmoji.get(),
				*_unique,
				QRect(-skip, 0, inner.width() + 2 * skip, inner.height()));
		}
		return _backgroundCache;
	}

private:
	void ready() {
		if (!_backroundPatterned && _repaint) {
			_repaint();
		}
	}

	const not_null<Main::Session*> _session;
	const std::shared_ptr<Data::UniqueGift> _unique;
	Fn<void()> _repaint;
	std::unique_ptr<Ui::Text::CustomEmoji> _patternEmoji;
	QImage _backgroundCache;
	base::flat_map<float64, QImage> _patternCache;
	bool _backroundPatterned = false;

};

[[nodiscard]] PaintRoundImageCallback GenerateGiftUniqueUserpicCallback(
		not_null<Main::Session*> session,
		std::shared_ptr<Data::UniqueGift> unique,
		Fn<void()> update) {
	struct State {
		QImage layer;
		std::shared_ptr<UniqueGiftBackground> bg;
		std::shared_ptr<Ui::Text::CustomEmoji> sticker;
	};
	const auto state = std::make_shared<State>();
	const auto repaint = [=] {
		if (update) {
			update();
		}
	};
	state->bg = std::make_shared<UniqueGiftBackground>(session, unique);
	state->bg->subscribeToUpdates(repaint);
	const auto tag = Data::CustomEmojiSizeTag::Isolated;
	state->sticker = session->data().customEmojiManager().create(
		unique->model.document,
		repaint,
		tag);

	return [=](QPainter &p, int x, int y, int outerw, int size) {
		const auto ideal = st::boostReplaceUserpic.photoSize;
		const auto scale = size / float64(ideal);
		const auto ratio = style::DevicePixelRatio();
		if (state->layer.size() != QSize(ideal, ideal) * ratio) {
			state->layer = QImage(
				QSize(ideal, ideal) * ratio,
				QImage::Format_ARGB32_Premultiplied);
			state->layer.setDevicePixelRatio(ratio);
		}
		state->layer.fill(Qt::transparent);

		auto q = QPainter(&state->layer);
		auto hq = PainterHighQualityEnabler(q);
		const auto esize = Data::FrameSizeFromTag(tag) / ratio;
		q.drawImage(QRect(0, 0, ideal, ideal), state->bg->image(ideal));
		state->sticker->paint(q, {
			.textColor = st::windowFg->c,
			.now = crl::now(),
			.position = QPoint((ideal - esize) / 2, (ideal - esize) / 2),
		});
		q.end();

		if (scale != 1.) {
			p.save();
			p.translate(x, y);
			p.scale(scale, scale);
			p.drawImage(0, 0, state->layer);
			p.restore();
		} else {
			p.drawImage(x, y, state->layer);
		}
	};
}

object_ptr<Ui::RpWidget> CreateGiftTransfer(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<Data::UniqueGift> unique,
		not_null<PeerData*> to) {
	struct State {
		QImage layer;
		QPoint giftPosition;
		PaintRoundImageCallback paintGift;
	};
	const auto st = &st::boostReplaceUserpicsRow;
	const auto full = st->button.size.height()
		+ st::boostReplaceIconAdd.y()
		+ st::lineWidth;
	auto result = object_ptr<Ui::FixedHeightWidget>(parent, full);
	const auto raw = result.data();
	const auto right = CreateChild<Ui::UserpicButton>(raw, to, st->button);
	const auto overlay = CreateChild<Ui::RpWidget>(raw);

	const auto state = raw->lifetime().make_state<State>();
	state->paintGift = GenerateGiftUniqueUserpicCallback(
		&to->session(),
		unique,
		[=] { raw->update(); });

	raw->widthValue(
	) | rpl::on_next([=](int width) {
		const auto skip = st::boostReplaceUserpicsSkip;
		const auto total = right->width() + skip + right->width();
		auto x = (width - total) / 2;
		state->giftPosition = QPoint(x, 0);
		x += right->width() + skip;
		right->moveToLeft(x, 0);
		overlay->setGeometry(QRect(0, 0, width, raw->height()));
	}, raw->lifetime());

	overlay->paintRequest(
	) | rpl::on_next([=] {
		const auto outerw = overlay->width();
		const auto ratio = style::DevicePixelRatio();
		if (state->layer.size() != QSize(outerw, full) * ratio) {
			state->layer = QImage(
				QSize(outerw, full) * ratio,
				QImage::Format_ARGB32_Premultiplied);
			state->layer.setDevicePixelRatio(ratio);
		}
		state->layer.fill(Qt::transparent);

		auto q = Painter(&state->layer);
		auto hq = PainterHighQualityEnabler(q);
		state->paintGift(
			q,
			state->giftPosition.x(),
			state->giftPosition.y(),
			outerw,
			right->width());

		const auto size = st::boostReplaceArrow.size();
		st::boostReplaceArrow.paint(
			q,
			(state->giftPosition.x()
				+ right->width()
				+ (st::boostReplaceUserpicsSkip - size.width()) / 2),
			(right->height() - size.height()) / 2,
			outerw);

		q.end();

		auto p = QPainter(overlay);
		p.drawImage(0, 0, state->layer);
	}, overlay->lifetime());
	return result;
}
