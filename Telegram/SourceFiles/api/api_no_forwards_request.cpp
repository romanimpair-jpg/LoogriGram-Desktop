/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_no_forwards_request.h"

#include "apiwrap.h"
#include "core/click_handler_types.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"

namespace Api {
namespace {

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

[[nodiscard]] std::shared_ptr<ClickHandler> MakeHandler(
		not_null<HistoryItem*> item,
		bool accept) {
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
		// LoogriGram: these two buttons also carried a post suggestion's
		// approval and decline, and a gift sale's. Both are purchases and
		// are deleted, so a keyboard that still arrives carrying them does
		// nothing rather than spending anything.
		const auto request = item->Get<HistoryServiceNoForwardsRequest>();
		if (request) {
			RespondToNoForwardsRequest(controller, item, request, accept);
		}
	});
}

} // namespace

std::shared_ptr<ClickHandler> AcceptClickHandler(
		not_null<HistoryItem*> item) {
	return MakeHandler(item, true);
}

std::shared_ptr<ClickHandler> DeclineClickHandler(
		not_null<HistoryItem*> item) {
	return MakeHandler(item, false);
}

} // namespace Api
