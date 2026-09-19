/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_share_at_time.h"

#include "boxes/share_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_chat_participant_status.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/view/history_view_context_menu.h" // CopyPostLink.
#include "lang/lang_keys.h"
#include "main/main_session.h"

namespace Media::View {

QString FormatShareAtTime(TimeId seconds) {
	const auto minutes = seconds / 60;
	const auto h = minutes / 60;
	const auto m = minutes % 60;
	const auto s = seconds % 60;
	const auto zero = QChar('0');
	return h
		? u"%1:%2:%3"_q.arg(h).arg(m, 2, 10, zero).arg(s, 2, 10, zero)
		: u"%1:%2"_q.arg(m).arg(s, 2, 10, zero);
}

object_ptr<Ui::BoxContent> PrepareShareAtTimeBox(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<HistoryItem*> item,
		TimeId videoTimestamp) {
	const auto id = item->fullId();
	const auto history = item->history();
	const auto owner = &history->owner();
	const auto session = &history->session();
	const auto canCopyLink = item->hasDirectLink()
		&& history->peer->isBroadcast()
		&& history->peer->asBroadcast()->hasUsername();
	const auto hasCaptions = item->media()
		&& !item->originalText().text.isEmpty()
		&& item->media()->allowsEditCaption();
	const auto hasOnlyForcedForwardedInfo = !hasCaptions
		&& item->media()
		&& item->media()->forceForwardedInfo();

	auto copyCallback = [=] {
		const auto item = owner->message(id);
		if (!item) {
			return;
		}
		CopyPostLink(
			show,
			item->fullId(),
			HistoryView::Context::History,
			videoTimestamp);
	};

	const auto requiredRight = item->requiredSendRight();
	const auto requiresInline = item->requiresSendInlineRight();
	auto filterCallback = [=](not_null<Data::Thread*> thread) {
		if (const auto user = thread->peer()->asUser()) {
			if (user->canSendIgnoreMoneyRestrictions()) {
				return true;
			}
		}
		return Data::CanSend(thread, requiredRight)
			&& (!requiresInline
				|| Data::CanSend(thread, ChatRestriction::SendInline));
	};
	auto copyLinkCallback = canCopyLink
		? Fn<void()>(std::move(copyCallback))
		: Fn<void()>();
	return Box<ShareBox>(ShareBox::Descriptor{
		.session = session,
		.copyCallback = std::move(copyLinkCallback),
		.submitCallback = ShareBox::DefaultForwardCallback(
			show,
			history,
			{ id },
			videoTimestamp),
		.filterCallback = std::move(filterCallback),
		.titleOverride = tr::lng_share_at_time_title(
			lt_time,
			rpl::single(FormatShareAtTime(videoTimestamp))),
		.st = DarkShareBoxStyle(),
		.forwardOptions = {
			.sendersCount = ItemsForwardSendersCount({ item }),
			.captionsCount = ItemsForwardCaptionsCount({ item }),
			.show = !hasOnlyForcedForwardedInfo,
		},
		.moneyRestrictionError = ShareMessageMoneyRestrictionError(),
	});
}

} // namespace Media::View
