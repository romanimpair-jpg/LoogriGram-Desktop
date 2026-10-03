/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class ClickHandler;

namespace Api {

// LoogriGram: the buttons under a suggested post, which are also how a
// request to allow forwarding from a chat is answered. Only free suggestions
// are answered here: a post with a publishing time and nothing else. A
// suggestion that carries a price is hidden before it is parsed, so it never
// gets these buttons. Gone from upstream: offering a price, offering one on
// someone else's post, changing a price, and answering a gift sale.
[[nodiscard]] std::shared_ptr<ClickHandler> AcceptClickHandler(
	not_null<HistoryItem*> item);
[[nodiscard]] std::shared_ptr<ClickHandler> DeclineClickHandler(
	not_null<HistoryItem*> item);
[[nodiscard]] std::shared_ptr<ClickHandler> SuggestChangesClickHandler(
	not_null<HistoryItem*> item);

} // namespace Api
