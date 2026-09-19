/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace LoogriGram {

// LoogriGram: gifts, giveaways, invoices, paid media, payments, prizes and
// priced suggested posts are how money moves between people on Telegram. We
// take no part in any of it, in either direction, so none of it is parsed or
// shown.
//
// **The item is still created and still lives in history - but empty.** The
// server tracks what we have read by message id, and the read position only
// advances past messages we have. Drop one entirely and nothing ever marks it
// read, so the chat keeps an unread badge that scrolling cannot clear - the
// same coupling that made read-receipt suppression get reverted here.
//
// So the TL type is checked before anything inside the message is looked at.
// A money message becomes a bare service item: its id, date, sender and the
// ContentHidden flag. No media, no text, no price, no stickers, no images, and
// no view is built for it. Element::isHidden() collapses it to nothing, the
// chat list preview skips it, and no notification is raised.
[[nodiscard]] bool MoneyMedia(const MTPMessageMedia &media);
[[nodiscard]] bool MoneyAction(const MTPMessageAction &action);
[[nodiscard]] bool MoneyMessage(const MTPDmessage &data);

// LoogriGram: stories are removed, and a message that carries one - a
// forwarded story or "X mentioned you in a story" - is hidden the same way.
// A reply to a story is not: it keeps its text and loses the quoted story.
[[nodiscard]] bool StoryMedia(const MTPMessageMedia &media);

[[nodiscard]] bool HiddenMedia(const MTPMessageMedia &media);
[[nodiscard]] bool HiddenMessage(const MTPDmessage &data);

[[nodiscard]] bool HiddenContent(not_null<const HistoryItem*> item);

} // namespace LoogriGram
