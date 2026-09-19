/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_statistics_sender.h"
#include "data/data_statistics.h"
#include "data/data_statistics_lists.h"

class ChannelData;
class PeerData;

namespace Api {

class Statistics final : public StatisticsRequestSender {
public:
	explicit Statistics(not_null<ChannelData*> channel);

	[[nodiscard]] rpl::producer<rpl::no_value, QString> request();
	using GraphResult = rpl::producer<Data::StatisticalGraph, QString>;
	[[nodiscard]] GraphResult requestZoom(
		const QString &token,
		float64 x);

	[[nodiscard]] Data::ChannelStatistics channelStats() const;
	[[nodiscard]] Data::SupergroupStatistics supergroupStats() const;
	[[nodiscard]] Data::StatisticsLists lists() const;

private:
	Data::ChannelStatistics _channelStats;
	Data::SupergroupStatistics _supergroupStats;
	Data::StatisticsLists _lists;

	std::deque<Fn<void()>> _zoomDeque;

};

class PublicForwards final : public StatisticsRequestSender {
public:
	PublicForwards(
		not_null<ChannelData*> channel,
		Data::RecentPostId fullId);

	void request(
		const Data::PublicForwardsSlice::OffsetToken &token,
		Fn<void(Data::PublicForwardsSlice)> done);

private:
	const Data::RecentPostId _fullId;
	mtpRequestId _requestId = 0;
	int _lastTotal = 0;

};

class MessageStatistics final : public StatisticsRequestSender {
public:
	explicit MessageStatistics(
		not_null<ChannelData*> channel,
		FullMsgId fullId);

	void request(Fn<void(Data::MessageStatistics)> done);

	[[nodiscard]] Data::PublicForwardsSlice firstSlice() const;

private:
	PublicForwards _publicForwards;
	const FullMsgId _fullId;

	Data::PublicForwardsSlice _firstSlice;

};

} // namespace Api
