/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/loogrigram_hidden_content.h"

#include "data/data_media_types.h"
#include "history/history_item.h"

namespace LoogriGram {

bool HiddenContent(not_null<const HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return false;
	}
	// Every gift arrives as MediaGiftBox, whether it came as a message of its
	// own or as the media attached to a service action - the twelve
	// prepare*Gift* handlers in history_item.cpp all build one - so this
	// single check covers premium gifts, star gifts, unique gifts, gift codes
	// and gifted TON alike.
	return media->gift()
		|| media->giveawayStart()
		|| media->giveawayResults()
		// Every invoice, not only paid media. An invoice is a request to pay
		// for something, which is the one thing this fork never does.
		|| media->invoice();
}

} // namespace LoogriGram
