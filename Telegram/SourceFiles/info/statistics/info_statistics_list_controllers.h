/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Ui {
class SettingsButton;
template <typename Widget>
class SlideWrap;
class VerticalLayout;
} // namespace Ui

namespace Data {
struct PublicForwardsSlice;
struct RecentPostId;
struct StatisticsLists;
} // namespace Data

namespace Main {
class SessionShow;
} // namespace Main

namespace Info::Statistics {

void AddPublicForwards(
	const Data::PublicForwardsSlice &firstSlice,
	not_null<Ui::VerticalLayout*> container,
	Fn<void(Data::RecentPostId)> requestShow,
	not_null<PeerData*> peer,
	Data::RecentPostId contextId);

void AddMembersList(
	Data::StatisticsLists data,
	not_null<Ui::VerticalLayout*> container,
	Fn<void(not_null<PeerData*>)> showPeerInfo,
	not_null<PeerData*> peer,
	rpl::producer<QString> title);

[[nodiscard]] not_null<Ui::SlideWrap<Ui::SettingsButton>*> AddShowMoreButton(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> title);

} // namespace Info::Statistics
