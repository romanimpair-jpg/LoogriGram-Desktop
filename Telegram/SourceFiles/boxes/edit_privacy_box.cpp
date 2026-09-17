/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/edit_privacy_box.h"

#include "apiwrap.h"
#include "boxes/filters/edit_filter_chats_list.h"
#include "boxes/peers/edit_peer_invite_link.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer_values.h"
#include "data/data_user.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "settings/settings_privacy_controllers.h"
#include "settings/sections/settings_privacy_security.h"
#include "ui/boxes/peer_qr_box.h"
#include "ui/controls/invite_link_buttons.h"
#include "ui/controls/invite_link_label.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"

namespace {

using Exceptions = Api::UserPrivacy::Exceptions;

// LoogriGram: a "User types" section sat above the chats in this picker,
// offering "Premium users" and "Mini apps" as exceptions alongside people.
// Premium is honoured for nobody, and mini apps were only ever offered for
// who may send gifts, which is deleted. Neither rule is read from the server
// any more, so the section had nothing left to show.
class PrivacyExceptionsBoxController : public ChatsListBoxController {
public:
	PrivacyExceptionsBoxController(
		not_null<Main::Session*> session,
		rpl::producer<QString> title,
		const Exceptions &selected);

	Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;

protected:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;

private:
	const not_null<Main::Session*> _session;
	rpl::producer<QString> _title;
	Exceptions _selected;

};

PrivacyExceptionsBoxController::PrivacyExceptionsBoxController(
	not_null<Main::Session*> session,
	rpl::producer<QString> title,
	const Exceptions &selected)
: ChatsListBoxController(session)
, _session(session)
, _title(std::move(title))
, _selected(selected) {
}

Main::Session &PrivacyExceptionsBoxController::session() const {
	return *_session;
}

void PrivacyExceptionsBoxController::prepareViewHook() {
	delegate()->peerListSetTitle(std::move(_title));
	delegate()->peerListAddSelectedPeers(_selected.peers);
}

void PrivacyExceptionsBoxController::rowClicked(not_null<PeerListRow*> row) {
	const auto peer = row->peer();

	// This call may delete row, if it was a search result row.
	delegate()->peerListSetRowChecked(row, !row->checked());

	if (const auto channel = peer->asChannel()) {
		if (!channel->membersCountKnown()) {
			channel->updateFull();
		}
	}
}

auto PrivacyExceptionsBoxController::createRow(not_null<History*> history)
-> std::unique_ptr<Row> {
	const auto peer = history->peer;
	if (peer->isSelf() || peer->isRepliesChat() || peer->isVerifyCodes()) {
		return nullptr;
	} else if (!peer->isUser()
		&& !peer->isChat()
		&& !peer->isMegagroup()) {
		return nullptr;
	}
	auto result = std::make_unique<Row>(history);
	const auto count = [&] {
		if (const auto chat = history->peer->asChat()) {
			return chat->count;
		} else if (const auto channel = history->peer->asChannel()) {
			return channel->membersCountKnown()
				? channel->membersCount()
				: 0;
		}
		return 0;
	}();
	if (count > 0) {
		result->setCustomStatus(
			tr::lng_chat_status_members(tr::now, lt_count_decimal, count));
	}
	return result;
}

} // namespace

bool EditPrivacyController::hasOption(Option option) const {
	return (option != Option::CloseFriends);
}

QString EditPrivacyController::optionLabel(Option option) const {
	switch (option) {
	case Option::Everyone: return tr::lng_edit_privacy_everyone(tr::now);
	case Option::Contacts: return tr::lng_edit_privacy_contacts(tr::now);
	case Option::CloseFriends:
		return tr::lng_edit_privacy_close_friends(tr::now);
	case Option::Nobody: return tr::lng_edit_privacy_nobody(tr::now);
	}
	Unexpected("Option value in optionsLabelKey.");
}

EditPrivacyBox::EditPrivacyBox(
	QWidget*,
	not_null<Window::SessionController*> window,
	std::unique_ptr<EditPrivacyController> controller,
	const Value &value)
: _window(window)
, _controller(std::move(controller))
, _value(value) {
}

