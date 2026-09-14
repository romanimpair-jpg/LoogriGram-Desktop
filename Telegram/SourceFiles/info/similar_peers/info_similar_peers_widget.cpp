/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/similar_peers/info_similar_peers_widget.h"

#include "api/api_chat_participants.h"
#include "apiwrap.h"
#include "boxes/peer_list_box.h"
#include "data/data_channel.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/info_controller.h"
#include "main/main_session.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "lang/lang_keys.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"

namespace Info::SimilarPeers {
namespace {

class ListController final : public PeerListController {
public:
	ListController(
		not_null<AbstractController*> controller,
		not_null<PeerData*> peer);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	std::unique_ptr<PeerListRow> createRestoredRow(
			not_null<PeerData*> peer) override {
		return createRow(peer);
	}

	std::unique_ptr<PeerListState> saveState() const override;
	void restoreState(std::unique_ptr<PeerListState> state) override;

private:
	std::unique_ptr<PeerListRow> createRow(not_null<PeerData*> peer);
	void rebuild();

	struct SavedState : SavedStateBase {
	};
	const not_null<AbstractController*> _controller;
	const not_null<PeerData*> _peer;

};

ListController::ListController(
	not_null<AbstractController*> controller,
	not_null<PeerData*> peer)
: PeerListController()
, _controller(controller)
, _peer(peer) {
}

Main::Session &ListController::session() const {
	return _peer->session();
}

std::unique_ptr<PeerListRow> ListController::createRow(
		not_null<PeerData*> peer) {
	auto result = std::make_unique<PeerListRow>(peer);
	if (const auto channel = peer->asChannel()) {
		if (const auto count = channel->membersCount(); count > 1) {
			result->setCustomStatus(
				tr::lng_chat_status_subscribers(
					tr::now,
					lt_count_decimal,
					count));
		}
	}
	return result;
}

void ListController::prepare() {
	delegate()->peerListSetTitle(_peer->isBroadcast()
		? tr::lng_similar_channels_title()
		: tr::lng_similar_bots_title());

	const auto participants = &_peer->session().api().chatParticipants();

	Data::AmPremiumValue(
		&_peer->session()
	) | rpl::on_next([=] {
		participants->loadSimilarPeers(_peer);
		rebuild();
	}, lifetime());

	participants->similarLoaded(
	) | rpl::filter(
		rpl::mappers::_1 == _peer
	) | rpl::on_next([=] {
		rebuild();
	}, lifetime());
}

void ListController::rebuild() {
	const auto participants = &_peer->session().api().chatParticipants();
	const auto &list = participants->similar(_peer);
	for (const auto &peer : list.list) {
		if (!delegate()->peerListFindRow(peer->id.value)) {
			delegate()->peerListAppendRow(createRow(peer));
		}
	}
	// LoogriGram: the tail of this list used to fade out behind a locked
	// "Show more" button and a line about the larger premium limit, both of
	// which opened the subscription page. It was built only when
	// !premium() && premiumPossible(), which is a contradiction here, so the
	// panel and the height it reserved are deleted rather than left unbuilt.
	delegate()->peerListRefreshRows();
}

void ListController::loadMoreRows() {
}

std::unique_ptr<PeerListState> ListController::saveState() const {
	auto result = PeerListController::saveState();
	auto my = std::make_unique<SavedState>();
	result->controllerState = std::move(my);
	return result;
}

void ListController::restoreState(
		std::unique_ptr<PeerListState> state) {
	auto typeErasedState = state
		? state->controllerState.get()
		: nullptr;
	if (dynamic_cast<SavedState*>(typeErasedState)) {
		PeerListController::restoreState(std::move(state));
	}
}

void ListController::rowClicked(not_null<PeerListRow*> row) {
	_controller->parentController()->showPeerHistory(
		row->peer(),
		Window::SectionShow::Way::Forward);
}

} // namespace

class InnerWidget final
	: public Ui::RpWidget
	, private PeerListContentDelegate {
public:
	InnerWidget(
		QWidget *parent,
		not_null<AbstractController*> controller,
		not_null<PeerData*> peer);

	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
	}

	rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;

	int desiredHeight() const;

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	using ListWidget = PeerListContent;

	// PeerListContentDelegate interface.
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

	object_ptr<ListWidget> setupList(
		RpWidget *parent,
		not_null<ListController*> controller);

	const std::shared_ptr<Main::SessionShow> _show;
	not_null<AbstractController*> _controller;
	const not_null<PeerData*> _peer;
	std::unique_ptr<ListController> _listController;
	object_ptr<ListWidget> _list;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;

};

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<AbstractController*> controller,
	not_null<PeerData*> peer)
