/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/channel_statistics/boosts/giveaway/boost_badge.h"

#include "ui/effects/radial_animation.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "ui/widgets/labels.h"
#include "styles/style_giveaway.h"
#include "styles/style_statistics.h"
#include "styles/style_widgets.h"

namespace Info::Statistics {

not_null<Ui::RpWidget*> InfiniteRadialAnimationWidget(
		not_null<Ui::RpWidget*> parent,
		int size,
		const style::InfiniteRadialAnimation *st) {
	class Widget final : public Ui::RpWidget {
	public:
		Widget(
			not_null<Ui::RpWidget*> p,
			int size,
			const style::InfiniteRadialAnimation *st)
		: Ui::RpWidget(p)
		, _st(st ? st : &st::startGiveawayButtonLoading)
		, _animation([=] { update(); }, *_st) {
			resize(size, size);
			shownValue() | rpl::on_next([=](bool v) {
				return v
					? _animation.start()
					: _animation.stop(anim::type::instant);
			}, lifetime());
		}

	protected:
		void paintEvent(QPaintEvent *e) override {
			auto p = QPainter(this);
			p.setPen(st::activeButtonFg);
			p.setBrush(st::activeButtonFg);
			const auto r = rect() - Margins(_st->thickness);
			_animation.draw(p, r.topLeft(), r.size(), width());
		}

	private:
		const style::InfiniteRadialAnimation *_st;
		Ui::InfiniteRadialAnimation _animation;

	};

	return Ui::CreateChild<Widget>(parent.get(), size, st);
}

void AddChildToWidgetCenter(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::RpWidget*> child) {
	parent->sizeValue(
	) | rpl::on_next([=](const QSize &s) {
		const auto size = child->size();
		child->moveToLeft(
			(s.width() - size.width()) / 2,
			(s.height() - size.height()) / 2);
	}, child->lifetime());
}

} // namespace Info::Statistics
