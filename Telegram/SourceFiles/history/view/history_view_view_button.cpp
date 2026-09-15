/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_view_button.h"

#include "core/application.h"
#include "core/click_handler_types.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/view/history_view_cursor_state.h"
#include "iv/iv_instance.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

[[nodiscard]] ClickHandlerPtr MakeRichMessageButtonClickHandler(
		FullMsgId itemId) {
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		const auto controller = my.sessionWindow.get();
		const auto item = controller
			? controller->session().data().message(itemId)
			: nullptr;
		if (!controller || !item) {
			return;
		}
		Core::App().iv().showRichMessage(controller, not_null{ item });
	});
}

[[nodiscard]] QString MakeRichMessageButtonText() {
	return tr::lng_view_button_full_article(tr::now);
}

} // namespace

struct ViewButton::Inner {
	Inner(
		FullMsgId itemId,
		uint8 colorIndex,
		Fn<void()> updateCallback);

	void createRipple(int height);
	void toggleRipple(bool pressed, int height);

	const style::margins &margins;
	const ClickHandlerPtr link;
	const Fn<void()> updateCallback;
	FullMsgId itemId;
	uint32 lastWidth : 24 = 0;
	uint32 colorIndex : 6 = 0;
	uint32 aboveInfo : 1 = 0;
	QPoint lastPoint;
	std::unique_ptr<Ui::RippleAnimation> ripple;
	Ui::Text::String text;
};

ViewButton::Inner::Inner(
	FullMsgId itemId,
	uint8 colorIndex,
	Fn<void()> updateCallback)
: margins(st::historyViewButtonMargins)
, link(MakeRichMessageButtonClickHandler(itemId))
, updateCallback(std::move(updateCallback))
, itemId(itemId)
, colorIndex(colorIndex)
, aboveInfo(1)
, text(st::historyViewButtonTextStyle, MakeRichMessageButtonText()) {
}

void ViewButton::Inner::createRipple(int height) {
	const auto radius = st::historyPagePreview.radius;
	ripple = std::make_unique<Ui::RippleAnimation>(
		st::defaultRippleAnimation,
		Ui::RippleAnimation::RoundRectMask(
			QSize(lastWidth, height - margins.top() - margins.bottom()),
			radius),
		updateCallback);
}

void ViewButton::Inner::toggleRipple(bool pressed, int height) {
	if (pressed) {
		if (!ripple) {
			createRipple(height);
		}
		ripple->add(lastPoint);
	} else if (ripple) {
		ripple->lastStop();
	}
}

ViewButton::ViewButton(
		FullMsgId itemId,
		uint8 colorIndex,
		Fn<void()> updateCallback)
: _inner(std::make_unique<Inner>(
	itemId,
	colorIndex,
	std::move(updateCallback))) {
}

ViewButton::~ViewButton() {
}

bool ViewButton::matches(FullMsgId itemId) const {
	return (_inner->itemId == itemId);
}

void ViewButton::resized() const {
	if (_inner->ripple) {
		_inner->createRipple(height());
	}
}

int ViewButton::height() const {
	return st::historyPageButtonHeight
		+ _inner->margins.top()
		+ _inner->margins.bottom();
}

bool ViewButton::belowMessageInfo() const {
	return !_inner->aboveInfo;
}

void ViewButton::draw(
		Painter &p,
		const QRect &r,
		const Ui::ChatPaintContext &context) {
	const auto st = context.st;
	const auto stm = context.messageStyle();
	const auto selected = context.selected();
	const auto cache = context.outbg
		? stm->replyCache[st->colorPatternIndex(_inner->colorIndex)].get()
		: st->coloredReplyCache(selected, _inner->colorIndex).get();
	{
		Ui::Text::ValidateQuotePaintCache(*cache, st::historyPagePreview);
		Ui::Text::FillQuotePaint(p, r, *cache, st::historyPagePreview);
		if (_inner->ripple) {
			_inner->ripple->paint(p, r.left(), r.top(), r.width(), &cache->bg);
			if (_inner->ripple->empty()) {
				_inner->ripple = nullptr;
			}
		}
		const auto padding = st::historyPageButtonPadding;
		const auto availableWidth = r.width()
			- padding.left()
			- padding.right();
		if (availableWidth > 0) {
			const auto textWidth = (_inner->text.maxWidth() < availableWidth)
				? _inner->text.maxWidth()
				: availableWidth;
			p.setPen(cache->icon);
			_inner->text.drawElided(
				p,
				r.left()
					+ padding.left()
					+ (availableWidth - textWidth) / 2,
				r.top() + padding.top(),
				textWidth,
				1,
				style::al_top);
		}
	}
	if (_inner->lastWidth != r.width()) {
		_inner->lastWidth = r.width();
		resized();
	}
}

const ClickHandlerPtr &ViewButton::link() const {
	return _inner->link;
}

bool ViewButton::checkLink(const ClickHandlerPtr &other, bool pressed) {
	if (_inner->link != other) {
		return false;
	}
	_inner->toggleRipple(pressed, height());
	return true;
}

bool ViewButton::getState(
		QPoint point,
		const QRect &g,
		not_null<TextState*> outResult) const {
	if (!g.contains(point)) {
		return false;
	}
	outResult->link = _inner->link;
	if (!_inner->ripple) {
		_inner->lastWidth = g.width();
	}
	_inner->lastPoint = point - g.topLeft();
	return true;
}

QRect ViewButton::countRect(const QRect &r) const {
	return QRect(
		r.left(),
		r.top() + r.height() - height(),
		r.width(),
		height()) - _inner->margins;
}

} // namespace HistoryView
