/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace style {
struct UserpicsRow;
} // namespace style

class ChannelData;

namespace Data {
struct UniqueGift;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {
struct BoostCounters;
struct BoostFeatures;
class BoxContent;
class RpWidget;
} // namespace Ui

[[nodiscard]] Ui::BoostCounters ParseBoostCounters(
	const MTPpremium_BoostsStatus &status);

[[nodiscard]] Ui::BoostFeatures LookupBoostFeatures(
	not_null<ChannelData*> channel);

enum class UserpicsTransferType {
	BoostReplace,
	AuctionRecipient,
	ChannelFutureOwner,
	GuardBotReplace,
};
[[nodiscard]] object_ptr<Ui::RpWidget> CreateUserpicsTransfer(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<std::vector<not_null<PeerData*>>> from,
	not_null<PeerData*> to,
	UserpicsTransferType type);

[[nodiscard]] object_ptr<Ui::RpWidget> CreateUserpicsWithMoreBadge(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<std::vector<not_null<PeerData*>>> peers,
	const style::UserpicsRow &st,
	int limit);

[[nodiscard]] object_ptr<Ui::RpWidget> CreateGiftTransfer(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<Data::UniqueGift> unique,
	not_null<PeerData*> to);

using PaintRoundImageCallback = Fn<void(
	Painter &p,
	int x,
	int y,
	int outerWidth,
	int size)>;

[[nodiscard]] PaintRoundImageCallback GenerateGiftUniqueUserpicCallback(
	not_null<Main::Session*> session,
	std::shared_ptr<Data::UniqueGift> unique,
	Fn<void()> update);
