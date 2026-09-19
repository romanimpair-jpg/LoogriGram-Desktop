/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/loogrigram_hidden_content.h"

#include "history/history_item.h"

namespace LoogriGram {

bool MoneyMedia(const MTPMessageMedia &media) {
	switch (media.type()) {
	case mtpc_messageMediaInvoice:
	case mtpc_messageMediaPaidMedia:
	case mtpc_messageMediaGiveaway:
	case mtpc_messageMediaGiveawayResults:
		return true;
	}
	return false;
}

bool MoneyAction(const MTPMessageAction &action) {
	switch (action.type()) {
	case mtpc_messageActionPaymentSentMe:
	case mtpc_messageActionPaymentSent:
	case mtpc_messageActionPaymentRefunded:
	case mtpc_messageActionGiftPremium:
	case mtpc_messageActionGiftCode:
	case mtpc_messageActionGiftStars:
	case mtpc_messageActionGiftTon:
	case mtpc_messageActionGiveawayLaunch:
	case mtpc_messageActionGiveawayResults:
	case mtpc_messageActionPrizeStars:
	case mtpc_messageActionBoostApply:
	case mtpc_messageActionStarGift:
	case mtpc_messageActionStarGiftUnique:
	case mtpc_messageActionStarGiftPurchaseOffer:
	case mtpc_messageActionStarGiftPurchaseOfferDeclined:
	case mtpc_messageActionPaidMessagesPrice:
	case mtpc_messageActionPaidMessagesRefunded:
	case mtpc_messageActionSuggestedPostApproval:
	case mtpc_messageActionSuggestedPostSuccess:
	case mtpc_messageActionSuggestedPostRefund:
	// Not money: "{user} suggests you add your date of birth". It used the
	// gift card to display itself and was hidden along with gifts; it stays
	// hidden rather than getting a view of its own.
	case mtpc_messageActionSuggestBirthday:
		return true;
	case mtpc_messageActionSetChatTheme:
		// A theme change is shown, unless the theme is a collectible gift.
		return (action.c_messageActionSetChatTheme().vtheme().type()
			== mtpc_chatThemeUniqueGift);
	}
	return false;
}

bool MoneyMessage(const MTPDmessage &data) {
	if (const auto media = data.vmedia(); media && MoneyMedia(*media)) {
		return true;
	} else if (const auto suggested = data.vsuggested_post()) {
		// A proposal to publish a post for a price.
		return suggested->data().vprice().has_value();
	}
	return false;
}

bool StoryMedia(const MTPMessageMedia &media) {
	return (media.type() == mtpc_messageMediaStory);
}

bool HiddenMedia(const MTPMessageMedia &media) {
	return MoneyMedia(media) || StoryMedia(media);
}

bool HiddenMessage(const MTPDmessage &data) {
	if (MoneyMessage(data)) {
		return true;
	} else if (const auto media = data.vmedia()) {
		return StoryMedia(*media);
	}
	return false;
}

bool HiddenContent(not_null<const HistoryItem*> item) {
	return item->contentHidden();
}

} // namespace LoogriGram
