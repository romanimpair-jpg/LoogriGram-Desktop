/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_bottom_info.h"

#include "ui/chat/message_bubble.h"
#include "ui/chat/chat_style.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "core/ui_integration.h"
#include "lang/lang_keys.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "history/history_item.h"
#include "history/history.h"
#include "history/view/media/history_view_media.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_cursor_state.h"
#include "base/unixtime.h"
#include "core/click_handler_types.h"
#include "main/main_session.h"
#include "lottie/lottie_icon.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_message_reactions.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"

namespace HistoryView {
namespace {

[[nodiscard]] QString SchedulePeriodText(TimeId period) {
	struct Entry {
		TimeId period = 0;
		QString text;
	};
	const auto map = std::vector<Entry>{
		{ 60, u"minutely"_q },
		{ 300, u"5-minutely"_q },
		{ 24 * 60 * 60, tr::lng_repeated_daily(tr::now) },
		{ 7 * 24 * 60 * 60, tr::lng_repeated_weekly(tr::now) },
		{ 14 * 24 * 60 * 60, tr::lng_repeated_biweekly(tr::now) },
		{ 30 * 24 * 60 * 60, tr::lng_repeated_monthly(tr::now) },
		{
			91 * 24 * 60 * 60,
			tr::lng_repeated_every_month(tr::now, lt_count, 3)
		},
		{
			182 * 24 * 60 * 60,
			tr::lng_repeated_every_month(tr::now, lt_count, 6)
		},
		{ 365 * 24 * 60 * 60, tr::lng_repeated_yearly(tr::now) },
	};
	for (const auto &entry : map) {
		if (entry.period >= period) {
			return entry.text;
		}
	}
	return map.back().text;
}

[[nodiscard]] QString FormatEditedDate(QDateTime sent, QDateTime edited) {
	const auto today = QDateTime::currentDateTime().date();
	const auto time = QLocale().toString(edited.time(), QLocale::ShortFormat);
	if (sent.date() == today && edited.date() == today) {
		return tr::lng_edited_at(tr::now, lt_time, time);
	}
	return tr::lng_edited_on(
		tr::now,
		lt_date,
		langDayOfMonthShort(edited.date()),
		lt_time,
		time);
}

} // namespace

BottomInfo::BottomInfo(
	not_null<::Data::Reactions*> reactionsOwner,
	Data &&data)
: _reactionsOwner(reactionsOwner)
, _data(std::move(data)) {
	layout();
}

BottomInfo::~BottomInfo() = default;

void BottomInfo::update(Data &&data, int availableWidth) {
	_data = std::move(data);
	layout();
	if (width() > 0) {
		resizeGetHeight(std::min(maxWidth(), availableWidth));
	}
}

bool BottomInfo::isWide() const {
	return (_data.flags & Data::Flag::Edited)
		|| _data.scheduleRepeatPeriod
		|| !_data.author.isEmpty()
		|| !_views.isEmpty()
		|| !_replies.isEmpty();
}

TextState BottomInfo::textState(
		not_null<const Message*> view,
		QPoint position) const {
	const auto item = view->data();
	auto result = TextState(item);
	const auto textWidth = _authorEditedDate.maxWidth();
	auto withTicksWidth = textWidth;
	if (_data.flags & (Data::Flag::OutLayout | Data::Flag::Sending)) {
		withTicksWidth += st::historySendStateSpace;
	}
	if (!_views.isEmpty()) {
		const auto viewsWidth = _views.maxWidth();
		const auto right = width()
			- withTicksWidth
			- ((_data.flags & Data::Flag::Pinned) ? st::historyPinWidth : 0)
			- st::historyViewsSpace
			- st::historyViewsWidth
			- viewsWidth;
		const auto inViews = QRect(
			right,
			0,
			withTicksWidth + st::historyViewsWidth,
			st::msgDateFont->height
		).contains(position);
		if (inViews) {
			result.customTooltip = true;
			const auto fullViews = tr::lng_views_tooltip(
				tr::now,
				lt_count_decimal,
				*_data.views);
			const auto fullForwards = _data.forwardsCount
				? ('\n' + tr::lng_forwards_tooltip(
					tr::now,
					lt_count_decimal,
					*_data.forwardsCount))
				: QString();
			result.customTooltipText = fullViews + fullForwards;
		}
	}
	const auto inTime = QRect(
		width() - withTicksWidth,
		0,
		withTicksWidth,
		st::msgDateFont->height
	).contains(position);
	if (inTime) {
		result.cursor = CursorState::Date;
	}
	return result;
}

bool BottomInfo::isSignedAuthorElided() const {
	return _authorElided;
}

void BottomInfo::paint(
		Painter &p,
		QPoint position,
		int outerWidth,
		bool unread,
		bool inverted,
		const PaintContext &context) const {
	const auto st = context.st;
	const auto stm = context.messageStyle();

	auto right = position.x() + width();
	const auto firstLineBottom = position.y() + st::msgDateFont->height;
	if (_data.flags & Data::Flag::OutLayout) {
		const auto &icon = (_data.flags & Data::Flag::Sending)
			? (inverted
				? st->historySendingInvertedIcon()
				: st->historySendingIcon())
			: unread
			? (inverted
				? st->historySentInvertedIcon()
				: stm->historySentIcon)
			: (inverted
				? st->historyReceivedInvertedIcon()
				: stm->historyReceivedIcon);
		icon.paint(
			p,
			QPoint(right, firstLineBottom) + st::historySendStatePosition,
			outerWidth);
		right -= st::historySendStateSpace;
	}

	const auto authorEditedWidth = _authorEditedDate.maxWidth();
	right -= authorEditedWidth;
	_authorEditedDate.drawLeft(
		p,
		right,
		position.y(),
		authorEditedWidth,
		outerWidth);

	if (_data.flags & Data::Flag::Silent) {
		const auto &icon = inverted
			? st->historySilentInvertedIcon()
			: stm->historySilentIcon;
		right -= st::historySilentWidth;
		icon.paint(
			p,
			right,
			firstLineBottom + st::historySilentTop,
			outerWidth);
	}
	if (_data.flags & Data::Flag::Ephemeral) {
		const auto &icon = inverted
			? st->historyEphemeralInvertedIcon()
			: stm->historyEphemeralIcon;
		right -= st::historyEphemeralStateWidth;
		icon.paint(
			p,
			right,
			firstLineBottom + st::historyEphemeralStateTop,
			outerWidth);
	}

	if (_data.flags & Data::Flag::Pinned) {
		const auto &icon = inverted
			? st->historyPinInvertedIcon()
			: stm->historyPinIcon;
		right -= st::historyPinWidth;
		icon.paint(
			p,
			right,
			firstLineBottom + st::historyPinTop,
			outerWidth);
	}
	if (!_views.isEmpty()) {
		const auto viewsWidth = _views.maxWidth();
		right -= st::historyViewsSpace + viewsWidth;
		_views.drawLeft(p, right, position.y(), viewsWidth, outerWidth);

		const auto &icon = inverted
			? st->historyViewsInvertedIcon()
			: stm->historyViewsIcon;
		right -= st::historyViewsWidth;
		icon.paint(
			p,
			right,
			firstLineBottom + st::historyViewsTop,
			outerWidth);
	}
	if (!_replies.isEmpty()) {
		const auto repliesWidth = _replies.maxWidth();
		right -= st::historyViewsSpace + repliesWidth;
		_replies.drawLeft(p, right, position.y(), repliesWidth, outerWidth);

		const auto &icon = inverted
			? st->historyRepliesInvertedIcon()
			: stm->historyRepliesIcon;
		right -= st::historyViewsWidth;
		icon.paint(
			p,
			right,
			firstLineBottom + st::historyViewsTop,
			outerWidth);
	}
	if ((_data.flags & Data::Flag::Sending)
		&& !(_data.flags & Data::Flag::OutLayout)) {
		right -= st::historySendStateSpace;
		const auto &icon = inverted
			? st->historyViewsSendingInvertedIcon()
			: st->historyViewsSendingIcon();
		icon.paint(
			p,
			right,
			firstLineBottom + st::historyViewsTop,
			outerWidth);
	}
	// LoogriGram: a message's effect showed as an icon on a second line
	// here (paintEffect), which replayed the effect on click and caught the
	// effect's fly animation after sending. Effects are gone (2026-10-04),
	// and with them the only reason this info took a second line.
}

QSize BottomInfo::countCurrentSize(int newWidth) {
	if (newWidth >= maxWidth()) {
		return optimalSize();
	}
	return QSize(newWidth, st::msgDateFont->height);
}

void BottomInfo::layout() {
	layoutDateText();
	layoutViewsText();
	layoutRepliesText();
	initDimensions();
}

void BottomInfo::layoutDateText() {
	const auto updated = (_data.flags & Data::Flag::Updated);
	const auto editedPrimary = !updated
		&& (_data.flags & Data::Flag::EditedPrimary)
		&& !(_data.flags & Data::Flag::ForwardedDate);
	const auto edited = editedPrimary
		? QString()
		: updated
		? (tr::lng_ephemeral_updated(tr::now) + ' ')
		: (_data.flags & Data::Flag::Edited)
		? (tr::lng_edited(tr::now) + ' ')
		: (_data.flags & Data::Flag::EstimateDate)
		? (tr::lng_approximate(tr::now) + ' ')
		: _data.scheduleRepeatPeriod
		? (SchedulePeriodText(_data.scheduleRepeatPeriod) + ' ')
		: QString();
	const auto author = _data.author;
	const auto prefix = !author.isEmpty() ? u", "_q : QString();
	const auto date = editedPrimary
		? FormatEditedDate(_data.date, _data.editedDate)
		: edited + ((_data.flags & Data::Flag::ForwardedDate)
		? Ui::FormatDateTimeSavedFrom(_data.date)
		: QLocale().toString(_data.date.time(), QLocale::ShortFormat));
	const auto afterAuthor = prefix + date;
	const auto afterAuthorWidth = st::msgDateFont->width(afterAuthor);
	const auto authorWidth = st::msgDateFont->width(author);
	const auto maxWidth = st::maxSignatureSize;
	_authorElided = !author.isEmpty()
		&& (authorWidth + afterAuthorWidth > maxWidth);
	const auto name = _authorElided
		? st::msgDateFont->elided(author, maxWidth - afterAuthorWidth)
		: author;
	const auto full = (_data.flags & Data::Flag::Imported)
		? (date + ' ' + tr::lng_imported(tr::now))
		: name.isEmpty()
		? date
		: (name + afterAuthor);
	auto helper = Ui::Text::CustomEmojiHelper(
		Core::TextContext({ .session = &_reactionsOwner->session() }));
	auto marked = TextWithEntities();
	marked.append(full);
	_authorEditedDate.setMarkedText(
		st::msgDateTextStyle,
		marked,
		Ui::NameTextOptions(),
		helper.context());
}

void BottomInfo::layoutViewsText() {
	if (!_data.views || (_data.flags & Data::Flag::Sending)) {
		_views.clear();
		return;
	}
	_views.setText(
		st::msgDateTextStyle,
		Lang::FormatCountToShort(std::max(*_data.views, 1)).string,
		Ui::NameTextOptions());
}

void BottomInfo::layoutRepliesText() {
	if (!_data.replies
		|| !*_data.replies
		|| (_data.flags & Data::Flag::RepliesContext)
		|| (_data.flags & Data::Flag::Sending)) {
		_replies.clear();
		return;
	}
	_replies.setText(
		st::msgDateTextStyle,
		Lang::FormatCountToShort(*_data.replies).string,
		Ui::NameTextOptions());
}

QSize BottomInfo::countOptimalSize() {
	auto width = 0;
	if (_data.flags & (Data::Flag::OutLayout | Data::Flag::Sending)) {
		width += st::historySendStateSpace;
	}
	width += _authorEditedDate.maxWidth();
	if (!_views.isEmpty()) {
		width += st::historyViewsSpace
			+ _views.maxWidth()
			+ st::historyViewsWidth;
	}
	if (!_replies.isEmpty()) {
		width += st::historyViewsSpace
			+ _replies.maxWidth()
			+ st::historyViewsWidth;
	}
	if (_data.flags & Data::Flag::Pinned) {
		width += st::historyPinWidth;
	}
	if (_data.flags & Data::Flag::Silent) {
		width += st::historySilentWidth;
	}
	if (_data.flags & Data::Flag::Ephemeral) {
		width += st::historyEphemeralStateWidth;
	}
	const auto dateHeight = st::msgDateFont->height;
	return QSize(width, dateHeight);
}

BottomInfo::Data BottomInfoDataFromMessage(not_null<Message*> message) {
	using Flag = BottomInfo::Data::Flag;
	const auto item = message->data();

	auto result = BottomInfo::Data();
	result.date = message->dateTime();
	if (message->hasOutLayout()) {
		result.flags |= Flag::OutLayout;
	}
	if (message->context() == Context::Replies) {
		result.flags |= Flag::RepliesContext;
	}
	if (item->isPinned() && message->context() != Context::Pinned) {
		result.flags |= Flag::Pinned;
	}
	if (!item->isPost()
		|| !item->hasRealFromId()
		|| !item->history()->peer->asChannel()->signatureProfiles()) {
		if (const auto msgsigned = item->Get<HistoryMessageSigned>()) {
			if (!msgsigned->isAnonymousRank) {
				result.author = msgsigned->author;
			}
		}
	}
	if (const auto editedDate = message->displayedEditDate()) {
		result.flags |= Flag::Edited;
		if (item->history()->session().messagePrimaryEditedDate()) {
			result.flags |= Flag::EditedPrimary;
			result.editedDate = base::unixtime::parse(editedDate);
		}
	}
	if (IsAnchoredEphemeral(item)) {
		result.flags |= Flag::Updated;
	}
	if (const auto views = item->Get<HistoryMessageViews>()) {
		if (views->views.count >= 0) {
			result.views = views->views.count;
		}
		if (views->replies.count >= 0 && !views->commentsMegagroupId) {
			result.replies = views->replies.count;
		}
		if (views->forwardsCount > 0) {
			result.forwardsCount = views->forwardsCount;
		}
	}
	if (item->isSending() || item->hasFailed()) {
		result.flags |= Flag::Sending;
	}
	if (item->isEphemeral()
		&& !message->hasBubble()
		&& (!message->media()
			|| !message->media()->drawsOwnEphemeralBadge())) {
		result.flags |= Flag::Ephemeral;
	}
	const auto forwarded = item->Get<HistoryMessageForwarded>();
	if (forwarded && forwarded->imported) {
		result.flags |= Flag::Imported;
	}
	if (item->awaitingVideoProcessing()) {
		result.flags |= Flag::EstimateDate;
	}
	if (item->isScheduled()) {
		result.scheduleRepeatPeriod = item->scheduleRepeatPeriod();
		if (item->isSilent()) {
			result.flags |= Flag::Silent;
		}
	}
	if (!forwarded) {
		return result;
	}
	if (forwarded->savedFromMsgId && forwarded->savedFromDate) {
		result.date = base::unixtime::parse(forwarded->savedFromDate);
		result.flags |= Flag::ForwardedDate;
	} else if (forwarded->originalDate
		&& (message->context() == Context::SavedSublist
			|| item->history()->peer->isSelf())
		&& !item->externalReply()) {
		result.date = base::unixtime::parse(forwarded->originalDate);
		result.flags |= Flag::ForwardedDate;
	}
	// We don't want to pass and update it in Data for now.
	//if (item->unread()) {
	//	result.flags |= Flag::Unread;
	//}
	return result;
}

} // namespace HistoryView
