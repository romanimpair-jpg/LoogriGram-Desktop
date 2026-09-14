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
	//
	// MediaGiftBox is a container rather than a purchase marker, and two of
	// the things filed under it are not purchases. Both are hidden anyway,
	// deliberately - do not "fix" this by narrowing the check:
	//
	//   BirthdaySuggest  "{user} suggests you add your date of birth", from a
	//                    contact who already has it saved. Nothing is bought;
	//                    it only borrows the same service card.
	//   ChatTheme        "X changed the chat theme", where the theme is a
	//                    collectible. Note the asymmetry this leaves: a theme
	//                    change to a non-collectible attaches no media, so it
	//                    is not hidden and still shows.
	return media->gift()
		|| media->giveawayStart()
		|| media->giveawayResults()
		// Every invoice, not only paid media. An invoice is a request to pay
		// for something, which is the one thing this fork never does.
		|| media->invoice();
}

} // namespace LoogriGram