void EditPrivacyBox::prepare() {
	_controller->setView(this);

	setupContent();
}

void EditPrivacyBox::editExceptions(
		Exception exception,
		Fn<void()> done) {
	auto controller = std::make_unique<PrivacyExceptionsBoxController>(
		&_window->session(),
		_controller->exceptionBoxTitle(exception),
		exceptions(exception));
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_settings_save(), crl::guard(this, [=] {
			auto &setTo = exceptions(exception);
			setTo.peers = box->collectSelectedRows();
			const auto type = [&] {
				switch (exception) {
				case Exception::Always: return Exception::Never;
				case Exception::Never: return Exception::Always;
				}
				Unexpected("Invalid exception value.");
			}();
			auto &removeFrom = exceptions(type);
			for (const auto &peer : exceptions(exception).peers) {
				removeFrom.peers.erase(
					ranges::remove(removeFrom.peers, peer),
					end(removeFrom.peers));
			}
			done();
			box->closeBox();
		}));
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};
	_window->show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)));
}

EditPrivacyBox::Exceptions &EditPrivacyBox::exceptions(Exception exception) {
	switch (exception) {
	case Exception::Always: return _value.always;
	case Exception::Never: return _value.never;
	}
	Unexpected("Invalid exception value.");
}

bool EditPrivacyBox::showExceptionLink(Exception exception) const {
	switch (exception) {
	case Exception::Always:
		return (_value.option == Option::Contacts)
			|| (_value.option == Option::CloseFriends)
			|| (_value.option == Option::Nobody);
	case Exception::Never:
		return (_value.option == Option::Everyone)
			|| (_value.option == Option::Contacts)
			|| (_value.option == Option::CloseFriends);
	}
	Unexpected("Invalid exception value.");
}

Ui::Radioenum<EditPrivacyBox::Option> *EditPrivacyBox::AddOption(
		not_null<Ui::VerticalLayout*> container,
		not_null<EditPrivacyController*> controller,
		const std::shared_ptr<Ui::RadioenumGroup<Option>> &group,
		Option option) {
	return container->add(
		object_ptr<Ui::Radioenum<Option>>(
			container,
			group,
			option,
			controller->optionLabel(option),
			st::settingsPrivacyOption),
		(st::settingsSendTypePadding + style::margins(
			-st::lineWidth,
			st::settingsPrivacySkipTop,
			0,
			0)));
}

Ui::FlatLabel *EditPrivacyBox::addLabel(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<TextWithEntities> text,
		int topSkip) {
	if (!text) {
		return nullptr;
	}
	auto label = object_ptr<Ui::FlatLabel>(
		container,
		rpl::duplicate(text),
		st::boxDividerLabel);
	const auto result = label.data();
	container->add(
		object_ptr<Ui::DividerLabel>(
			container,
			std::move(label),
			st::defaultBoxDividerLabelPadding),
		{ 0, topSkip, 0, 0 });
	return result;
}

Ui::FlatLabel *EditPrivacyBox::addLabelOrDivider(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<TextWithEntities> text,
		int topSkip) {
	if (const auto result = addLabel(container, std::move(text), topSkip)) {
		return result;
	}
	container->add(
		object_ptr<Ui::BoxContentDivider>(container),
		{ 0, topSkip, 0, 0 });
	return nullptr;
}

