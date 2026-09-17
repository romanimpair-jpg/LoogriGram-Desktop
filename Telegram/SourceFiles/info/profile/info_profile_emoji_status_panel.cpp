/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_emoji_status_panel.h"

#include "api/api_peer_photo.h"
#include "apiwrap.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_emoji_statuses.h"
#include "lang/lang_keys.h"
#include "menu/menu_send.h" // SendMenu::Type.
#include "ui/effects/emoji_fly_animation.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "styles/style_chat_helpers.h"

namespace Info::Profile {
namespace {

constexpr auto kLimitFirstRow = 8;

} // namespace

EmojiStatusPanel::EmojiStatusPanel() = default;

EmojiStatusPanel::~EmojiStatusPanel() {
	if (hasFocus()) {
		// Panel will try to return focus to the layer widget, the problem is
		// we are destroying the layer widget probably right now and focusing
		// it will lead to a crash, because it destroys its children (how we
		// got here) after it clears focus out of itself. So if you return
		// the focus inside a child destructor, it won't be cleared at all.
		_panel->window()->setFocus();
	}
}

// LoogriGram: this panel also set our own emoji status, from recent,
// default and coloured status lists with a duration picker. A status is
// only accepted from a subscriber, so that half is deleted; what is left
// picks a profile background emoji or a channel's status.

void EmojiStatusPanel::show(Descriptor &&descriptor) {
	const auto controller = descriptor.controller;
	if (!_panel) {
		create(descriptor);

		_panel->shownValue(
		) | rpl::filter([=] {
			return (_panelButton != nullptr);
		}) | rpl::on_next([=](bool shown) {
			if (shown) {
				_panelButton->installEventFilter(_panel.get());
			} else {
				_panelButton->removeEventFilter(_panel.get());
			}
		}, _panel->lifetime());
	}
	const auto button = descriptor.button;
	if (const auto previous = _panelButton.data()) {
		if (previous != button) {
			previous->removeEventFilter(_panel.get());
		}
	}
	_panelButton = button;
	_animationSizeTag = descriptor.animationSizeTag;
	const auto feed = [=, now = descriptor.ensureAddedEmojiId](
			std::vector<EmojiStatusId> list) {
		list.insert(begin(list), EmojiStatusId());
		if (now && !ranges::contains(list, now)) {
			list.push_back(now);
		}
		_panel->selector()->provideRecentEmoji(list);
	};
	if (descriptor.backgroundEmojiMode) {
		controller->session().api().peerPhoto().emojiListValue(
			Api::PeerPhoto::EmojiListType::Background
		) | rpl::on_next([=](std::vector<DocumentId> &&list) {
			auto tmp = std::vector<EmojiStatusId>();
			for (const auto &id : list) {
				tmp.push_back(EmojiStatusId{ .documentId = id });
			}
			feed(std::move(tmp));
		}, _panel->lifetime());
	} else {
		const auto &statuses = controller->session().data().emojiStatuses();
		const auto &other = statuses.list(Data::EmojiStatuses::Type::ChannelDefault);
		auto list = statuses.list(Data::EmojiStatuses::Type::ChannelColored);
		if (list.size() > kLimitFirstRow - 1) {
			list.erase(begin(list) + kLimitFirstRow - 1, end(list));
		}
		list.reserve(list.size() + other.size() + 1);
		for (const auto &id : other) {
			if (!ranges::contains(list, id)) {
				list.push_back(id);
			}
		}
		feed(std::move(list));
	}
	const auto parent = _panel->parentWidget();
	const auto global = button->mapToGlobal(QPoint());
	const auto local = parent->mapFromGlobal(global);
	_panel->moveBottomRight(
		local.y() + (st::normalFont->height / 2),
		local.x() + button->width() * 3);
	_panel->toggleAnimated();
}

void EmojiStatusPanel::hideFast() {
	if (_panel) {
		_panel->hideFast();
	}
}

void EmojiStatusPanel::hideAnimated() {
	if (_panel) {
		_panel->hideAnimated();
	}
}

bool EmojiStatusPanel::shown() const {
	return _panel && !_panel->isHidden();
}

bool EmojiStatusPanel::hasFocus() const {
	return _panel && Ui::InFocusChain(_panel.get());
}

void EmojiStatusPanel::repaint() {
	_panel->selector()->update();
}

bool EmojiStatusPanel::paintBadgeFrame(not_null<Ui::RpWidget*> widget) {
	if (!_animation) {
		return false;
	} else if (_animation->paintBadgeFrame(widget)) {
		return true;
	}
	InvokeQueued(_animation->layer(), [=] { _animation = nullptr; });
	return false;
}

void EmojiStatusPanel::create(const Descriptor &descriptor) {
	using Selector = ChatHelpers::TabbedSelector;
	using Descriptor = ChatHelpers::TabbedSelectorDescriptor;
	using Mode = ChatHelpers::TabbedSelector::Mode;
	const auto controller = descriptor.controller;
	const auto body = controller->window().widget()->bodyWidget();
	auto features = ChatHelpers::ComposeFeatures();
	_panel = base::make_unique_q<ChatHelpers::TabbedPanel>(
		body,
		controller,
		object_ptr<Selector>(
			nullptr,
			Descriptor{
				.show = controller->uiShow(),
				.st = st::backgroundEmojiPan,
				.level = Window::GifPauseReason::Layer,
				.mode = (descriptor.backgroundEmojiMode
					? Mode::BackgroundEmoji
					: Mode::ChannelStatus),
				.customTextColor = descriptor.customTextColor,
				.features = features,
			}));
	_customTextColor = descriptor.customTextColor;
	_backgroundEmojiMode = descriptor.backgroundEmojiMode;
	_panel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	_panel->hide();

	struct Chosen {
		EmojiStatusId id;
		TimeId until = 0;
		Ui::MessageSendingAnimationFrom animation;
	};

	_panel->selector()->contextMenuRequested(
	) | rpl::on_next([=] {
		_panel->selector()->showMenuWithDetails({});
	}, _panel->lifetime());

	auto statusChosen = _panel->selector()->customEmojiChosen(
	) | rpl::map([=](ChatHelpers::FileChosen data) {
		return Chosen{
			.id = { data.document->id },
			.until = data.options.scheduled,
			.animation = data.messageSendingFrom,
		};
	});

	auto emojiChosen = _panel->selector()->emojiChosen(
	) | rpl::map([=](ChatHelpers::EmojiChosen data) {
		return Chosen{ .animation = data.messageSendingFrom };
	});

	rpl::merge(
		std::move(statusChosen),
		std::move(emojiChosen)
	) | rpl::on_next([=](const Chosen &chosen) {
		const auto owner = &controller->session().data();
		startAnimation(owner, body, chosen.id, chosen.animation);
		_someCustomChosen.fire({ chosen.id, chosen.until });
		_panel->hideAnimated();
	}, _panel->lifetime());
}

void EmojiStatusPanel::startAnimation(
		not_null<Data::Session*> owner,
		not_null<Ui::RpWidget*> body,
		EmojiStatusId statusId,
		Ui::MessageSendingAnimationFrom from) {
	if (!_panelButton || !statusId) {
		return;
	}
	const auto documentId = statusId.documentId;
	auto args = Ui::ReactionFlyAnimationArgs{
		.id = { { documentId } },
		.flyIcon = from.frame,
		.flyFrom = body->mapFromGlobal(from.globalStartGeometry),
		.forceFirstFrame = _backgroundEmojiMode,
	};
	const auto color = _customTextColor
		? _customTextColor
		: [] { return st::profileVerifiedCheckBg->c; };
	_animation = std::make_unique<Ui::EmojiFlyAnimation>(
		body,
		&owner->reactions(),
		std::move(args),
		[=] { _animation->repaint(); },
		_customTextColor,
		_animationSizeTag);
}

} // namespace Info::Profile
