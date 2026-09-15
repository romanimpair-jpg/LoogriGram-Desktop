/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/chat/chat_style.h"

namespace Data {
class Media;
} // namespace Data

struct FullMsgId;
struct WebPageData;

namespace HistoryView {

struct TextState;

// LoogriGram: this had a second kind, a giveaway message's "How this works"
// button. Giveaways are deleted, so only the rich-page kind remains and the
// media-based constructor with it.
class ViewButton {
public:
	ViewButton(
		FullMsgId itemId,
		uint8 colorIndex,
		Fn<void()> updateCallback);
	~ViewButton();

	[[nodiscard]] bool matches(FullMsgId itemId) const;
	[[nodiscard]] int height() const;
	[[nodiscard]] bool belowMessageInfo() const;

	void draw(
		Painter &p,
		const QRect &r,
		const Ui::ChatPaintContext &context);

	[[nodiscard]] const ClickHandlerPtr &link() const;
	bool checkLink(const ClickHandlerPtr &other, bool pressed);

	[[nodiscard]] QRect countRect(const QRect &r) const;

	[[nodiscard]] bool getState(
		QPoint point,
		const QRect &g,
		not_null<TextState*> outResult) const;

private:
	void resized() const;

	struct Inner;
	const std::unique_ptr<Inner> _inner;
};

} // namespace HistoryView
