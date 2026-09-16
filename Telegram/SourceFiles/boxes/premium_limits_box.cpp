/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/premium_limits_box.h"

#include "ui/boxes/confirm_box.h"
#include "ui/controls/peer_list_dummy.h"
#include "ui/effects/premium_bubble.h"
#include "ui/effects/premium_graphics.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peers/prepare_short_info_box.h" // PrepareShortInfoBox
#include "window/window_session_controller.h"
#include "data/data_chat_filters.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_forum.h"
#include "data/data_saved_messages.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_premium_limits.h"
#include "lang/lang_keys.h"
#include "base/unixtime.h"
#include "apiwrap.h"
#include "styles/style_premium.h"
#include "styles/style_premium_limits.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace {

struct InfographicDescriptor {
	float64 current = 0;
	const style::icon *icon;
	std::optional<tr::phrase<lngtag_count>> phrase;
};

void AddSubtitle(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text) {
	const auto &subtitlePadding = st::settingsButton.padding;
	Ui::AddSubsectionTitle(
		container,
		std::move(text),
		{ 0, subtitlePadding.top(), 0, -subtitlePadding.bottom() });
}

class InactiveController final : public PeerListController {
public:
	explicit InactiveController(not_null<Main::Session*> session);
	~InactiveController();

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	[[nodiscard]] rpl::producer<int> countValue() const;

private:
	void appendRow(not_null<PeerData*> peer, TimeId date);
	[[nodiscard]] std::unique_ptr<PeerListRow> createRow(
		not_null<PeerData*> peer,
		TimeId date) const;

	const not_null<Main::Session*> _session;
	rpl::variable<int> _count;
	mtpRequestId _requestId = 0;

};

class PublicsController final : public PeerListController {
public:
	PublicsController(
		not_null<Window::SessionNavigation*> navigation,
		Fn<void()> closeBox);
	~PublicsController();

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowRightActionClicked(not_null<PeerListRow*> row) override;

	[[nodiscard]] rpl::producer<int> countValue() const;

private:
	void appendRow(not_null<PeerData*> peer);
	[[nodiscard]] std::unique_ptr<PeerListRow> createRow(
		not_null<PeerData*> peer) const;

	const not_null<Window::SessionNavigation*> _navigation;
	rpl::variable<int> _count;
	Fn<void()> _closeBox;
	mtpRequestId _requestId = 0;

};

class InactiveDelegate final : public PeerListContentDelegate {
public:
	void peerListSetTitle(rpl::producer<QString> title) override;
	void peerListSetAdditionalTitle(rpl::producer<QString> title) override;
	bool peerListIsRowChecked(not_null<PeerListRow*> row) override;
	int peerListSelectedRowsCount() override;
	void peerListScrollToTop() override;
	void peerListAddSelectedPeerInBunch(
		not_null<PeerData*> peer) override;
	void peerListAddSelectedRowInBunch(
		not_null<PeerListRow*> row) override;
	void peerListFinishSelectedRowsBunch() override;
	void peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) override;
	std::shared_ptr<Main::SessionShow> peerListUiShow() override;
	void peerListSetRowChecked(
		not_null<PeerListRow*> row,
		bool checked) override;

	[[nodiscard]] rpl::producer<int> selectedCountChanges() const;
	[[nodiscard]] const base::flat_set<PeerListRowId> &selected() const;

private:
	base::flat_set<PeerListRowId> _selectedIds;
	rpl::event_stream<int> _selectedCountChanges;

};

void InactiveDelegate::peerListSetTitle(rpl::producer<QString> title) {
}

void InactiveDelegate::peerListSetAdditionalTitle(
	rpl::producer<QString> title) {
}

bool InactiveDelegate::peerListIsRowChecked(not_null<PeerListRow*> row) {
	return _selectedIds.contains(row->id());
}

int InactiveDelegate::peerListSelectedRowsCount() {
	return int(_selectedIds.size());
}

void InactiveDelegate::peerListScrollToTop() {
}

void InactiveDelegate::peerListAddSelectedPeerInBunch(
		not_null<PeerData*> peer) {
	_selectedIds.emplace(PeerListRowId(peer->id.value));
	_selectedCountChanges.fire(int(_selectedIds.size()));
}

void InactiveDelegate::peerListAddSelectedRowInBunch(
		not_null<PeerListRow*> row) {
	_selectedIds.emplace(row->id());
	_selectedCountChanges.fire(int(_selectedIds.size()));
}

