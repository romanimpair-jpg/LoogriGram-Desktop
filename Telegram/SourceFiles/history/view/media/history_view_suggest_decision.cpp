/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_suggest_decision.h"

#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "history/view/media/history_view_media_generic.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "ui/chat/chat_style.h"
#include "ui/text/text_utilities.h"
#include "ui/text/format_values.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kFadedOpacity = 0.85;

enum EmojiType {
	kAgreement,
	kCalendar,
	kDecline,
};

[[nodiscard]] const char *Raw(EmojiType type) {
	switch (type) {
	case EmojiType::kAgreement: return "\xf0\x9f\xa4\x9d";
	case EmojiType::kCalendar: return "\xf0\x9f\x93\x86";
	case EmojiType::kDecline: return "\xe2\x9d\x8c";
	}
	Unexpected("EmojiType in Raw.");
}

[[nodiscard]] QString Emoji(EmojiType type) {
	return QString::fromUtf8(Raw(type));
}

// LoogriGram: upstream also told a price change apart. A suggestion shown
// here never has a price, so only the time and the message can change.
struct Changes {
	bool date = false;
	bool message = true;
};
[[nodiscard]] std::optional<Changes> ResolveChanges(
		not_null<HistoryItem*> changed,
		HistoryItem *original) {
	const auto wasSuggest = original
		? original->Get<HistoryMessageSuggestion>()
		: nullptr;
	const auto nowSuggest = changed->Get<HistoryMessageSuggestion>();
	if (!wasSuggest || !nowSuggest) {
		return {};
	}
	auto result = Changes();
	if (wasSuggest->date != nowSuggest->date) {
		result.date = true;
	}
	const auto wasText = original->originalText();
	const auto nowText = changed->originalText();
	const auto mediaSame = [&] {
		const auto wasMedia = original->media();
		const auto nowMedia = changed->media();
		if (!wasMedia && !nowMedia) {
			return true;
		} else if (!wasMedia
			|| !nowMedia
			|| !wasMedia->allowsEditCaption()
			|| !nowMedia->allowsEditCaption()) {
			return false;
		}
		// We treat as "same" only same photo or same file.
		return (wasMedia->photo() == nowMedia->photo())
			&& (wasMedia->document() == nowMedia->document());
	};
	if (!result.date) {
		result.message = true;
	} else if (wasText == nowText && mediaSame()) {
		result.message = false;
	}
	return result;
}

} // namespace

auto GenerateSuggestDecisionMedia(
	not_null<Element*> parent,
	not_null<const HistoryServiceSuggestDecision*> decision)
	-> Fn<void(
		not_null<MediaGeneric*>,
		Fn<void(std::unique_ptr<MediaGenericPart>)>)> {
	return [=](
			not_null<MediaGeneric*> media,
			Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
		const auto peer = parent->history()->peer;
		const auto broadcast = peer->monoforumBroadcast();
		if (!broadcast) {
			return;
		}

		auto pushText = [&](
				TextWithEntities text,
				QMargins margins = {},
				style::align align = style::al_left,
				const base::flat_map<uint16, ClickHandlerPtr> &links = {}) {
			push(std::make_unique<MediaGenericTextPart>(
				std::move(text),
				margins,
				st::defaultTextStyle,
				links,
				Ui::Text::MarkedContext(),
				align));
		};

		// LoogriGram: upstream also drew "not enough Stars or TON" here, and
		// under an agreement what was charged, when the channel is paid and
		// the refund terms. Those answers carry a price and are hidden.
		if (decision->rejected) {
			const auto withComment = !decision->rejectComment.isEmpty();
			pushText(
				TextWithEntities(
				).append(Emoji(kDecline)).append(' ').append(
					(withComment
						? tr::lng_suggest_action_declined_reason
						: tr::lng_suggest_action_declined)(
							tr::now,
							lt_from,
							tr::bold(broadcast->name()),
							tr::marked)),
				(withComment
					? st::chatSuggestInfoTitleMargin
					: st::chatSuggestInfoFullMargin));
			if (withComment) {
				const auto fadedFg = [](const PaintContext &context) {
					auto result = context.st->msgServiceFg()->c;
					result.setAlphaF(result.alphaF() * kFadedOpacity);
					return result;
				};
				push(std::make_unique<TextPartColored>(
					TextWithEntities().append('"').append(
						decision->rejectComment
					).append('"'),
					st::chatSuggestInfoLastMargin,
					fadedFg));
			}
		} else {
			pushText(
				TextWithEntities(
				).append(Emoji(kAgreement)).append(' ').append(
					tr::bold(tr::lng_suggest_action_agreement(tr::now))
				),
				st::chatSuggestInfoTitleMargin,
				style::al_top);
			const auto date = base::unixtime::parse(decision->date);
			pushText(
				TextWithEntities(
				).append(Emoji(kCalendar)).append(' ').append(
					tr::lng_suggest_action_agree_date(
						tr::now,
						lt_channel,
						tr::bold(broadcast->name()),
						lt_date,
						tr::bold(tr::lng_mediaview_date_time(
							tr::now,
							lt_date,
							QLocale().toString(
								date.date(),
								QLocale::ShortFormat),
							lt_time,
							QLocale().toString(
								date.time(),
								QLocale::ShortFormat))),
						tr::marked)),
				st::chatSuggestInfoLastMargin);
		}
	};
}

