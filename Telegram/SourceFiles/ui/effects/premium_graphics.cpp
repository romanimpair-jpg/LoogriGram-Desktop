/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_graphics.h"

#include "lang/lang_keys.h"
#include "ui/abstract_button.h"
#include "ui/effects/animations.h"
#include "ui/effects/gradient.h"
#include "ui/effects/numbers_animation.h"
#include "ui/effects/premium_bubble.h"
#include "ui/text/text_utilities.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_options.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_premium_limits.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"

#include <QtGui/QBrush>

namespace Ui {
namespace Premium {

QGradientStops ButtonGradientStops() {
	return {
		{ 0., st::premiumButtonBg1->c },
		{ 0.6, st::premiumButtonBg2->c },
		{ 1., st::premiumButtonBg3->c },
	};
}

QGradientStops GiftGradientStops() {
	return {
		{ 0., st::premiumButtonBg1->c },
		{ 1., st::premiumButtonBg2->c },
	};
}

QLinearGradient ComputeGradient(
		not_null<QWidget*> content,
		int left,
		int width) {
	// Take a full width of parent box without paddings.
	const auto fullGradientWidth = content->parentWidget()->width();
	auto fullGradient = QLinearGradient(0, 0, fullGradientWidth, 0);
	fullGradient.setStops(ButtonGradientStops());

	auto gradient = QLinearGradient(0, 0, width, 0);
	const auto fullFinal = float64(fullGradient.finalStop().x());
	left += ((fullGradientWidth - content->width()) / 2);
	gradient.setColorAt(
		.0,
		anim::gradient_color_at(fullGradient, left / fullFinal));
	gradient.setColorAt(
		1.,
		anim::gradient_color_at(
			fullGradient,
			std::min(1., (left + width) / fullFinal)));

	return gradient;
}

} // namespace Premium
} // namespace Ui
