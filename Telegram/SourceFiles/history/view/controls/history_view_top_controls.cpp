/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_top_controls.h"

#include "apiwrap.h"
#include "base/binary_guard.h"
#include "base/call_delayed.h"
#include "boxes/peers/edit_peer_requests_box.h"
#include "calls/calls_instance.h"
#include "core/application.h"
#include "data/data_channel.h"
#include "data/data_changes.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_shared_media.h"
#include "data/data_thread.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_contact_status.h"
#include "history/view/history_view_group_call_bar.h"
#include "history/view/history_view_pinned_bar.h"
#include "history/view/history_view_pinned_section.h"
#include "history/view/history_view_pinned_tracker.h"
#include "history/view/history_view_requests_bar.h"
#include "history/view/history_view_translate_bar.h"
#include "history/view/history_view_translate_tracker.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "ui/chat/group_call_bar.h"
#include "ui/chat/pinned_bar.h"
#include "ui/chat/requests_bar.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/elastic_scroll.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

namespace HistoryView {
namespace {

rpl::producer<Ui::MessageBarContent> RootViewContent(
		not_null<History*> history,
		MsgId rootId,
		Fn<void()> repaint) {
	return MessageBarContentByItemId(
		&history->session(),
		FullMsgId(history->peer->id, rootId),
		std::move(repaint)
	) | rpl::map([=](Ui::MessageBarContent &&content) {
		const auto item = history->owner().message(history->peer, rootId);
		if (!item) {
			content.text = tr::link(tr::lng_deleted_message(tr::now));
		}
		PeerData *sender = history->peer;
		if (item && item->discussionPostOriginalSender()) {
			sender = item->discussionPostOriginalSender();
		}
		content.title = sender->name().isEmpty()
			? "Message"
			: sender->name();
		return std::move(content);
	});
}

} // namespace

TopControls::TopControls(
	not_null<Ui::RpWidget*> parent,
	TopControlsDescriptor descriptor)
: _parent(parent)
, _controller(descriptor.controller)
, _history(descriptor.history)
, _repliesRootId(descriptor.repliesRootId)
, _topic(descriptor.topic)
, _sublist(descriptor.sublist)
, _monoforumPeerId(descriptor.monoforumPeerId)
, _scroll(descriptor.scroll)
, _list(descriptor.list)
, _keyboardReservedHeight(std::move(descriptor.keyboardReservedHeight))
, _showAtStart(std::move(descriptor.showAtStart))
, _showAtPosition(std::move(descriptor.showAtPosition))
, _wrap(std::make_unique<Ui::RpWidget>(_parent))
, _topBars(std::make_unique<Ui::RpWidget>(_wrap.get())) {
	_topBars->show();
	_wrap->show();
	setupTranslateBar();
	rebuildModeSensitiveBars();
	updateLayout();

	_moveWithTopDelta = std::move(descriptor.moveWithTopDelta);
	_relayout = std::move(descriptor.relayout);
	_relayoutWithScrollTopDelta = std::move(
		descriptor.relayoutWithScrollTopDelta);
}

TopControls::~TopControls() = default;

void TopControls::move(int x, int y) {
	_wrap->move(x, y);
}

void TopControls::resizeToWidth(int width) {
	if (_width == width) {
		return;
	}
	_width = width;
	updateLayout();
}

void TopControls::raise() {
	_wrap->raise();
	updateZOrder();
}

void TopControls::show() {
	_wrap->show();
	updateZOrder();
}

void TopControls::hide() {
	_wrap->hide();
}

void TopControls::subscribeToPinnedMessages() {
	const auto thread = activeThread();
	Expects(thread != nullptr);
	const auto migrated = migratedPeer()
		? _history->migrateFrom()
		: nullptr;

	const auto was = height();
	resetPinnedState();
	updateLayout();
	applyHeightChangeWithRelayout(was, height());

	using EntryUpdateFlag = Data::EntryUpdate::Flag;
	_history->session().changes().entryUpdates(
		EntryUpdateFlag::HasPinnedMessages
	) | rpl::on_next([=](const Data::EntryUpdate &update) {
		if (_pinnedTracker
			&& (update.flags & EntryUpdateFlag::HasPinnedMessages)
			&& ((thread == update.entry.get())
				|| (migrated == update.entry.get()))) {
			checkPinnedBarState();
		}
	}, _pinnedLifetime);

	setupPinnedTracker();
}

void TopControls::setAnimatingMode(bool enabled) {
	_animatingMode = enabled;
}

void TopControls::finishAnimating() {
	if (_groupCallBar) {
		_groupCallBar->finishAnimating();
	}
	if (_requestsBar) {
		_requestsBar->finishAnimating();
	}
	if (_translateBar) {
		_translateBar->finishAnimating();
	}
	if (_pinnedPill) {
		_pinnedPill->finishAnimating();
	}
	if (!_animatingMode) {
		return;
	}
	_animatingMode = false;
}

void TopControls::setRepliesRootId(MsgId repliesRootId) {
	if (_repliesRootId == repliesRootId) {
		return;
	}
	const auto was = height();
	_repliesRootId = repliesRootId;
	resetPinnedState();
	_repliesRootView = nullptr;
	_repliesRootViewHeight = 0;
	_repliesRootViewInited = false;
	_repliesRootViewInitScheduled = false;
	_shownRepliesRootItem = nullptr;
	rebuildModeSensitiveBars();
	updateLayout();
	applyHeightChangeWithRelayout(was, height());
}

void TopControls::setTopic(Data::ForumTopic *topic) {
	if (_topic == topic) {
		return;
	}
	const auto was = height();
	_topic = topic;
	resetPinnedState();
	_repliesRootView = nullptr;
	_repliesRootViewHeight = 0;
	_repliesRootViewInited = false;
	_repliesRootViewInitScheduled = false;
	_topicReopenBar = nullptr;
	_topicReopenBarHeight = 0;
	_shownRepliesRootItem = nullptr;
	rebuildModeSensitiveBars();
	updateLayout();
	applyHeightChangeWithRelayout(was, height());
}

Data::Thread *TopControls::activeThread() const {
	return _sublist
		? static_cast<Data::Thread*>(_sublist)
		: _topic
		? static_cast<Data::Thread*>(_topic)
		: static_cast<Data::Thread*>(_history.get());
}

PeerData *TopControls::migratedPeer() const {
	if (_repliesRootId || _topic || _sublist) {
		return nullptr;
	}
	const auto migrated = _history->migrateFrom();
	return migrated ? migrated->peer.get() : nullptr;
}

bool TopControls::showInForum() const {
	const auto thread = activeThread();
	return thread && thread->topicRootId();
}

void TopControls::setRepliesRootVisible(bool shown) {
	if (_animatingMode || (_repliesRootVisible.current() == shown)) {
		return;
	} else if (_sublist) {
		_repliesRootVisible = shown;
		return;
	} else if (!_repliesRootId) {
		return;
	} else if (!_topic) {
		if (!_repliesRootViewInitScheduled) {
			const auto next = shown ? st::historyReplyHeight : 0;
			if (next != _repliesRootViewHeight) {
				const auto was = height();
				_repliesRootViewHeight = next;
				updateLayout();
				applyHeightChangeWithTopMoved(was, height());
			}
		}
		_repliesRootVisible = shown;
		if (_repliesRootView && !_repliesRootViewInited) {
			_repliesRootView->finishAnimating();
			if (!_repliesRootViewInitScheduled) {
				_repliesRootViewInitScheduled = true;
				base::call_delayed(0, _wrap.get(), [=] {
					_repliesRootViewInited = true;
				});
			}
		}
	} else {
		_repliesRootVisible = shown;
	}
}

rpl::producer<bool> TopControls::repliesRootVisibleValue() const {
	return _repliesRootVisible.value();
}

void TopControls::updatePinnedViewer() {
	if (_scroll->isHidden() || !_pinnedTracker) {
		return;
	}
	const auto migrated = migratedPeer();
	const auto visibleBottom = _scroll->scrollTop() + _scroll->height();
	auto [view, offset] = _list->findViewForPinnedTracking(visibleBottom);
	const auto lessThanId = !view
		? (ServerMaxMsgId - 1)
		: (migrated && (view->history() != _history))
		? (view->data()->id + (offset > 0 ? 1 : 0) - ServerMaxMsgId)
		: (view->data()->id + (offset > 0 ? 1 : 0));
	const auto lastClickedId = !_pinnedClickedId
		? (ServerMaxMsgId - 1)
		: (migrated && !peerIsChannel(_pinnedClickedId.peer))
		? (_pinnedClickedId.msg - ServerMaxMsgId)
		: _pinnedClickedId.msg;
	if (_pinnedClickedId
		&& lessThanId <= lastClickedId
		&& !_list->animatedScrolling()) {
		_pinnedClickedId = FullMsgId();
	}
	if (_pinnedClickedId && !_minPinnedId) {
		const auto fullHistory = !_repliesRootId && !_topic && !_sublist;
		_minPinnedId = Data::ResolveMinPinnedId(
			_history->peer,
			fullHistory ? MsgId(0) : _repliesRootId,
			fullHistory ? PeerId(0) : _monoforumPeerId,
			migrated);
	}
	if (_pinnedClickedId && _minPinnedId && _minPinnedId >= _pinnedClickedId) {
		_pinnedTracker->trackAround(ServerMaxMsgId - 1);
	} else {
		_pinnedTracker->trackAround(std::min(lessThanId, lastClickedId));
	}
}

void TopControls::checkLastPinnedClickedIdReset(
		int wasScrollTop,
		int nowScrollTop) {
	if (_scroll->isHidden() || !_pinnedTracker) {
		return;
	}
	if (wasScrollTop < nowScrollTop && _pinnedClickedId) {
		_pinnedClickedId = FullMsgId();
		_minPinnedId = std::nullopt;
		updatePinnedViewer();
	}
}

void TopControls::addTranslatedItems(not_null<TranslateTracker*> tracker) {
	if (_shownRepliesRootItem) {
		tracker->add(_shownRepliesRootItem);
	}
}

int TopControls::height() const {
	return _height.current();
}

rpl::producer<int> TopControls::heightValue() const {
	return _height.value();
}

void TopControls::setupRootView() {
	_repliesRootView = std::make_unique<Ui::PinnedBar>(_topBars.get(), [=] {
		return _controller->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
	}, _controller->gifPauseLevelChanged());
	_repliesRootView->setContent(rpl::combine(
		RootViewContent(
			_history,
			_repliesRootId,
			[bar = _repliesRootView.get()] { bar->customEmojiRepaint(); }),
		_repliesRootVisible.value()
	) | rpl::map([=](Ui::MessageBarContent &&content, bool show) {
		const auto shown = !content.title.isEmpty() && !content.text.empty();
		crl::on_main(_wrap.get(), [=] {
			_shownRepliesRootItem = shown
				? _history->owner().message(_history->peer->id, _repliesRootId)
				: nullptr;
		});
		return show ? std::move(content) : Ui::MessageBarContent();
	}));

	_controller->adaptive().oneColumnValue(
	) | rpl::on_next([=](bool one) {
		_repliesRootView->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _repliesRootView->lifetime());

	_repliesRootView->barClicks(
	) | rpl::on_next([=] {
		if (_showAtStart) {
			_showAtStart();
		}
	}, _repliesRootView->lifetime());

	_repliesRootViewHeight = 0;
	_repliesRootView->heightValue(
	) | rpl::on_next([=](int height) {
		if (height == _repliesRootViewHeight) {
			return;
		}
		const auto was = this->height();
		_repliesRootViewHeight = height;
		updateLayout();
		applyHeightChangeWithTopMoved(was, this->height());
	}, _repliesRootView->lifetime());
}

void TopControls::setupTopicReopenBar() {
	_topicReopenBar = std::make_unique<TopicReopenBar>(_topBars.get(), _topic);
	_topicReopenBar->bar().setVisible(true);
	_topicReopenBarHeight = _topicReopenBar->bar().height();
	_topicReopenBar->bar().heightValue(
	) | rpl::on_next([=] {
		const auto height = _topicReopenBar->bar().height();
		if (height == _topicReopenBarHeight) {
			return;
		}
		const auto was = this->height();
		_topicReopenBarHeight = height;
		updateLayout();
		const auto delta = this->height() - was;
		if (!delta) {
			return;
		} else if (_relayoutWithScrollTopDelta) {
			_relayoutWithScrollTopDelta(delta);
		} else {
			applyHeightChangeWithRelayout(was, this->height());
		}
	}, _topicReopenBar->bar().lifetime());
}

void TopControls::setupTranslateBar() {
	_translateBar = std::make_unique<TranslateBar>(
		_topBars.get(),
		_controller,
		_history);
	_controller->adaptive().oneColumnValue(
	) | rpl::on_next([=, raw = _translateBar.get()](bool one) {
		raw->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _translateBar->lifetime());

	_translateBarHeight = 0;
	_translateBar->heightValue(
	) | rpl::on_next([=](int height) {
		if (height == _translateBarHeight) {
			return;
		}
		const auto was = this->height();
		_translateBarHeight = height;
		updateLayout();
		applyHeightChangeWithTopMoved(was, this->height());
	}, _translateBar->lifetime());

	_translateBar->finishAnimating();
}

void TopControls::setupGroupCallBar() {
	const auto peer = _history->peer;
	if (!peer->isChannel() && !peer->isChat()) {
		return;
	}
	_groupCallBar = std::make_unique<Ui::GroupCallBar>(
		_topBars.get(),
		GroupCallBarContentByPeer(
			peer,
			st::historyGroupCallUserpics.size,
			showInForum()),
		Core::App().appDeactivatedValue());

	_controller->adaptive().oneColumnValue(
	) | rpl::on_next([=](bool one) {
		_groupCallBar->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _groupCallBar->lifetime());

	rpl::merge(
		_groupCallBar->barClicks(),
		_groupCallBar->joinClicks()
	) | rpl::on_next([=] {
		if (const auto peer = _history->peer; peer->groupCall()) {
			_controller->startOrJoinGroupCall(peer, {});
		}
	}, _groupCallBar->lifetime());

	_groupCallBarHeight = 0;
	_groupCallBar->heightValue(
	) | rpl::on_next([=](int height) {
		if (height == _groupCallBarHeight) {
			return;
		}
		const auto was = this->height();
		_groupCallBarHeight = height;
		updateLayout();
		applyHeightChangeWithTopMoved(was, this->height());
	}, _groupCallBar->lifetime());
}

void TopControls::setupRequestsBar() {
	const auto peer = _history->peer;
	if (!peer->isChannel() && !peer->isChat()) {
		return;
	}
	_requestsBar = std::make_unique<Ui::RequestsBar>(
		_topBars.get(),
		RequestsBarContentByPeer(
			peer,
			st::historyRequestsUserpics.size,
			showInForum()));

	_controller->adaptive().oneColumnValue(
	) | rpl::on_next([=](bool one) {
		_requestsBar->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _requestsBar->lifetime());

	_requestsBar->barClicks(
	) | rpl::on_next([=] {
		RequestsBoxController::Start(_controller, _history->peer);
	}, _requestsBar->lifetime());

	_requestsBarHeight = 0;
	_requestsBar->heightValue(
	) | rpl::on_next([=](int height) {
		if (height == _requestsBarHeight) {
			return;
		}
		const auto was = this->height();
		_requestsBarHeight = height;
		updateLayout();
		applyHeightChangeWithTopMoved(was, this->height());
	}, _requestsBar->lifetime());
}

void TopControls::setupPeerBars() {
	if (!_contactStatus) {
		_contactStatus = std::make_unique<ContactStatus>(
			_controller,
			_topBars.get(),
			_history->peer,
			showInForum());
		_contactStatus->show();
		_contactStatusHeight = 0;
		_contactStatus->bar().heightValue(
		) | rpl::on_next([=] {
			const auto height = _contactStatus->bar().height();
			if (height == _contactStatusHeight) {
				return;
			}
			const auto was = this->height();
			_contactStatusHeight = height;
			updateLayout();
			applyHeightChangeWithRelayout(was, this->height());
		}, _contactStatus->bar().lifetime());
	}
}

void TopControls::setupPinnedTracker() {
	const auto thread = activeThread();
	Expects(thread != nullptr);

	_pinnedTracker = std::make_unique<PinnedTracker>(thread);
	setupPinnedPill();
	const auto fullHistory = !_repliesRootId && !_topic && !_sublist;
	if (fullHistory) {
		checkPinnedBarState();
		return;
	}

	SharedMediaViewer(
		&_history->session(),
		Storage::SharedMediaKey(
			_history->peer->id,
			_repliesRootId,
			_monoforumPeerId,
			Storage::SharedMediaType::Pinned,
			ServerMaxMsgId - 1),
		1,
		1
	) | rpl::filter([=](const SparseIdsSlice &result) {
		return result.fullCount().has_value();
	}) | rpl::on_next([=](const SparseIdsSlice &result) {
		thread->setHasPinnedMessages(*result.fullCount() != 0);
		if (result.skippedAfter() == 0) {
			auto &settings = _history->session().settings();
			const auto peerId = _history->peer->id;
			const auto hiddenId = settings.hiddenPinnedMessageId(
				peerId,
				_repliesRootId,
				_monoforumPeerId);
			const auto last = result.size() ? result[result.size() - 1] : 0;
			if (hiddenId && hiddenId != last) {
				settings.setHiddenPinnedMessageId(
					peerId,
					_repliesRootId,
					_monoforumPeerId,
					0);
				_history->session().saveSettingsDelayed();
			}
		}
		checkPinnedBarState();
	}, _pinnedLifetime);
}

// LoogriGram: the pinned messages pill, in place of upstream's pinned bar
// across the top. See HistoryView::PinnedPill and HistoryWidget's own.
void TopControls::setupPinnedPill() {
	const auto fullHistory = !_repliesRootId && !_topic && !_sublist;
	const auto migrated = migratedPeer();
	const auto topicRootId = fullHistory ? MsgId(0) : _repliesRootId;
	const auto monoforumPeerId = fullHistory ? PeerId(0) : _monoforumPeerId;
	const auto showAll = [=] {
		const auto id = _pinnedTracker
			? _pinnedTracker->currentMessageId().message
			: FullMsgId();
		if (fullHistory) {
			_controller->showSection(std::make_shared<PinnedMemento>(
				_history,
				((!id || !migrated || peerIsChannel(id.peer))
					? id.msg
					: (id.msg - ServerMaxMsgId))));
		} else if (const auto thread = activeThread()) {
			_controller->showSection(
				std::make_shared<PinnedMemento>(thread, id.msg));
		}
	};
	_pinnedPill = base::make_unique_q<PinnedPill>(
		_scroll.get(),
		_controller->chatStyle(),
		[=](QEvent *e) { return _scroll->viewportEvent(e); },
		[=] { hidePinnedMessage(); },
		showAll);

	auto pinnedCount = Info::Profile::SharedMediaCountValue(
		_history->peer,
		topicRootId,
		monoforumPeerId,
		migrated,
		Storage::SharedMediaType::Pinned
	) | rpl::distinct_until_changed(
	) | rpl::map([=](int count) {
		if (_pinnedClickedId) {
			_pinnedClickedId = FullMsgId();
			_minPinnedId = std::nullopt;
			updatePinnedViewer();
		}
		return count;
	});
	rpl::combine(
		std::move(pinnedCount),
		_pinnedPillAllowed.value(),
		_repliesRootVisible.value()
	) | rpl::on_next([=](int count, bool allowed, bool rootVisible) {
		_pinnedPill->setCount(count);

		// As upstream's bar, not shown in a topic while its first message,
		// pinned, is in view - unless something else is pinned too.
		_pinnedPill->toggle(
			allowed && (fullHistory || rootVisible || count > 1),
			anim::type::normal);
	}, _pinnedPill->lifetime());

	const auto jump = [=](FullMsgId id) {
		const auto item = _history->owner().message(id);
		if (!item) {
			return;
		}
		if (_showAtPosition) {
			_showAtPosition(item->position());
		}
		if (const auto group = _history->owner().groups().find(item)) {
			_pinnedClickedId = group->items.front()->fullId();
		} else {
			_pinnedClickedId = id;
		}
		_minPinnedId = std::nullopt;
		updatePinnedViewer();
	};
	_pinnedPill->setClickedCallback([=] {
		const auto id = _pinnedTracker->currentMessageId().message;
		if (!id) {
			return;
		} else if (_history->owner().message(id)) {
			jump(id);
			return;
		}
		// The bar loaded the message to show its text, so a click on it
		// always found the message. The pill shows none, so it loads here.
		_history->session().api().requestMessageData(
			_history->owner().peer(id.peer),
			id.msg,
			crl::guard(_pinnedPill.get(), [=] { jump(id); }));
	});
}

void TopControls::checkPinnedBarState() {
	Expects(_pinnedTracker != nullptr);

	const auto fullHistory = !_repliesRootId && !_topic && !_sublist;
	const auto migrated = migratedPeer();
	const auto topicRootId = fullHistory ? MsgId(0) : _repliesRootId;
	const auto monoforumPeerId = fullHistory ? PeerId(0) : _monoforumPeerId;

	// LoogriGram: anyone may hide the pill, as in HistoryWidget. Upstream
	// honoured hiding only for those who cannot pin.
	const auto hiddenId = _history->peer->session().settings(
		).hiddenPinnedMessageId(
			_history->peer->id,
			topicRootId,
			monoforumPeerId);
	const auto currentPinnedId = Data::ResolveTopPinnedId(
		_history->peer,
		topicRootId,
		monoforumPeerId,
		migrated);
	const auto universalPinnedId = !currentPinnedId
		? MsgId(0)
		: (migrated && !peerIsChannel(currentPinnedId.peer))
		? (currentPinnedId.msg - ServerMaxMsgId)
		: currentPinnedId.msg;
	_pinnedPillAllowed = universalPinnedId
		&& (universalPinnedId != hiddenId);
}

// LoogriGram: the pill's menu hides it at once, for anyone, until a newer
// message is pinned; as HistoryWidget::hidePinnedMessage.
void TopControls::hidePinnedMessage() {
	const auto fullHistory = !_repliesRootId && !_topic && !_sublist;
	if (Window::HidePinnedMessages(
			_history->peer,
			fullHistory ? MsgId(0) : _repliesRootId,
			fullHistory ? PeerId(0) : _monoforumPeerId)
		&& _pinnedTracker) {
		checkPinnedBarState();
	}
}

void TopControls::resetPinnedState() {
	_pinnedLifetime.destroy();
	_pinnedTracker = nullptr;
	_pinnedPill = nullptr;
	_pinnedPillAllowed = false;
	_pinnedClickedId = FullMsgId();
	_minPinnedId = std::nullopt;
}

void TopControls::rebuildModeSensitiveBars() {
	const auto inForum = showInForum();
	const auto fullChat = !_repliesRootId && !_sublist;
	if (!_modeSensitiveBarsInited
		|| (_modeSensitiveShowInForum != inForum)
		|| (_modeSensitiveFullChat != fullChat)) {
		_modeSensitiveBarsInited = true;
		_modeSensitiveShowInForum = inForum;
		_modeSensitiveFullChat = fullChat;
		_groupCallBar = nullptr;
		_requestsBar = nullptr;
		_contactStatus = nullptr;
		_groupCallBarHeight = 0;
		_requestsBarHeight = 0;
		_contactStatusHeight = 0;
		if (fullChat) {
			setupGroupCallBar();
			setupRequestsBar();
		}
	}
	if (fullChat) {
		setupPeerBars();
	}
	if (_topic || !_repliesRootId) {
		if (_repliesRootView) {
			_repliesRootView = nullptr;
			_repliesRootViewHeight = 0;
			_repliesRootViewInited = false;
			_repliesRootViewInitScheduled = false;
			_shownRepliesRootItem = nullptr;
		}
	} else if (!_repliesRootView) {
		setupRootView();
	}
	if (_topic) {
		if (!_topicReopenBar) {
			setupTopicReopenBar();
		}
	} else if (_topicReopenBar) {
		_topicReopenBar = nullptr;
		_topicReopenBarHeight = 0;
	}
	updateZOrder();
}

void TopControls::updateLayout() {
	auto top = 0;
	_topBars->move(0, 0);
	if (_repliesRootView) {
		_repliesRootView->move(0, top);
		_repliesRootView->resizeToWidth(_width);
		top += _repliesRootViewHeight;
	}
	if (_groupCallBar) {
		_groupCallBar->move(0, top);
		_groupCallBar->resizeToWidth(_width);
		top += _groupCallBarHeight;
	}
	if (_requestsBar) {
		_requestsBar->move(0, top);
		_requestsBar->resizeToWidth(_width);
		top += _requestsBarHeight;
	}
	if (_topicReopenBar) {
		_topicReopenBar->bar().move(0, top);
		top += _topicReopenBarHeight;
	}
	if (_translateBar) {
		_translateBar->move(0, top);
		_translateBar->resizeToWidth(_width);
		top += _translateBarHeight;
	}
	if (_contactStatus) {
		_contactStatus->bar().move(0, top);
		top += _contactStatusHeight;
	}
	_topBars->resize(_width, top + st::lineWidth);
	_wrap->resize(_width, top + st::lineWidth);
	_height = top;
}

void TopControls::updateZOrder() {
	_topBars->raise();
	if (_contactStatus) {
		_contactStatus->bar().raise();
	}
	if (_translateBar) {
		_translateBar->raise();
	}
	if (_topicReopenBar) {
		_topicReopenBar->bar().raise();
	}
	if (_requestsBar) {
		_requestsBar->raise();
	}
	if (_groupCallBar) {
		_groupCallBar->raise();
	}
	if (_repliesRootView) {
		_repliesRootView->raise();
	}
}

void TopControls::applyHeightChange(int was, int now, bool preserveTop) {
	if (was == now) {
		return;
	} else if (preserveTop) {
		applyHeightChangeWithTopMoved(was, now);
	} else {
		applyHeightChangeWithRelayout(was, now);
	}
}

void TopControls::applyHeightChangeWithTopMoved(int was, int now) {
	const auto delta = now - was;
	if (!delta) {
		return;
	} else if (_moveWithTopDelta) {
		_moveWithTopDelta(delta);
	} else if (_relayout) {
		_relayout();
	}
}

void TopControls::applyHeightChangeWithRelayout(int was, int now) {
	if (was == now) {
		return;
	} else if (_relayout) {
		_relayout();
	} else if (_moveWithTopDelta) {
		_moveWithTopDelta(now - was);
	}
}

} // namespace HistoryView