void InactiveDelegate::peerListSetRowChecked(
		not_null<PeerListRow*> row,
		bool checked) {
	if (checked) {
		_selectedIds.emplace(row->id());
	} else {
		_selectedIds.remove(row->id());
	}
	_selectedCountChanges.fire(int(_selectedIds.size()));
	PeerListContentDelegate::peerListSetRowChecked(row, checked);
}

void InactiveDelegate::peerListFinishSelectedRowsBunch() {
}

void InactiveDelegate::peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) {
	description.destroy();
}

std::shared_ptr<Main::SessionShow> InactiveDelegate::peerListUiShow() {
	Unexpected("...InactiveDelegate::peerListUiShow");
}

rpl::producer<int> InactiveDelegate::selectedCountChanges() const {
	return _selectedCountChanges.events();
}

const base::flat_set<PeerListRowId> &InactiveDelegate::selected() const {
	return _selectedIds;
}

InactiveController::InactiveController(not_null<Main::Session*> session)
: _session(session) {
}

InactiveController::~InactiveController() {
	if (_requestId) {
		_session->api().request(_requestId).cancel();
	}
}

Main::Session &InactiveController::session() const {
	return *_session;
}

void InactiveController::prepare() {
	_requestId = _session->api().request(MTPchannels_GetInactiveChannels(
	)).done([=](const MTPmessages_InactiveChats &result) {
		_requestId = 0;
		const auto &data = result.data();
		_session->data().processUsers(data.vusers());
		const auto &list = data.vchats().v;
		const auto &dates = data.vdates().v;
		for (auto i = 0, count = int(list.size()); i != count; ++i) {
			const auto peer = _session->data().processChat(list[i]);
			const auto date = (i < dates.size()) ? dates[i].v : TimeId();
			appendRow(peer, date);
		}
		delegate()->peerListRefreshRows();
		_count = delegate()->peerListFullRowsCount();
	}).send();
}

void InactiveController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

rpl::producer<int> InactiveController::countValue() const {
	return _count.value();
}

void InactiveController::appendRow(
		not_null<PeerData*> participant,
		TimeId date) {
	if (!delegate()->peerListFindRow(participant->id.value)) {
		delegate()->peerListAppendRow(createRow(participant, date));
	}
}

std::unique_ptr<PeerListRow> InactiveController::createRow(
		not_null<PeerData*> peer,
		TimeId date) const {
	auto result = std::make_unique<PeerListRow>(peer);
	const auto active = base::unixtime::parse(date).date();
	const auto now = QDate::currentDate();
	const auto time = [&] {
		const auto days = active.daysTo(now);
		if (now < active) {
			return QString();
		} else if (active == now) {
			const auto unixtime = base::unixtime::now();
			const auto delta = int64(unixtime) - int64(date);
			if (delta <= 0) {
				return QString();
			} else if (delta >= 3600) {
				return tr::lng_hours(tr::now, lt_count, delta / 3600);
			} else if (delta >= 60) {
				return tr::lng_minutes(tr::now, lt_count, delta / 60);
			} else {
				return tr::lng_seconds(tr::now, lt_count, delta);
			}
		} else if (days >= 365) {
			return tr::lng_years(tr::now, lt_count, days / 365);
		} else if (days >= 31) {
			return tr::lng_months(tr::now, lt_count, days / 31);
		} else if (days >= 7) {
			return tr::lng_weeks(tr::now, lt_count, days / 7);
		} else {
			return tr::lng_days(tr::now, lt_count, days);
		}
	}();
	result->setCustomStatus(tr::lng_channels_leave_status(
		tr::now,
		lt_type,
		(peer->isBroadcast()
			? tr::lng_channel_status(tr::now)
			: tr::lng_group_status(tr::now)),
		lt_time,
		time));
	return result;
}

PublicsController::PublicsController(
	not_null<Window::SessionNavigation*> navigation,
	Fn<void()> closeBox)
: _navigation(navigation)
, _closeBox(std::move(closeBox)) {
}

PublicsController::~PublicsController() {
	if (_requestId) {
		_navigation->session().api().request(_requestId).cancel();
	}
}

Main::Session &PublicsController::session() const {
	return _navigation->session();
}

rpl::producer<int> PublicsController::countValue() const {
	return _count.value();
}

void PublicsController::prepare() {
	_requestId = _navigation->session().api().request(
		MTPchannels_GetAdminedPublicChannels(MTP_flags(0))
	).done([=](const MTPmessages_Chats &result) {
		_requestId = 0;

		const auto &chats = result.match([](const auto &data) {
			return data.vchats().v;
		});
		auto &owner = _navigation->session().data();
		for (const auto &chat : chats) {
			if (const auto peer = owner.processChat(chat)) {
				if (!peer->isChannel() || peer->username().isEmpty()) {
					continue;
				}
				appendRow(peer);
			}
			delegate()->peerListRefreshRows();
		}
		_count = delegate()->peerListFullRowsCount();
	}).send();
}

