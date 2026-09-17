/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/statistics/info_statistics_list_controllers.h"

#include "api/api_statistics.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peer_list_widgets.h"
#include "core/ui_integration.h" // TextContext
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/session/session_show.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/effects/outline_segments.h" // Ui::UnreadStoryOutlineGradient.
#include "ui/effects/toggle_arrow.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_color_indices.h"
#include "styles/style_credits.h"
#include "styles/style_dialogs.h" // dialogsStoriesFull.
#include "styles/style_layers.h" // boxRowPadding.
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_statistics.h"
#include "styles/style_chat.h"

namespace Info::Statistics {
namespace {

void AddSubtitle(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title) {
	const auto &subtitlePadding = st::settingsButton.padding;
	Ui::AddSubsectionTitle(
		container,
		std::move(title),
		{ 0, -subtitlePadding.top(), 0, -subtitlePadding.bottom() });
}

[[nodiscard]] object_ptr<Ui::SettingsButton> CreateShowMoreButton(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<QString> title) {
	auto owned = object_ptr<Ui::SettingsButton>(
		parent,
		std::move(title),
		st::statisticsShowMoreButton);
	Ui::AddToggleUpDownArrowToMoreButton(owned.data());
	return owned;
}

[[nodiscard]] QString FormatText(
		int value1, tr::phrase<lngtag_count> phrase1,
		int value2, tr::phrase<lngtag_count> phrase2,
		int value3, tr::phrase<lngtag_count> phrase3) {
	const auto separator = u", "_q;
	auto resultText = QString();
	if (value1 > 0) {
		resultText += phrase1(tr::now, lt_count, value1);
	}
	if (value2 > 0) {
		if (!resultText.isEmpty()) {
			resultText += separator;
		}
		resultText += phrase2(tr::now, lt_count, value2);
	}
	if (value3 > 0) {
		if (!resultText.isEmpty()) {
			resultText += separator;
		}
		resultText += phrase3(tr::now, lt_count, value3);
	}
	return resultText;
}

struct PublicForwardsDescriptor final {
	Data::PublicForwardsSlice firstSlice;
	Fn<void(Data::RecentPostId)> requestShow;
	not_null<PeerData*> peer;
	Data::RecentPostId contextId;
};

struct MembersDescriptor final {
	not_null<Main::Session*> session;
	Fn<void(not_null<PeerData*>)> showPeerInfo;
	Data::StatisticsLists data;
};

class PeerListRowWithFullId : public PeerListRow {
public:
	PeerListRowWithFullId(
		not_null<PeerData*> peer,
		Data::RecentPostId contextId);

	[[nodiscard]] PaintRoundImageCallback generatePaintUserpicCallback(
		bool) override;

	[[nodiscard]] Data::RecentPostId contextId() const;

private:
	const Data::RecentPostId _contextId;

};

PeerListRowWithFullId::PeerListRowWithFullId(
	not_null<PeerData*> peer,
	Data::RecentPostId contextId)
: PeerListRow(peer)
, _contextId(contextId) {
}

PaintRoundImageCallback PeerListRowWithFullId::generatePaintUserpicCallback(
		bool forceRound) {
	if (!_contextId.storyId) {
		return PeerListRow::generatePaintUserpicCallback(forceRound);
	}
	const auto peer = PeerListRow::peer();
	auto userpic = PeerListRow::ensureUserpicView();

	const auto line = st::dialogsStoriesFull.lineTwice;
	const auto penWidth = line / 2.;
	const auto offset = 1.5 * penWidth * 2;
	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable {
		const auto rect = QRect(QPoint(x, y), Size(size));
		peer->paintUserpicLeft(
			p,
			userpic,
			x + offset,
			y + offset,
			outerWidth,
			size - offset * 2);
		auto hq = PainterHighQualityEnabler(p);
		auto gradient = Ui::UnreadStoryOutlineGradient();
		gradient.setStart(rect.topRight());
		gradient.setFinalStop(rect.bottomLeft());

		p.setPen(QPen(gradient, penWidth));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(rect - Margins(penWidth));
	};
}

Data::RecentPostId PeerListRowWithFullId::contextId() const {
	return _contextId;
}

class MembersController final : public PeerListController {
public:
	MembersController(MembersDescriptor d);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	void setLimit(int limit);

private:
	void addRows(int from, int to);

