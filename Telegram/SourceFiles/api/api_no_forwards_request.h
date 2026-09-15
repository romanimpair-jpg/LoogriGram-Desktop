/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class ClickHandler;

namespace Api {

// LoogriGram: this was api_suggest_post, which offered a channel a price in
// stars or TON to publish your post and let an admin approve, decline or
// counter-offer. All of that is deleted. What is left is the other thing
// those buttons do: answering a request to allow forwarding from a chat,
// which costs nothing and is nobody's purchase.
[[nodiscard]] std::shared_ptr<ClickHandler> AcceptClickHandler(
	not_null<HistoryItem*> item);
[[nodiscard]] std::shared_ptr<ClickHandler> DeclineClickHandler(
	not_null<HistoryItem*> item);

} // namespace Api
