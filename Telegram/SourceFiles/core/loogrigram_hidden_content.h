/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace LoogriGram {

// LoogriGram: gifts, giveaways and paid posts are things other people send
// us. We take no part in any of it, so none of it is shown - not the gift
// someone sent, not the giveaway a channel is running, not the post we would
// have to pay to read.
//
// **The item is still created and still lives in history.** That is the whole
// point of hiding at the view rather than refusing the message: Telegram
// tracks what we have read by message id, and the read position only advances
// past messages we have. Drop one at parse time and nothing ever marks it
// read, so the chat keeps an unread badge that scrolling cannot clear - the
// same coupling that made read-receipt suppression get reverted here.
//
// Every view asks this instead. Element::isHidden() collapses the message to
// nothing, the chat list preview skips it when choosing what to show, and the
// notification for it is never raised.
//
// Service messages that are only a line of text - "X boosted this channel",
// a refund notice, a price change - are not covered here. They carry no media
// to recognise them by, and are cut at the source instead: their action is
// routed to PrepareEmptyText in history_item.cpp, which is upstream's own way
// of saying an action displays nothing.
[[nodiscard]] bool HiddenContent(not_null<const HistoryItem*> item);

} // namespace LoogriGram
