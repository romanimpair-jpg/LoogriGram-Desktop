/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_common.h"

#include "base/qt/qt_key_modifiers.h"
#include "data/data_histories.h"
#include "data/data_thread.h"
#include "history/history.h"

namespace Api {

MTPSuggestedPost SuggestToMTP(SuggestOptions suggest) {
	using Flag = MTPDsuggestedPost::Flag;
	// LoogriGram: never with a price. Upstream set f_price when the offer
	// had one; what is sent from here is upstream's "for free" case.
	return suggest.exists
		? MTP_suggestedPost(
			MTP_flags(suggest.date ? Flag::f_schedule_date : Flag()),
			MTPStarsAmount(),
			MTP_int(suggest.date))
		: MTPSuggestedPost();
}

SendAction::SendAction(
	not_null<Data::Thread*> thread,
	SendOptions options)
: history(thread->owningHistory())
, options(options)
, replyTo({ .messageId = { history->peer->id, thread->topicRootId() } }) {
	replyTo.topicRootId = replyTo.messageId.msg;
}

SendOptions DefaultSendWhenOnlineOptions() {
	return {
		.scheduled = kScheduledUntilOnlineTimestamp,
		.silent = base::IsCtrlPressed(),
	};
}

MTPInputReplyTo SendAction::mtpReplyTo() const {
	return Data::ReplyToForMTP(history, replyTo);
}

} // namespace Api
