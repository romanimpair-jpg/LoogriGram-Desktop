/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_send.h"

#include "menu/menu_checked_action.h"

#include "api/api_common.h"
#include "base/event_filter.h"
#include "base/unixtime.h"
#include "boxes/abstract_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/shortcuts.h"
#include "history/view/reactions/history_view_reactions_selector.h"
#include "history/view/history_view_schedule_box.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_unread_things.h"
#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "data/data_peer.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_message_reactions.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "window/section_widget.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

#include <QtWidgets/QApplication>

namespace SendMenu {

Fn<void(Action, Details)> DefaultCallback(
		std::shared_ptr<ChatHelpers::Show> show,
		Fn<void(Api::SendOptions)> send) {
	const auto guard = base::make_weak(show->toastParent());
	return [=](Action action, Details details) {
		if (action.type == ActionType::Send) {
			send(action.options);
			return;
		}
		auto box = HistoryView::PrepareScheduleBox(
			guard,
			show,
			details,
			send,
			action.options);
		const auto weak = base::make_weak(box.data());
		show->showBox(std::move(box));
		if (const auto strong = weak.get()) {
			strong->setCloseByOutsideClick(false);
		}
	};
}

// LoogriGram: AttachSendMenuEffect put a message-effect picker above the
// send menu, previewed the chosen effect over a sample message and sent with
// it (the preview also replaced its Send button for a Premium effect). The
// user's decision, 2026-10-04: no big animated views. FillSendMenu and
// SetupMenuAndShortcuts no longer take the Show only the picker needed.

// LoogriGram: a send menu let you set a price in stars for your comment in
// a live stream, so it would be shown in colour and pinned. Deleted with the
// rest of paying to be seen.

FillMenuResult FillSendMenu(
		not_null<Ui::PopupMenu*> menu,
		Details details,
		Fn<void(Action, Details)> action,
		const style::ComposeIcons *iconsOverride,
		std::optional<QPoint> desiredPositionOverride) {
	const auto type = details.type;
	const auto sending = (type != Type::Disabled);
	const auto empty = !sending
		&& (details.spoiler == SpoilerState::None)
		&& (details.caption == CaptionState::None)
		&& (details.photoQuality == PhotoQualityState::None)
		&& (details.cover == CoverState::None);
	if (empty || !action) {
		return FillMenuResult::Skipped;
	}
	const auto &icons = iconsOverride
		? *iconsOverride
		: st::defaultComposeIcons;

	if (sending && type != Type::Reminder) {
		menu->addAction(
			tr::lng_send_silent_message(tr::now),
			[=] { action({ Api::SendOptions{ .silent = true } }, details); },
			&icons.menuMute);
	}
	if (sending && type != Type::SilentOnly) {
		menu->addAction(
			((type == Type::Reminder)
				? tr::lng_reminder_message(tr::now)
				: tr::lng_schedule_message(tr::now)),
			[=] { action({ .type = ActionType::Schedule }, details); },
			&icons.menuSchedule);
	}
	if (sending && type == Type::ScheduledToUser) {
		menu->addAction(
			tr::lng_scheduled_send_until_online(tr::now),
			[=] { action(
				{ Api::DefaultSendWhenOnlineOptions() },
				details); },
			&icons.menuWhenOnline);
	}

	if ((type != Type::Disabled)
		&& ((details.spoiler != SpoilerState::None)
			|| (details.caption != CaptionState::None)
			|| (details.photoQuality != PhotoQualityState::None)
			|| (details.cover != CoverState::None))) {
		menu->addSeparator(&st::expandedMenuSeparator);
	}
	if (details.photoQuality != PhotoQualityState::None) {
		const auto high = (details.photoQuality == PhotoQualityState::High);
		Menu::AddCheckedAction(
			menu,
			tr::lng_send_high_quality(tr::now),
			[=] { action({ .type = high
				? ActionType::PhotoQualityOff
				: ActionType::PhotoQualityOn
			}, details); },
			&icons.menuQualityHigh,
			high);
	}
	if (details.spoiler != SpoilerState::None) {
		const auto spoilered = (details.spoiler == SpoilerState::Enabled);
		Menu::AddCheckedAction(
			menu,
			tr::lng_context_spoiler_effect(tr::now),
			[=] { action({ .type = spoilered
				? ActionType::SpoilerOff
				: ActionType::SpoilerOn
			}, details); },
			&icons.menuSpoiler,
			spoilered);
	}
	if (details.caption != CaptionState::None) {
		const auto above = (details.caption == CaptionState::Above);
		menu->addAction(
			(above
				? tr::lng_caption_move_down(tr::now)
				: tr::lng_caption_move_up(tr::now)),
			[=] { action({ .type = above
				? ActionType::CaptionDown
				: ActionType::CaptionUp
			}, details); },
			above ? &icons.menuBelow : &icons.menuAbove);
	}
	if (details.cover != CoverState::None) {
		menu->addAction(
			tr::lng_context_edit_cover(tr::now),
			[=] { action({ .type = ActionType::EditCover }, details); },
			&icons.menuCover);
		if (details.cover == CoverState::Has) {
			menu->addAction(
				tr::lng_context_clear_cover(tr::now),
				[=] { action({ .type = ActionType::RemoveCover }, details); },
				&icons.menuCoverRemove);
		}
	}
	const auto position = desiredPositionOverride.value_or(QCursor::pos());
	menu->prepareGeometryFor(position);
	return FillMenuResult::Prepared;
}

void SetupMenuAndShortcuts(
		not_null<Ui::RpWidget*> button,
		Fn<Details()> details,
		Fn<void(Action, Details)> action,
		const style::PopupMenu *stOverride,
		const style::ComposeIcons *iconsOverride) {
	const auto menu = std::make_shared<base::unique_qptr<Ui::PopupMenu>>();
	const auto showMenu = [=] {
		*menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			stOverride ? *stOverride : st::popupMenuWithIcons);
		const auto result = FillSendMenu(
			*menu,
			details(),
			action,
			iconsOverride);
		if (result != FillMenuResult::Prepared) {
			return false;
		}
		(*menu)->popupPrepared();
		return true;
	};
	base::install_event_filter(button, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ContextMenu && showMenu()) {
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	Shortcuts::Requests(
	) | rpl::filter([=] {
		return button->isActiveWindow();
	}) | rpl::on_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;

		const auto now = details().type;
		if (now == Type::Disabled) {
			return;
		}
		((now != Type::Reminder)
			&& request->check(Command::SendSilentMessage)
			&& request->handle([=] {
				action({ Api::SendOptions{ .silent = true } }, details());
				return true;
			}))
		||
		((now != Type::SilentOnly)
			&& request->check(Command::ScheduleMessage)
			&& request->handle([=] {
				action({ .type = ActionType::Schedule }, details());
				return true;
			}))
		||
		(request->check(Command::JustSendMessage) && request->handle([=] {
			const auto post = [&](QEvent::Type type) {
				QApplication::postEvent(
					button,
					new QMouseEvent(
						type,
						QPointF(0, 0),
						Qt::LeftButton,
						Qt::LeftButton,
						Qt::NoModifier));
			};
			post(QEvent::MouseButtonPress);
			post(QEvent::MouseButtonRelease);
			return true;
		}));
	}, button->lifetime());
}

