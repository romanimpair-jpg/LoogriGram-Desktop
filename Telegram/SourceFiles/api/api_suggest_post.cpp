/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_suggest_post.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "core/click_handler_types.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "history/view/controls/history_view_suggest_options.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"

namespace Api {
namespace {

void SendApproval(
		std::shared_ptr<Main::SessionShow> show,
		not_null<HistoryItem*> item,
		TimeId scheduleDate = 0) {
	using Flag = MTPmessages_ToggleSuggestedPostApproval::Flag;
	const auto suggestion = item->Get<HistoryMessageSuggestion>();
	if (!suggestion
		|| suggestion->accepted
		|| suggestion->rejected
		|| suggestion->requestId) {
		return;
	}

	const auto id = item->fullId();
	const auto session = &show->session();
	const auto finish = [=] {
		if (const auto item = session->data().message(id)) {
			const auto suggestion = item->Get<HistoryMessageSuggestion>();
			if (suggestion) {
				suggestion->requestId = 0;
			}
		}
	};
	suggestion->requestId = session->api().request(
		MTPmessages_ToggleSuggestedPostApproval(
			MTP_flags(scheduleDate ? Flag::f_schedule_date : Flag()),
			item->history()->peer->input(),
			MTP_int(item->id.bare),
			MTP_int(scheduleDate),
			MTPstring()) // reject_comment
	).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		finish();
	}).fail([=](const MTP::Error &error) {
		show->showToast(error.type());
		finish();
	}).send();
}

void ConfirmApproval(
		std::shared_ptr<Main::SessionShow> show,
		not_null<HistoryItem*> item,
		TimeId scheduleDate = 0,
		Fn<void()> accepted = nullptr) {
	const auto suggestion = item->Get<HistoryMessageSuggestion>();
	if (!suggestion
		|| suggestion->accepted
		|| suggestion->rejected
		|| suggestion->requestId) {
		return;
	}
	const auto id = item->fullId();
	const auto admin = item->history()->amMonoforumAdmin();
	const auto peer = item->history()->peer;
	const auto broadcast = peer->monoforumBroadcast();
	const auto channelName = (broadcast ? broadcast : peer)->name();
	// LoogriGram: upstream checked the payer's balance here, offered to top
	// it up, and added what would be paid or received after commission. A
	// suggestion that reaches this has no price, so approving it publishes
	// the post and nothing else.
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto callback = std::make_shared<Fn<void()>>();
		auto text = admin
			? tr::lng_suggest_accept_text(
				tr::now,
				lt_from,
				tr::bold(item->from()->shortName()),
				tr::marked)
			: tr::lng_suggest_accept_text_to(
				tr::now,
				lt_channel,
				tr::bold(channelName),
				tr::marked);
		Ui::ConfirmBox(box, {
			.text = std::move(text),
			.confirmed = [=](Fn<void()> close) { (*callback)(); close(); },
			.confirmText = tr::lng_suggest_accept_send(),
			.title = tr::lng_suggest_accept_title(),
		});
		*callback = [=, weak = base::make_weak(box)] {
			if (const auto onstack = accepted) {
				onstack();
			}
			const auto item = show->session().data().message(id);
			if (!item) {
				return;
			}
			SendApproval(show, item, scheduleDate);
			if (const auto strong = weak.get()) {
				strong->closeBox();
			}
		};
	}));
}

void SendDecline(
		std::shared_ptr<Main::SessionShow> show,
		not_null<HistoryItem*> item,
		const QString &comment) {
	using Flag = MTPmessages_ToggleSuggestedPostApproval::Flag;
	const auto suggestion = item->Get<HistoryMessageSuggestion>();
	if (!suggestion
		|| suggestion->accepted
		|| suggestion->rejected
		|| suggestion->requestId) {
		return;
	}

	const auto id = item->fullId();
	const auto session = &show->session();
	const auto finish = [=] {
		if (const auto item = session->data().message(id)) {
			const auto suggestion = item->Get<HistoryMessageSuggestion>();
			if (suggestion) {
				suggestion->requestId = 0;
			}
		}
	};
	suggestion->requestId = session->api().request(
		MTPmessages_ToggleSuggestedPostApproval(
			MTP_flags(Flag::f_reject
				| (comment.isEmpty() ? Flag() : Flag::f_reject_comment)),
			item->history()->peer->input(),
			MTP_int(item->id.bare),
			MTPint(), // schedule_date
			MTP_string(comment))
	).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		finish();
	}).fail([=](const MTP::Error &error) {
		show->showToast(error.type());
		finish();
	}).send();
}