auto GenerateSuggestRequestMedia(
	not_null<Element*> parent,
	not_null<const HistoryMessageSuggestion*> suggest)
	-> Fn<void(
		not_null<MediaGeneric*>,
		Fn<void(std::unique_ptr<MediaGenericPart>)>)> {
	return [=](
			not_null<MediaGeneric*> media,
			Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
		const auto normalFg = [](const PaintContext &context) {
			return context.st->msgServiceFg()->c;
		};
		const auto fadedFg = [](const PaintContext &context) {
			auto result = context.st->msgServiceFg()->c;
			result.setAlphaF(result.alphaF() * kFadedOpacity);
			return result;
		};
		const auto item = parent->data();
		const auto replyData = item->Get<HistoryMessageReply>();
		const auto original = replyData
			? replyData->resolvedMessage.get()
			: nullptr;
		const auto changes = ResolveChanges(item, original);
		const auto from = item->from();

		auto pushText = [&](
				TextWithEntities text,
				QMargins margins = {},
				style::align align = style::al_left,
				const base::flat_map<uint16, ClickHandlerPtr> &links = {}) {
			push(std::make_unique<MediaGenericTextPart>(
				std::move(text),
				margins,
				st::defaultTextStyle,
				links,
				Ui::Text::MarkedContext(),
				align));
		};

		pushText(
			((!changes && from->isSelf())
				? tr::lng_suggest_action_your(
					tr::now,
					tr::marked)
				: (!changes
					? tr::lng_suggest_action_his
					: changes->message
					? tr::lng_suggest_change_content
					: tr::lng_suggest_change_time)(
						tr::now,
						lt_from,
						tr::bold(from->shortName()),
						tr::marked)),
			st::chatSuggestInfoTitleMargin,
			style::al_top);

		// LoogriGram: a Price row came first. A suggestion has no price here.
		auto entries = std::vector<AttributeTable::Entry>();
		entries.push_back({
			((changes && changes->date)
				? tr::lng_suggest_change_time_label
				: tr::lng_suggest_action_time_label)(tr::now),
			tr::bold(suggest->date
				? Ui::FormatDateTime(base::unixtime::parse(suggest->date))
				: tr::lng_suggest_action_time_any(tr::now)),
		});
		push(std::make_unique<AttributeTable>(
			std::move(entries),
			((changes && changes->message)
				? st::chatSuggestTableMiddleMargin
				: st::chatSuggestTableLastMargin),
			fadedFg,
			normalFg));
		if (changes && changes->message) {
			push(std::make_unique<TextPartColored>(
				tr::lng_suggest_change_text_label(
					tr::now,
					tr::marked),
				st::chatSuggestInfoLastMargin,
				fadedFg));
		}
	};
}

} // namespace HistoryView
