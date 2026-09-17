/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_message_reaction_id.h"

#include "data/stickers/data_custom_emoji.h"

namespace Data {

HashtagWithUsername HashtagWithUsernameFromQuery(QStringView query) {
	const auto match = TextUtilities::RegExpHashtag(true).match(query);
	if (match.hasMatch()) {
		const auto username = match.capturedView(2).mid(1).toString();
		const auto offset = int(match.capturedLength(1));
		const auto full = int(query.size());
		const auto length = full
			- int(username.size())
			- 1
			- offset
			- int(match.capturedLength(3));
		if (!username.isEmpty() && length > 0 && offset + length <= full) {
			const auto hashtag = query.mid(offset, length).toString();
			return { hashtag, username };
		}
	}
	return {};
}

QString ReactionEntityData(const ReactionId &id) {
	if (id.empty()) {
		return {};
	} else if (const auto custom = id.custom()) {
		return SerializeCustomEmojiId(custom);
	}
	return u"default:"_q + id.emoji();
}

ReactionId ReactionFromMTP(const MTPReaction &reaction) {
	return reaction.match([](MTPDreactionEmpty) {
		return ReactionId{ QString() };
	}, [](const MTPDreactionEmoji &data) {
		return ReactionId{ qs(data.vemoticon()) };
	}, [](const MTPDreactionCustomEmoji &data) {
		return ReactionId{ DocumentId(data.vdocument_id().v) };
	}, [](const MTPDreactionPaid &) {
		// LoogriGram: paid reactions are deleted; one that arrives is read
		// as an empty reaction, which every caller already ignores.
		return ReactionId{ QString() };
	});
}

MTPReaction ReactionToMTP(ReactionId id) {
	if (!id) {
		return MTP_reactionEmpty();
	} else if (const auto custom = id.custom()) {
		return MTP_reactionCustomEmoji(MTP_long(custom));
	} else {
		return MTP_reactionEmoji(MTP_string(id.emoji()));
	}
}

} // namespace Data