void RequestApprovalDate(
		std::shared_ptr<Main::SessionShow> show,
		not_null<HistoryItem*> item) {
	const auto id = item->fullId();
	const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
	const auto close = [=] {
		if (const auto strong = weak->get()) {
			strong->closeBox();
		}
	};
	const auto done = [=](TimeId result) {
		if (const auto item = show->session().data().message(id)) {
			ConfirmApproval(show, item, result, close);
		} else {
			close();
		}
	};
	using namespace HistoryView;
	auto dateBox = Box(ChooseSuggestTimeBox, SuggestTimeBoxArgs{
		.session = &show->session(),
		.done = done,
		.mode = SuggestMode::Publish,
	});
	*weak = dateBox.data();
	show->show(std::move(dateBox));
}

void RequestDeclineComment(
		std::shared_ptr<Main::SessionShow> show,
		not_null<HistoryItem*> item) {
	const auto id = item->fullId();
	const auto admin = item->history()->amMonoforumAdmin();
	const auto peer = item->history()->peer;
	const auto broadcast = peer->monoforumBroadcast();
	const auto channelName = (broadcast ? broadcast : peer)->name();
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto callback = std::make_shared<Fn<void()>>();
		Ui::ConfirmBox(box, {
			.text = (admin
				? tr::lng_suggest_decline_text(
					lt_from,
					rpl::single(tr::bold(item->from()->shortName())),
					tr::marked)
				: tr::lng_suggest_decline_text_to(
					lt_channel,
					rpl::single(tr::bold(channelName)),
					tr::marked)),
			.confirmed = [=](Fn<void()> close) { (*callback)(); close(); },
			.confirmText = tr::lng_suggest_action_decline(),
			.confirmStyle = &st::attentionBoxButton,
			.title = tr::lng_suggest_decline_title(),
		});
		const auto reason = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::factcheckField,
			Ui::InputField::Mode::NoNewlines,
			tr::lng_suggest_decline_reason()));
		box->setFocusCallback([=] {
			reason->setFocusFast();
		});
		*callback = [=, weak = base::make_weak(box)] {
			const auto item = show->session().data().message(id);
			if (!item) {
				return;
			}
			SendDecline(show, item, reason->getLastText().trimmed());
			if (const auto strong = weak.get()) {
				strong->closeBox();
			}
		};
		reason->submits(
		) | rpl::on_next([=](Qt::KeyboardModifiers modifiers) {
			if (!(modifiers & Qt::ShiftModifier)) {
				(*callback)();
			}
		}, box->lifetime());
	}));
}

void SendSuggest(
		std::shared_ptr<Main::SessionShow> show,
		not_null<HistoryItem*> item,
		Fn<void(SuggestOptions&)> modify,
		Fn<void()> done = nullptr) {
	const auto suggestion = item->Get<HistoryMessageSuggestion>();
	const auto isForward = item->Get<HistoryMessageForwarded>();
	auto action = SendAction(item->history());
	action.options.suggest.exists = 1;
	if (suggestion) {
		action.options.suggest.date = suggestion->date;
	}
	modify(action.options.suggest);
	action.replyTo.monoforumPeerId = item->history()->amMonoforumAdmin()
		? item->sublistPeerId()
		: PeerId();
	action.replyTo.messageId = item->fullId();

	// LoogriGram: upstream ran the per-message payment check here, for a
	// chat that charges to be written to. Nothing here pays to send.
	show->session().api().sendAction(action);
	show->session().api().forwardMessages({
		.items = { item },
		.options = (isForward
			? Data::ForwardOptions::PreserveInfo
			: Data::ForwardOptions::NoSenderNames),
	}, action);
	if (const auto onstack = done) {
		onstack();
	}
}