void EditPrivacyBox::setupContent() {
	using namespace Settings;

	setTitle(_controller->title());

	auto wrap = object_ptr<Ui::VerticalLayout>(this);
	const auto content = wrap.data();
	setInnerWidget(object_ptr<Ui::OverrideMargins>(
		this,
		std::move(wrap)));

	const auto group = std::make_shared<Ui::RadioenumGroup<Option>>(
		_value.option);
	const auto toggle = Ui::CreateChild<rpl::event_stream<Option>>(content);
	group->setChangedCallback([=](Option value) {
		_value.option = value;
		toggle->fire_copy(value);
	});
	auto optionValue = toggle->events_starting_with_copy(_value.option);

	const auto addOptionRow = [&](Option option) {
		return (_controller->hasOption(option) || (_value.option == option))
			? AddOption(content, _controller.get(), group, option)
			: nullptr;
	};
	const auto addExceptionLink = [=](Exception exception) {
		const auto update = Ui::CreateChild<rpl::event_stream<>>(content);
		auto label = update->events_starting_with({}) | rpl::map([=] {
			const auto &value = exceptions(exception);
			const auto count = Settings::ExceptionUsersCount(value.peers);
			return count
				? tr::lng_edit_privacy_exceptions_count(
					tr::now,
					lt_count,
					count)
				: tr::lng_edit_privacy_exceptions_add(tr::now);
		});
		_controller->handleExceptionsChange(
			exception,
			update->events_starting_with({}) | rpl::map([=] {
				return Settings::ExceptionUsersCount(
					exceptions(exception).peers);
			}));
		auto text = _controller->exceptionButtonTextKey(exception);
		const auto button = content->add(
			object_ptr<Ui::SlideWrap<Button>>(
				content,
				object_ptr<Button>(
					content,
					rpl::duplicate(text),
					st::settingsButtonNoIcon)));
		CreateRightLabel(
			button->entity(),
			std::move(label),
			st::settingsButtonNoIcon,
			std::move(text));
		button->toggleOn(rpl::duplicate(
			optionValue
		) | rpl::map([=] {
			return showExceptionLink(exception);
		}))->entity()->addClickHandler([=] {
			editExceptions(exception, [=] { update->fire({}); });
		});
		return button;
	};

	auto above = _controller->setupAboveWidget(
		_window,
		content,
		rpl::duplicate(optionValue),
		getDelegate()->outerContainer());
	if (above) {
		content->add(std::move(above));
	}

	Ui::AddSubsectionTitle(
		content,
		_controller->optionsTitleKey(),
		{ 0, st::settingsPrivacySkipTop, 0, 0 });

	const auto options = {
		Option::Everyone,
		Option::Contacts,
		Option::CloseFriends,
		Option::Nobody,
	};
	for (const auto &option : options) {
		addOptionRow(option);
	}

	const auto warning = addLabelOrDivider(
		content,
		_controller->warning(),
		st::defaultVerticalListSkip + st::settingsPrivacySkipTop);
	if (warning) {
		_controller->prepareWarningLabel(warning);
	}

	auto middle = _controller->setupMiddleWidget(
		_window,
		content,
		rpl::duplicate(optionValue));
	if (middle) {
		content->add(std::move(middle));
	}

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(
		content,
		tr::lng_edit_privacy_exceptions(),
		{ 0, st::settingsPrivacySkipTop, 0, 0 });
	const auto always = addExceptionLink(Exception::Always);
	const auto never = addExceptionLink(Exception::Never);
	_always = always->entity();
	_never = never->entity();
	addLabel(
		content,
		_controller->exceptionsDescription() | rpl::map(tr::marked),
		st::defaultVerticalListSkip);

	auto below = _controller->setupBelowWidget(
		_window,
		content,
		rpl::duplicate(optionValue));
	if (below) {
		content->add(std::move(below));
	}

	addButton(tr::lng_settings_save(), [=] {
		const auto someAreDisallowed = (_value.option != Option::Everyone)
			|| !_value.never.peers.empty();
		_controller->confirmSave(someAreDisallowed, crl::guard(this, [=] {
			_value.ignoreAlways = !showExceptionLink(Exception::Always);
			_value.ignoreNever = !showExceptionLink(Exception::Never);

			_controller->saveAdditional();
			_window->session().api().userPrivacy().save(
				_controller->key(),
				_value);
			closeBox();
		}));
	});
	addButton(tr::lng_cancel(), [this] { closeBox(); });

	const auto linkHeight = st::settingsButtonNoIcon.padding.top()
		+ st::settingsButtonNoIcon.height
		+ st::settingsButtonNoIcon.padding.bottom();

	widthValue(
	) | rpl::on_next([=](int width) {
		content->resizeToWidth(width);
	}, content->lifetime());

	content->heightValue(
	) | rpl::map([=](int height) {
		return height - always->height() - never->height() + 2 * linkHeight;
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](int height) {
		setDimensions(st::boxWideWidth, height);
	}, content->lifetime());
}

