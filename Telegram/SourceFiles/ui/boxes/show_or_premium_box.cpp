/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/show_or_premium_box.h"

#include "base/object_ptr.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "styles/style_premium.h"

namespace Ui {
namespace {

constexpr auto kShowOrLineOpacity = 0.3;

} // namespace

object_ptr<RpWidget> MakeShowOrLabel(
		not_null<RpWidget*> parent,
		rpl::producer<QString> text) {
	auto result = object_ptr<FlatLabel>(
		parent,
		std::move(text),
		st::showOrLabel);
	const auto raw = result.data();

	raw->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(raw);

		const auto full = st::showOrLineWidth;
		const auto left = (raw->width() - full) / 2;
		const auto text = raw->naturalWidth() + 2 * st::showOrLabelSkip;
		const auto fill = (full - text) / 2;
		const auto stroke = st::lineWidth;
		const auto top = st::showOrLineTop;
		p.setOpacity(kShowOrLineOpacity);
		p.fillRect(left, top, fill, stroke, st::windowSubTextFg);
		const auto start = left + full - fill;
		p.fillRect(start, top, fill, stroke, st::windowSubTextFg);
	}, raw->lifetime());

	return result;
}

} // namespace Ui