void SuggestApprovalDate(
		std::shared_ptr<Main::SessionShow> show,
		not_null<HistoryItem*> item) {
	const auto suggestion = item->Get<HistoryMessageSuggestion>();
	if (!suggestion) {
		return;
	}
	const auto id = item->fullId();
	const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
	const auto done = [=](TimeId result) {
		const auto item = show->session().data().message(id);
		if (!item) {
			return;
		}
		const auto close = [=] {
			if (const auto strong = weak->get()) {
				strong->closeBox();
			}
		};
		SendSuggest(
			show,
			item,
			[=](SuggestOptions &options) { options.date = result; },
			close);
	};
	using namespace HistoryView;
	auto dateBox = Box(ChooseSuggestTimeBox, SuggestTimeBoxArgs{
		.session = &show->session(),
		.done = done,
		.value = suggestion->date,
		.mode = SuggestMode::Change,
	});
	*weak = dateBox.data();
	show->show(std::move(dateBox));
}

void RespondToNoForwardsRequest(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		not_null<HistoryServiceNoForwardsRequest*> request,
		bool accept) {
	if (request->requestId) {
		return;
	}
	const auto id = item->fullId();
	const auto session = &item->history()->session();
	const auto peer = item->history()->peer;
	const auto msgId = item->id;
	const auto finish = [=] {
		if (const auto item = session->data().message(id)) {
			if (const auto r = item->Get<HistoryServiceNoForwardsRequest>()) {
				r->requestId = 0;
			}
		}
	};
	using Flag = MTPmessages_ToggleNoForwards::Flag;
	request->requestId = session->api().request(MTPmessages_ToggleNoForwards(
		MTP_flags(Flag::f_request_msg_id),
		peer->input(),
		MTP_bool(!accept),
		MTP_int(msgId)
	)).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		finish();
	}).fail([=](const MTP::Error &error) {
		controller->showToast(error.type());
		finish();
	}).send();
}

} // namespace

std::shared_ptr<ClickHandler> AcceptClickHandler(
		not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	const auto id = item->fullId();
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		const auto controller = my.sessionWindow.get();
		if (!controller || &controller->session() != session) {
			return;
		}
		const auto item = session->data().message(id);
		if (!item) {
			return;
		}
		const auto request = item->Get<HistoryServiceNoForwardsRequest>();
		if (request) {
			RespondToNoForwardsRequest(controller, item, request, true);
			return;
		}
		// LoogriGram: a gift sale was accepted here too. Gifts are deleted,
		// and their offers are hidden before they are parsed.
		const auto suggestion = item->Get<HistoryMessageSuggestion>();
		if (!suggestion) {
			return;
		} else if (!suggestion->date) {
			RequestApprovalDate(controller->uiShow(), item);
		} else {
			ConfirmApproval(controller->uiShow(), item);
		}
	});
}

std::shared_ptr<ClickHandler> DeclineClickHandler(
		not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	const auto id = item->fullId();
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		const auto controller = my.sessionWindow.get();
		if (!controller || &controller->session() != session) {
			return;
		}
		const auto item = session->data().message(id);
		if (!item) {
			return;
		}
		const auto request = item->Get<HistoryServiceNoForwardsRequest>();
		if (request) {
			RespondToNoForwardsRequest(controller, item, request, false);
			return;
		}
		// LoogriGram: a gift sale was declined here too. See above.
		if (item->Get<HistoryMessageSuggestion>()) {
			RequestDeclineComment(controller->uiShow(), item);
		}
	});
}

std::shared_ptr<ClickHandler> SuggestChangesClickHandler(
		not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	const auto id = item->fullId();
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		const auto window = my.sessionWindow.get();
		if (!window || &window->session() != session) {
			return;
		}
		// LoogriGram: upstream opened a menu here - edit the message, edit
		// the price, edit the time. A suggestion here has no price, and
		// editing the message went with the edit-as-suggestion path, so this
		// goes straight to the time.
		if (const auto item = session->data().message(id)) {
			SuggestApprovalDate(window->uiShow(), item);
		}
	});
}

} // namespace Api