void SetupReadAllMenu(
		not_null<Ui::RpWidget*> button,
		Fn<Data::Thread*()> currentThread,
		const QString &text,
		Fn<void(not_null<Data::Thread*>, Fn<void()>)> sendReadRequest) {
	struct State {
		base::unique_qptr<Ui::PopupMenu> menu;
		base::flat_set<base::weak_ptr<Data::Thread>> sentForEntries;
	};
	const auto state = std::make_shared<State>();
	const auto showMenu = [=] {
		const auto thread = base::make_weak(currentThread());
		if (!thread) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		state->menu->addAction(text, [=] {
			const auto strong = thread.get();
			if (!strong || !state->sentForEntries.emplace(thread).second) {
				return;
			}
			sendReadRequest(strong, [=] {
				state->sentForEntries.remove(thread);
			});
		}, &st::menuIconMarkRead);
		state->menu->popup(QCursor::pos());
	};

	base::install_event_filter(button, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ContextMenu) {
			showMenu();
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});
}

void SetupUnreadMentionsMenu(
		not_null<Ui::RpWidget*> button,
		Fn<Data::Thread*()> currentThread) {
	const auto text = tr::lng_context_mark_read_mentions_all(tr::now);
	const auto sendOne = [=](
			base::weak_ptr<Data::Thread> weakThread,
			Fn<void()> done,
			auto resend) -> void {
		const auto thread = weakThread.get();
		if (!thread) {
			done();
			return;
		}
		const auto peer = thread->peer();
		const auto topic = thread->asTopic();
		const auto rootId = topic ? topic->rootId() : 0;
		using Flag = MTPmessages_ReadMentions::Flag;
		peer->session().api().request(MTPmessages_ReadMentions(
			MTP_flags(rootId ? Flag::f_top_msg_id : Flag()),
			peer->input(),
			MTP_int(rootId)
		)).done([=](const MTPmessages_AffectedHistory &result) {
			const auto offset = peer->session().api().applyAffectedHistory(
				peer,
				result);
			if (offset > 0) {
				resend(weakThread, done, resend);
			} else {
				done();
				peer->owner().history(peer)->clearUnreadMentionsFor(rootId);
			}
		}).fail(done).send();
	};
	const auto sendRequest = [=](
			not_null<Data::Thread*> thread,
			Fn<void()> done) {
		sendOne(base::make_weak(thread), std::move(done), sendOne);
	};
	SetupReadAllMenu(button, currentThread, text, sendRequest);
}