void PublicsController::rowClicked(not_null<PeerListRow*> row) {
	_navigation->parentController()->show(
		PrepareShortInfoBox(row->peer(), _navigation));
}

void PublicsController::rowRightActionClicked(not_null<PeerListRow*> row) {
	const auto peer = row->peer();
	const auto textMethod = peer->isMegagroup()
		? tr::lng_channels_too_much_public_revoke_confirm_group
		: tr::lng_channels_too_much_public_revoke_confirm_channel;
	const auto text = textMethod(
		tr::now,
		lt_link,
		peer->session().createInternalLink(peer->username()),
		lt_group,
		peer->name());
	const auto confirmText = tr::lng_channels_too_much_public_revoke(
		tr::now);
	const auto closeBox = _closeBox;
	const auto once = std::make_shared<bool>(false);
	auto callback = crl::guard(_navigation, [=](Fn<void()> close) {
		if (*once) {
			return;
		}
		*once = true;
		peer->session().api().request(MTPchannels_UpdateUsername(
			peer->asChannel()->inputChannel(),
			MTP_string()
		)).done([=] {
			peer->session().api().request(MTPchannels_DeactivateAllUsernames(
				peer->asChannel()->inputChannel()
			)).done([=] {
				closeBox();
				close();
			}).send();
		}).send();
	});
	_navigation->parentController()->show(
		Ui::MakeConfirmBox({
			.text = text,
			.confirmed = std::move(callback),
			.confirmText = confirmText,
		}));
}

void PublicsController::appendRow(not_null<PeerData*> participant) {
	if (!delegate()->peerListFindRow(participant->id.value)) {
		delegate()->peerListAppendRow(createRow(participant));
	}
}

std::unique_ptr<PeerListRow> PublicsController::createRow(
		not_null<PeerData*> peer) const {
	auto result = std::make_unique<PeerListRowWithLink>(peer);
	result->setActionLink(tr::lng_channels_too_much_public_revoke(tr::now));
	result->setCustomStatus(
		_navigation->session().createInternalLink(peer->username()));
	return result;
}

// LoogriGram: these boxes compared the free cap with the premium one - a
// premium bubble, a second bar naming the larger number, a referral tag for
// the subscription page. The account is never premium, so each is now the
// server's cap, the count against it and an OK button.
void SimpleLimitBox(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> title,
		rpl::producer<TextWithEntities> text,
		const InfographicDescriptor &descriptor,
		bool fixed = false) {
	box->setWidth(st::boxWideWidth);

	const auto top = fixed
		? box->setPinnedToTopContent(object_ptr<Ui::VerticalLayout>(box))
		: box->verticalLayout();

	Ui::AddSkip(top, st::premiumInfographicPadding.top());
	Ui::Premium::AddBubbleRow(
		top,
		st::defaultPremiumBubble,
		BoxShowFinishes(box),
		0,
		descriptor.current,
		2 * descriptor.current,
		Ui::Premium::BubbleType::NoPremium,
		descriptor.phrase,
		descriptor.icon);
	Ui::AddSkip(top, st::premiumLineTextSkip);

	box->setTitle(std::move(title));

	auto padding = st::boxPadding;
	padding.setTop(padding.bottom());
	top->add(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(text),
			st::aboutRevokePublicLabel),
		padding);

	box->addButton(tr::lng_box_ok(), [=] {
		box->closeBox();
	});

	if (fixed) {
		Ui::AddSkip(top, st::settingsButton.padding.bottom());
		Ui::AddDivider(top);
	}
}

[[nodiscard]] int PinsCount(not_null<Dialogs::MainList*> list) {
	return list->pinned()->order().size();
}

void SimplePinsLimitBox(
		not_null<Ui::GenericBox*> box,
		float64 limit,
		float64 currentCount) {
	auto text = tr::lng_filter_pin_limit1(
		lt_count,
		rpl::single(limit),
		tr::rich);
	SimpleLimitBox(
		box,
		tr::lng_filter_pin_limit_title(),
		std::move(text),
		{ std::max(currentCount, limit), &st::premiumIconPins });
}

} // namespace

void ChannelsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	const auto limit = float64(
		Data::PremiumLimits(session).channelsCurrent());

	auto text = rpl::combine(
		tr::lng_channels_limit1(
			lt_count,
			rpl::single(limit),
			tr::rich),
		tr::lng_channels_limit2_final(tr::rich)
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		return a.append(QChar(' ')).append(std::move(b));
	});

	SimpleLimitBox(
		box,
		tr::lng_channels_limit_title(),
		std::move(text),
		{ limit, &st::premiumIconGroups },
		true);

	AddSubtitle(box->verticalLayout(), tr::lng_channels_leave_title());

	const auto delegate = box->lifetime().make_state<InactiveDelegate>();
	const auto controller = box->lifetime().make_state<InactiveController>(
		session);

	const auto content = box->addRow(
		object_ptr<PeerListContent>(box, controller),
		style::margins());
	delegate->setContent(content);
	controller->setDelegate(delegate);

	const auto count = 100;
	const auto placeholder = box->addRow(
		object_ptr<PeerListDummy>(box, count, st::defaultPeerList),
		style::margins());

	using namespace rpl::mappers;
	controller->countValue(
	) | rpl::filter(_1 > 0) | rpl::on_next([=] {
		delete placeholder;
	}, placeholder->lifetime());

	delegate->selectedCountChanges(
	) | rpl::on_next([=](int count) {
		const auto leave = [=](const base::flat_set<PeerListRowId> &ids) {
			for (const auto rowId : ids) {
				const auto id = peerToChannel(PeerId(rowId));
				if (const auto channel = session->data().channelLoaded(id)) {
					session->api().leaveChannel(channel);
				}
			}
			box->showToast(tr::lng_channels_leave_done(tr::now));
			box->closeBox();
		};
		box->clearButtons();
		if (count) {
			box->addButton(
				tr::lng_channels_leave(lt_count, rpl::single(count * 1.)),
				[=] { leave(delegate->selected()); });
		} else {
			box->addButton(tr::lng_box_ok(), [=] {
				box->closeBox();
			});
		}
	}, box->lifetime());
}

void PublicLinksLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionNavigation*> navigation,
		Fn<void()> retry) {
	const auto session = &navigation->session();
	const auto limit = float64(
		Data::PremiumLimits(session).channelsPublicCurrent());

	auto text = rpl::combine(
		tr::lng_links_limit1(
			lt_count,
			rpl::single(limit),
			tr::rich),
		tr::lng_links_limit2_final(tr::rich)
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		return a.append(QChar(' ')).append(std::move(b));
	});

	SimpleLimitBox(
		box,
		tr::lng_links_limit_title(),
		std::move(text),
		{ limit, &st::premiumIconLinks },
		true);

	AddSubtitle(box->verticalLayout(), tr::lng_links_revoke_title());

	const auto delegate = box->lifetime().make_state<InactiveDelegate>();
	const auto controller = box->lifetime().make_state<PublicsController>(
		navigation,
		crl::guard(box, [=] { box->closeBox(); retry(); }));

	const auto content = box->addRow(
		object_ptr<PeerListContent>(box, controller),
		style::margins());
	delegate->setContent(content);
	controller->setDelegate(delegate);

	const auto count = limit;
	const auto placeholder = box->addRow(
		object_ptr<PeerListDummy>(box, count, st::defaultPeerList),
		style::margins());

	using namespace rpl::mappers;
	controller->countValue(
	) | rpl::filter(_1 > 0) | rpl::on_next([=] {
		delete placeholder;
	}, placeholder->lifetime());
}

void FilterChatsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		int currentCount,
		bool include) {
	const auto limit = float64(
		Data::PremiumLimits(session).dialogFiltersChatsCurrent());

	auto text = (include
		? tr::lng_filter_chats_limit1
		: tr::lng_filter_chats_exlude_limit1)(
			lt_count,
			rpl::single(limit),
			tr::rich);

	SimpleLimitBox(
		box,
		tr::lng_filter_chats_limit_title(),
		std::move(text),
		{ std::max(float64(currentCount), limit), &st::premiumIconChats });
}

void FilterLinksLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	const auto limit = float64(
		Data::PremiumLimits(session).dialogFiltersLinksCurrent());

	auto text = tr::lng_filter_links_limit1(
		lt_count,
		rpl::single(limit),
		tr::rich);

	SimpleLimitBox(
		box,
		tr::lng_filter_links_limit_title(),
		std::move(text),
		{ limit, &st::premiumIconChats });
}

void FiltersLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		std::optional<int> filtersCountOverride) {
	const auto limit = float64(
		Data::PremiumLimits(session).dialogFiltersCurrent());
	const auto cloud = int(ranges::count_if(
		session->data().chatsFilters().list(),
		[](const Data::ChatFilter &f) { return f.id() != FilterId(); }));
	const auto current = float64(filtersCountOverride.value_or(cloud));

	auto text = tr::lng_filters_limit1(
		lt_count,
		rpl::single(limit),
		tr::rich);
	SimpleLimitBox(
		box,
		tr::lng_filters_limit_title(),
		std::move(text),
		{ current, &st::premiumIconFolders });
}

void ShareableFiltersLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	const auto limit = float64(
		Data::PremiumLimits(session).dialogShareableFiltersCurrent());
	const auto current = float64(ranges::count_if(
		session->data().chatsFilters().list(),
		[](const Data::ChatFilter &f) { return f.chatlist(); }));

	auto text = tr::lng_filter_shared_limit1(
		lt_count,
		rpl::single(limit),
		tr::rich);
	SimpleLimitBox(
		box,
		tr::lng_filter_shared_limit_title(),
		std::move(text),
		{ current, &st::premiumIconFolders });
}

void FilterPinsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		FilterId filterId) {
	SimplePinsLimitBox(
		box,
		Data::PremiumLimits(session).dialogFiltersChatsCurrent(),
		PinsCount(session->data().chatsFilters().chatsList(filterId)));
}

void FolderPinsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	SimplePinsLimitBox(
		box,
		Data::PremiumLimits(session).dialogsFolderPinnedCurrent(),
		PinsCount(session->data().folder(Data::Folder::kId)->chatsList()));
}

void PinsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	SimplePinsLimitBox(
		box,
		Data::PremiumLimits(session).dialogsPinnedCurrent(),
		PinsCount(session->data().chatsList()));
}

void SublistsPinsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	SimplePinsLimitBox(
		box,
		Data::PremiumLimits(session).savedSublistsPinnedCurrent(),
		PinsCount(session->data().savedMessages().chatsList()));
}

void ForumPinsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Data::Forum*> forum) {
	const auto current = forum->owner().pinnedChatsLimit(forum) * 1.;

	auto text = tr::lng_forum_pin_limit(
		lt_count,
		rpl::single(current),
		tr::rich);
	SimpleLimitBox(
		box,
		tr::lng_filter_pin_limit_title(),
		std::move(text),
		{ current, &st::premiumIconPins });
}

void CaptionLimitReachedBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		int remove,
		const style::PremiumLimits *stOverride) {
	Ui::ConfirmBox(box, Ui::ConfirmBoxArgs{
		.text = tr::lng_caption_limit_reached(tr::now, lt_count, remove),
		.labelStyle = stOverride ? &stOverride->boxLabel : nullptr,
		.inform = true,
	});
}

void FileSizeLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	const auto parts = Data::PremiumLimits(session).uploadMaxCurrent();
	const auto gb = float64((parts + 999) / 2000);

	auto text = tr::lng_file_size_limit1(
		lt_size,
		rpl::single(tr::bold(
			tr::lng_file_size_limit(tr::now, lt_count, gb))),
		tr::rich);

	SimpleLimitBox(
		box,
		tr::lng_file_size_limit_title(),
		std::move(text),
		{ gb, &st::premiumIconFiles, tr::lng_file_size_limit });
}

// LoogriGram: this used to offer a way out - free a place by subscribing on
// one of the other logged-in accounts. The list it built that from filters
// for accounts that are not premium but could become premium, which is empty
// by definition here, so the Continue button, the account picker and the
// switch-then-subscribe dance behind them were all unreachable. What is left
// is the count and the cap, which is the part that was ever any use.
void AccountsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	const auto current = int(session->domain().orderedAccounts().size());

	box->setWidth(st::boxWideWidth);

	const auto top = box->verticalLayout();

	Ui::AddSkip(top, st::premiumInfographicPadding.top());
	Ui::Premium::AddBubbleRow(
		top,
		st::defaultPremiumBubble,
		BoxShowFinishes(box),
		0,
		current,
		current * 2,
		Ui::Premium::BubbleType::NoPremium,
		std::nullopt,
		&st::premiumIconAccounts);
	Ui::AddSkip(top, st::premiumLineTextSkip);

	box->setTitle(tr::lng_accounts_limit_title());

	auto padding = st::boxPadding;
	padding.setTop(padding.bottom());
	top->add(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_accounts_limit1(
				lt_count,
				rpl::single<float64>(current),
				tr::rich),
			st::aboutRevokePublicLabel),
		padding);

	box->addButton(tr::lng_box_ok(), [=] {
		box->closeBox();
	});
}

