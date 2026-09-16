/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_self_forwards_tagger.h"

#include "base/event_filter.h"
#include "base/timer_rpl.h"
#include "boxes/choose_filter_box.h"
#include "data/data_chat_filters.h"
#include "data/data_session.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/rect.h"
#include "ui/toast/toast_widget.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

namespace HistoryView {
namespace {

constexpr auto kInitTimer = crl::time(3000);
constexpr auto kTimerOnLeave = crl::time(2000);

} // namespace

SelfForwardsTagger::SelfForwardsTagger(
	not_null<Window::SessionController*> controller,
	not_null<Ui::RpWidget*> parent,
	Fn<Ui::RpWidget*()> listWidget,
	not_null<QWidget*> scroll,
	Fn<History*()> history)
: _controller(controller)
, _parent(parent)
, _listWidget(std::move(listWidget))
, _scroll(scroll)
, _history(std::move(history)) {
	setup();
}

SelfForwardsTagger::~SelfForwardsTagger() = default;

// LoogriGram: this also offered to tag messages just forwarded to Saved
// Messages, which only happened for premium accounts. What is left is the
// offer to add a just-joined chat to a folder.
void SelfForwardsTagger::setup() {
	_controller->session().data().recentJoinChat(
	) | rpl::on_next([=](const Data::RecentJoinChat &data) {
		if (!_controller->session().data().chatsFilters().has()) {
			return;
		}
		const auto history = _history ? _history() : nullptr;
		if (!history || history->peer->id != data.fromPeerId) {
			return;
		}
		const auto peerId = data.joinedPeerId;
		if (const auto peer = _controller->session().data().peer(peerId)) {
			showChannelFilterToast(peer);
		}
	}, _lifetime);
}

void SelfForwardsTagger::showChannelFilterToast(not_null<PeerData*> peer) {
	hideToast();
	const auto toastText = peer->isChannel() && !peer->isMegagroup()
		? tr::lng_add_channel_to_filter_selector(tr::now)
		: tr::lng_add_group_to_filter_selector(tr::now);
	_toast = Ui::Toast::Show(_scroll, Ui::Toast::Config{
		.text = { .text = toastText },
		.iconLottie = u"toast/chats_filter_in"_q,
		.iconPadding = st::selfForwardsTaggerIconPadding,
		.st = &st::joinChatAddToFilterToast,
		.attach = RectPart::Top,
		.acceptinput = true,
		.infinite = true,
	});
	if (const auto strong = _toast.get()) {
		const auto widget = strong->widget();
		const auto rightButton = createRightButton(widget);
		const auto history = peer->owner().history(peer);

		const auto state = widget->lifetime().make_state<ToastTimerState>();

		rightButton->setClickedCallback([=] {
			state->expanded = true;
			state->timerLifetime.destroy();
			const auto menu = Ui::CreateChild<Ui::PopupMenu>(
				rightButton,
				st::foldersMenu);
			menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
			FillChooseFilterMenu(_controller, menu, history);
			if (!menu->empty()) {
				menu->popup(
					rightButton->mapToGlobal(
						QPoint(
							rightButton->width(),
							rightButton->height() + rightButton->y())));
				QObject::connect(menu, &QObject::destroyed, [=] {
					hideToast();
				});
			} else {
				hideToast();
			}
		});

		setupToastTimer(widget, state, [=] { hideToast(); });
	}
}

not_null<Ui::AbstractButton*> SelfForwardsTagger::createRightButton(
		not_null<Ui::RpWidget*> widget) {
	const auto button = Ui::CreateChild<Ui::IconButton>(
		widget.get(),
		st::joinChatAddToFilterToastButton);
	widget->sizeValue() | rpl::on_next([=](const QSize &size) {
		button->moveToRight(
			st::lineWidth * 4,
			(size.height() - button->height()) / 2);
	}, button->lifetime());

	button->show();
	return button;
}

void SelfForwardsTagger::setupToastTimer(
		not_null<Ui::RpWidget*> widget,
		not_null<ToastTimerState*> state,
		Fn<void()> hideCallback) {
	const auto restartTimer = [=](crl::time ms) {
		state->timerLifetime.destroy();
		base::timer_once(ms) | rpl::on_next([=] {
			hideCallback();
		}, state->timerLifetime);
	};

	base::install_event_filter(widget, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::MouseButtonPress) {
			state->timerLifetime.destroy();
			return base::EventFilterResult::Continue;
		} else if (!state->expanded && event->type() == QEvent::Enter) {
			state->timerLifetime.destroy();
			return base::EventFilterResult::Continue;
		} else if (!state->expanded && event->type() == QEvent::Leave) {
			restartTimer(kTimerOnLeave);
			return base::EventFilterResult::Continue;
		}
		return base::EventFilterResult::Continue;
	}, widget->lifetime());

	restartTimer(kInitTimer);
}

void SelfForwardsTagger::hideToast() {
	if (const auto strong = _toast.get()) {
		strong->hideAnimated();
	}
}

} // namespace HistoryView