	const not_null<Main::Session*> _session;
	Fn<void(not_null<PeerData*>)> _showPeerInfo;
	Data::StatisticsLists _data;
	int _limit = 0;

};

MembersController::MembersController(MembersDescriptor d)
: _session(std::move(d.session))
, _showPeerInfo(std::move(d.showPeerInfo))
, _data(std::move(d.data)) {
}

Main::Session &MembersController::session() const {
	return *_session;
}

void MembersController::setLimit(int limit) {
	addRows(_limit, limit);
	_limit = limit;
}

void MembersController::addRows(int from, int to) {
	const auto addRow = [&](UserId userId, QString text) {
		const auto user = _session->data().user(userId);
		auto row = std::make_unique<PeerListRow>(user);
		row->setCustomStatus(std::move(text));
		delegate()->peerListAppendRow(std::move(row));
	};
	if (!_data.topSenders.empty()) {
		for (auto i = from; i < to; i++) {
			const auto &member = _data.topSenders[i];
			addRow(
				member.userId,
				FormatText(
					member.sentMessageCount,
					tr::lng_stats_member_messages,
					member.averageCharacterCount,
					tr::lng_stats_member_characters,
					0,
					{}));
		}
	} else if (!_data.topAdministrators.empty()) {
		for (auto i = from; i < to; i++) {
			const auto &admin = _data.topAdministrators[i];
			addRow(
				admin.userId,
				FormatText(
					admin.deletedMessageCount,
					tr::lng_stats_member_deletions,
					admin.bannedUserCount,
					tr::lng_stats_member_bans,
					admin.restrictedUserCount,
					tr::lng_stats_member_restrictions));
		}
	} else if (!_data.topInviters.empty()) {
		for (auto i = from; i < to; i++) {
			const auto &inviter = _data.topInviters[i];
			addRow(
				inviter.userId,
				FormatText(
					inviter.addedMemberCount,
					tr::lng_stats_member_invitations,
					0,
					{},
					0,
					{}));
		}
	}
}

void MembersController::prepare() {
}

void MembersController::loadMoreRows() {
}

void MembersController::rowClicked(not_null<PeerListRow*> row) {
	crl::on_main([=, peer = row->peer()] {
		_showPeerInfo(peer);
	});
}

class PublicForwardsController final : public PeerListController {
public:
	explicit PublicForwardsController(PublicForwardsDescriptor d);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

private:
	void appendRow(not_null<PeerData*> peer, Data::RecentPostId contextId);
	void applySlice(const Data::PublicForwardsSlice &slice);

	const not_null<Main::Session*> _session;
	Fn<void(Data::RecentPostId)> _requestShow;

	Api::PublicForwards _api;
	Data::PublicForwardsSlice _firstSlice;
	Data::PublicForwardsSlice::OffsetToken _apiToken;

	bool _allLoaded = false;

};

PublicForwardsController::PublicForwardsController(PublicForwardsDescriptor d)
: _session(&d.peer->session())
, _requestShow(std::move(d.requestShow))
, _api(d.peer->asChannel(), d.contextId)
, _firstSlice(std::move(d.firstSlice)) {
}

Main::Session &PublicForwardsController::session() const {
	return *_session;
}

void PublicForwardsController::prepare() {
	applySlice(base::take(_firstSlice));
	delegate()->peerListRefreshRows();
}

void PublicForwardsController::loadMoreRows() {
	if (_allLoaded) {
		return;
	}
	_api.request(_apiToken, [=](const Data::PublicForwardsSlice &slice) {
		applySlice(slice);
	});
}

void PublicForwardsController::applySlice(
		const Data::PublicForwardsSlice &slice) {
	_allLoaded = slice.allLoaded;
	_apiToken = slice.token;

	for (const auto &item : slice.list) {
		if (const auto &full = item.messageId) {
			if (const auto peer = session().data().peerLoaded(full.peer)) {
				appendRow(peer, item);
			}
		} else if (const auto &full = item.storyId) {
			if (const auto story = session().data().stories().lookup(full)) {
				appendRow((*story)->peer(), item);
			}
		}
	}
	delegate()->peerListRefreshRows();
}

void PublicForwardsController::rowClicked(not_null<PeerListRow*> row) {
	const auto rowWithId = static_cast<PeerListRowWithFullId*>(row.get());
	crl::on_main([=, id = rowWithId->contextId()] { _requestShow(id); });
}

base::unique_qptr<Ui::PopupMenu> PublicForwardsController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	auto menu = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	const auto peer = row->peer();
	const auto text = (peer->isChat() || peer->isMegagroup())
		? tr::lng_context_view_group(tr::now)
		: peer->isUser()
		? tr::lng_context_view_profile(tr::now)
		: peer->isChannel()
		? tr::lng_context_view_channel(tr::now)
		: QString();
	if (text.isEmpty()) {
		return nullptr;
	}
	menu->addAction(text, crl::guard(parent, [=, peerId = peer->id] {
		_requestShow({ .messageId = { peerId, MsgId() } });
	}), peer->isUser() ? &st::menuIconProfile : &st::menuIconInfo);
	return menu;
}

void PublicForwardsController::appendRow(
		not_null<PeerData*> peer,
		Data::RecentPostId contextId) {
	if (delegate()->peerListFindRow(peer->id.value)) {
		return;
	}

	auto row = std::make_unique<PeerListRowWithFullId>(peer, contextId);

	const auto members = peer->isChannel()
		? peer->asChannel()->membersCount()
		: 0;
	const auto views = [&] {
		if (contextId.messageId) {
			const auto message = peer->owner().message(contextId.messageId);
			return message ? std::max(message->viewsCount(), 0) : 0;
		} else if (const auto &id = contextId.storyId) {
			const auto story = peer->owner().stories().lookup(id);
			return story ? (*story)->views() : 0;
		}
		return 0;
	}();

	const auto membersText = !members
		? QString()
		: peer->isMegagroup()
		? tr::lng_chat_status_members(tr::now, lt_count_decimal, members)
		: tr::lng_chat_status_subscribers(tr::now, lt_count_decimal, members);
	const auto viewsText = views
		? tr::lng_stats_recent_messages_views({}, lt_count_decimal, views)
		: QString();
	const auto resultText = (membersText.isEmpty() && viewsText.isEmpty())
		? tr::lng_stories_no_views(tr::now)
		: (membersText.isEmpty() || viewsText.isEmpty())
		? membersText + viewsText
		: QString("%1, %2").arg(membersText, viewsText);
	row->setCustomStatus(resultText);

	delegate()->peerListAppendRow(std::move(row));
	return;
}

} // namespace