: RpWidget(parent)
, _show(controller->uiShow())
, _controller(controller)
, _peer(peer)
, _listController(std::make_unique<ListController>(controller, _peer))
, _list(setupList(this, _listController.get())) {
	setContent(_list.data());
	_listController->setDelegate(static_cast<PeerListDelegate*>(this));
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list, visibleTop, visibleBottom);
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	memento->setListState(_listController->saveState());
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	_listController->restoreState(memento->listState());
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

int InnerWidget::desiredHeight() const {
	auto desired = 0;
	desired += _list->fullRowsCount() * st::infoMembersList.item.height;
	return qMax(height(), desired);
}

object_ptr<InnerWidget::ListWidget> InnerWidget::setupList(
		RpWidget *parent,
		not_null<ListController*> controller) {
	controller->setStyleOverrides(&st::infoMembersList);
	auto result = object_ptr<ListWidget>(
		parent,
		controller);
	result->scrollToRequests(
	) | rpl::on_next([this](Ui::ScrollToRequest request) {
		auto addmin = (request.ymin < 0)
			? 0
			: st::infoCommonGroupsMargin.top();
		auto addmax = (request.ymax < 0)
			? 0
			: st::infoCommonGroupsMargin.top();
		_scrollToRequests.fire({
			request.ymin + addmin,
			request.ymax + addmax });
	}, result->lifetime());
	result->moveToLeft(0, st::infoCommonGroupsMargin.top());
	parent->widthValue(
	) | rpl::on_next([list = result.data()](int newWidth) {
		list->resizeToWidth(newWidth);
	}, result->lifetime());
	result->heightValue(
	) | rpl::on_next([=](int listHeight) {
		auto newHeight = st::infoCommonGroupsMargin.top()
			+ listHeight
			+ st::infoCommonGroupsMargin.bottom();
		parent->resize(parent->width(), std::max(newHeight, 0));
	}, result->lifetime());
	return result;
}

void InnerWidget::peerListSetTitle(rpl::producer<QString> title) {
}

void InnerWidget::peerListSetAdditionalTitle(rpl::producer<QString> title) {
}

bool InnerWidget::peerListIsRowChecked(not_null<PeerListRow*> row) {
	return false;
}

int InnerWidget::peerListSelectedRowsCount() {
	return 0;
}

void InnerWidget::peerListScrollToTop() {
	_scrollToRequests.fire({ -1, -1 });
}

void InnerWidget::peerListAddSelectedPeerInBunch(not_null<PeerData*> peer) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void InnerWidget::peerListAddSelectedRowInBunch(not_null<PeerListRow*> row) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void InnerWidget::peerListFinishSelectedRowsBunch() {
}

void InnerWidget::peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) {
	description.destroy();
}

std::shared_ptr<Main::SessionShow> InnerWidget::peerListUiShow() {
	return _show;
}

object_ptr<Ui::RpWidget> MakeSimilarPeersInner(
		QWidget *parent,
		not_null<AbstractController*> controller,
		not_null<PeerData*> peer) {
	return object_ptr<InnerWidget>(parent, controller, peer);
}

Memento::Memento(not_null<PeerData*> peer)
: ContentMemento(peer, nullptr, nullptr, PeerId()) {
}

Section Memento::section() const {
	return Section(Section::Type::SimilarPeers);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller, peer());
	result->setInternalState(geometry, this);
	return result;
}

void Memento::setListState(std::unique_ptr<PeerListState> state) {
	_listState = std::move(state);
}

std::unique_ptr<PeerListState> Memento::listState() {
	return std::move(_listState);
}

Memento::~Memento() = default;

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<PeerData*> peer)
: ContentWidget(parent, controller) {
	_inner = setInnerWidget(object_ptr<InnerWidget>(
		this,
		controller,
		peer));
}

rpl::producer<QString> Widget::title() {
	return peer()->isBroadcast()
		? tr::lng_similar_channels_title()
		: tr::lng_similar_bots_title();
}

not_null<PeerData*> Widget::peer() const {
	return _inner->peer();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (auto similarMemento = dynamic_cast<Memento*>(memento.get())) {
		if (similarMemento->peer() == peer()) {
			restoreState(similarMemento);
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::shared_ptr<ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(peer());
	saveState(result.get());
	return result;
}

void Widget::saveState(not_null<Memento*> memento) {
	memento->setScrollTop(scrollTopSave());
	_inner->saveState(memento);
}

void Widget::restoreState(not_null<Memento*> memento) {
	_inner->restoreState(memento);
	scrollTopRestore(memento->scrollTop());
}

} // namespace Info::SimilarPeers