void SetupUnreadReactionsMenu(
		not_null<Ui::RpWidget*> button,
		Fn<Data::Thread*()> currentThread) {
	const auto text = tr::lng_context_mark_read_reactions_all(tr::now);
	const auto sendOne = [=](
			base::weak_ptr<Data::Thread> weakThread,
			Fn<void()> done,
			auto resend) -> void {
		const auto thread = weakThread.get();
		if (!thread) {
			done();
			return;
		}
		const auto topic = thread->asTopic();
		const auto sublist = thread->asSublist();
		const auto peer = thread->peer();
		const auto rootId = topic ? topic->rootId() : 0;
		using Flag = MTPmessages_ReadReactions::Flag;
		peer->session().api().request(MTPmessages_ReadReactions(
			MTP_flags((rootId ? Flag::f_top_msg_id : Flag(0))
				| (sublist ? Flag::f_saved_peer_id : Flag(0))),
			peer->input(),
			MTP_int(rootId),
			sublist ? sublist->sublistPeer()->input() : MTPInputPeer()
		)).done([=](const MTPmessages_AffectedHistory &result) {
			const auto offset = peer->session().api().applyAffectedHistory(
				peer,
				result);
			if (offset > 0) {
				resend(weakThread, done, resend);
			} else {
				done();
				peer->owner().history(peer)->clearUnreadReactionsFor(
					rootId,
					sublist);
			}
		}).fail(done).send();
	};
	const auto sendRequest = [=](
			not_null<Data::Thread*> thread,
			Fn<void()> done) {
		sendOne(base::make_weak(thread), std::move(done), sendOne);
	};
	SetupReadAllMenu(button, currentThread, text, sendRequest);
}

void SetupUnreadPollVotesMenu(
		not_null<Ui::RpWidget*> button,
		Fn<Data::Thread*()> currentThread) {
	const auto text = tr::lng_context_mark_read_poll_votes_all(tr::now);
	const auto sendOne = [=](
			base::weak_ptr<Data::Thread> weakThread,
			Fn<void()> done,
			auto resend) -> void {
		const auto thread = weakThread.get();
		if (!thread) {
			done();
			return;
		}
		const auto topic = thread->asTopic();
		const auto peer = thread->peer();
		const auto rootId = topic ? topic->rootId() : 0;
		using Flag = MTPmessages_ReadPollVotes::Flag;
		peer->session().api().request(MTPmessages_ReadPollVotes(
			MTP_flags(rootId ? Flag::f_top_msg_id : Flag(0)),
			peer->input(),
			MTP_int(rootId)
		)).done([=](const MTPmessages_AffectedHistory &result) {
			const auto offset = peer->session().api().applyAffectedHistory(
				peer,
				result);
			if (offset > 0) {
				resend(weakThread, done, resend);
			} else {
				done();
				peer->owner().history(peer)->clearUnreadPollVotesFor(
					rootId);
			}
		}).fail(done).send();
	};
	const auto sendRequest = [=](
			not_null<Data::Thread*> thread,
			Fn<void()> done) {
		sendOne(base::make_weak(thread), std::move(done), sendOne);
	};
	SetupReadAllMenu(button, currentThread, text, sendRequest);
}

} // namespace SendMenu