void AddPublicForwards(
		const Data::PublicForwardsSlice &firstSlice,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Data::RecentPostId)> requestShow,
		not_null<PeerData*> peer,
		Data::RecentPostId contextId) {
	if (!peer->isChannel()) {
		return;
	}

	struct State final {
		State(PublicForwardsDescriptor d) : controller(std::move(d)) {
		}
		PeerListContentDelegateSimple delegate;
		PublicForwardsController controller;
	};
	auto d = PublicForwardsDescriptor{
		firstSlice,
		std::move(requestShow),
		peer,
		contextId,
	};
	const auto state = container->lifetime().make_state<State>(std::move(d));

	if (const auto total = firstSlice.total; total > 0) {
		AddSubtitle(
			container,
			tr::lng_stats_overview_message_public_share(
				lt_count_decimal,
				rpl::single<float64>(total)));
	}

	state->delegate.setContent(container->add(
		object_ptr<PeerListContent>(container, &state->controller)));
	state->controller.setDelegate(&state->delegate);
}

void AddMembersList(
		Data::StatisticsLists data,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(not_null<PeerData*>)> showPeerInfo,
		not_null<PeerData*> peer,
		rpl::producer<QString> title) {
	if (!peer->isMegagroup()) {
		return;
	}
	const auto max = !data.topSenders.empty()
		? data.topSenders.size()
		: !data.topAdministrators.empty()
		? data.topAdministrators.size()
		: !data.topInviters.empty()
		? data.topInviters.size()
		: 0;
	if (!max) {
		return;
	}

	constexpr auto kPerPage = 40;
	struct State final {
		State(MembersDescriptor d) : controller(std::move(d)) {
		}
		PeerListContentDelegateSimple delegate;
		MembersController controller;
		int limit = 0;
	};
	auto d = MembersDescriptor{
		&peer->session(),
		std::move(showPeerInfo),
		std::move(data),
	};
	const auto state = container->lifetime().make_state<State>(std::move(d));

	AddSubtitle(container, std::move(title));

	state->delegate.setContent(container->add(
		object_ptr<PeerListContent>(container, &state->controller)));
	state->controller.setDelegate(&state->delegate);

	const auto wrap = AddShowMoreButton(
		container,
		tr::lng_stories_show_more());

	const auto showMore = [=] {
		state->limit = std::min(int(max), state->limit + kPerPage);
		state->controller.setLimit(state->limit);
		if (state->limit == max) {
			wrap->toggle(false, anim::type::instant);
		}
		container->resizeToWidth(container->width());
	};
	wrap->entity()->setClickedCallback(showMore);
	showMore();
}

not_null<Ui::SlideWrap<Ui::SettingsButton>*> AddShowMoreButton(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title) {
	return container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			CreateShowMoreButton(container, std::move(title))),
		{ 0, -st::settingsButton.padding.top(), 0, 0 });
}

} // namespace Info::Statistics
