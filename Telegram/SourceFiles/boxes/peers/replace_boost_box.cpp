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
// Giving a boost means spending a subscription slot, so the flow is gone,
// and so are the boost counters and per-level feature lists the boost boxes
// showed. What is left is the userpic-row helpers other boxes use.

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
		{
			const auto guard = (type == Type::GuardBotReplace);
			const auto back = guard ? last : right;
			const auto add = st::boostReplaceIconAdd;
			const auto &icon = guard
				? st::guardBotReplaceIcon
				: st::starrefJoinIcon;
			const auto skip = guard ? st::boostReplaceIconSkip : 0;
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