void EditPrivacyBox::showFinished() {
	_window->checkHighlightControl(u"privacy/always"_q, _always.data());
	_window->checkHighlightControl(u"privacy/never"_q, _never.data());
	_controller->checkHighlightControls(_window);
}

// LoogriGram: EditMessagesPrivacyBox chose between "Everybody" and "My
// Contacts and Premium Users" for who may start a chat with us. The second
// is premium-only to pick and lets strangers through only if they pay, so
// the box and its settings row are deleted; with it gone there is no choice
// left to offer.

// LoogriGram: this box set a price in stars on writing to a channel's direct
// messages, with a toggle above the slider that turned the whole thing on.
// The toggle is not a money control - direct messages can be open and free -
// so it stays and the price goes, along with the link block's dependence on
// a slider that is no longer between them.
void EditDirectMessagesBox(
		not_null<Ui::GenericBox*> box,
		not_null<ChannelData*> channel,
		bool savedValue,
		Fn<void(bool)> callback) {
	box->setTitle(tr::lng_manage_monoforum());
	box->setWidth(st::boxWideWidth);

	const auto container = box->verticalLayout();

	Settings::AddDividerTextWithLottie(container, {
		.lottie = u"direct_messages"_q,
		.lottieSize = st::settingsFilterIconSize,
		.lottieMargins = st::settingsFilterIconPadding,
		.showFinished = box->showFinishes(),
		.about = tr::lng_manage_monoforum_about(
			tr::rich
		),
		.aboutMargins = st::settingsFilterDividerLabelPadding,
	});

	Ui::AddSkip(container);

	const auto toggle = container->add(object_ptr<Ui::SettingsButton>(
		box,
		tr::lng_manage_monoforum_allow(),
		st::settingsButtonNoIcon));
	toggle->toggleOn(rpl::single(savedValue));

	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);

	const auto wrap = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)),
		style::margins());
	wrap->toggle(savedValue, anim::type::instant);
	wrap->toggleOn(toggle->toggledChanges());

	const auto inner = wrap->entity();
	if (const auto username = channel->username(); !username.isEmpty()) {
		Ui::AddSkip(inner);
		Ui::AddSubsectionTitle(
			inner,
			tr::lng_manage_monoforum_link_subtitle());

		constexpr auto kDirectParam = "?direct"_cs;
		const auto link = channel->session().createInternalLinkFull(username)
			+ kDirectParam.utf8();
		const auto copyLink = [=] {
			TextUtilities::SetClipboardText(TextForMimeData::Simple(link));
			box->uiShow()->showToast({
				.text = { tr::lng_group_invite_copied(tr::now) },
				.iconLottie = u"toast/voip_invite"_q,
				.iconLottieSize = st::toastLottieIconSize,
			});
		};
		const auto shareLink = [=] {
			box->uiShow()->showBox(ShareInviteLinkBox(channel, link));
		};
		const auto createMenu = [=] {
			auto result = base::make_unique_q<Ui::PopupMenu>(
				inner,
				st::popupMenuWithIcons);
			result->addAction(
				tr::lng_group_invite_context_qr(tr::now),
				[=] {
					box->uiShow()->showBox(Box([=](
							not_null<Ui::GenericBox*> qrBox) {
						Ui::FillPeerQrBox(qrBox, channel, link, nullptr);
					}));
				},
				&st::menuIconQrCode);
			return result;
		};

		auto linkText = Ui::Text::StripUrlProtocol(link);
		const auto label = inner->lifetime().make_state<Ui::InviteLinkLabel>(
			inner,
			rpl::single(std::move(linkText)),
			createMenu);
		inner->add(
			label->take(),
			st::inviteLinkFieldPadding);

		label->clicks() | rpl::on_next(copyLink, label->lifetime());

		Ui::AddSkip(inner);

		AddCopyShareLinkButtons(inner, copyLink, shareLink);
		Ui::AddSkip(inner);
		Ui::AddSkip(inner);

		Ui::AddDivider(inner);
	}

	box->addButton(tr::lng_settings_save(), [=] {
		const auto weak = base::make_weak(box);
		callback(toggle->toggled());
		if (const auto strong = weak.get()) {
			strong->closeBox();
		}
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}
