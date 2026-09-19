/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_statistics.h"

#include "api/api_statistics_data_deserialize.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "main/main_session.h"

namespace Api {
namespace {

[[nodiscard]] Data::StatisticalValue StatisticalValueFromTL(
		const MTPStatsAbsValueAndPrev &tl) {
	const auto current = tl.data().vcurrent().v;
	const auto previous = tl.data().vprevious().v;
	return Data::StatisticalValue{
		.value = current,
		.previousValue = previous,
		.growthRatePercentage = previous
			? std::abs((current - previous) / float64(previous) * 100.)
			: 0,
	};
}

[[nodiscard]] Data::ChannelStatistics ChannelStatisticsFromTL(
		const MTPDstats_broadcastStats &data) {
	const auto &tlUnmuted = data.venabled_notifications().data();
	const auto unmuted = (!tlUnmuted.vtotal().v)
		? 0.
		: std::clamp(
			tlUnmuted.vpart().v / tlUnmuted.vtotal().v * 100.,
			0.,
			100.);
	return {
		.startDate = data.vperiod().data().vmin_date().v,
		.endDate = data.vperiod().data().vmax_date().v,

		.memberCount = StatisticalValueFromTL(data.vfollowers()),
		.meanViewCount = StatisticalValueFromTL(data.vviews_per_post()),
		.meanShareCount = StatisticalValueFromTL(data.vshares_per_post()),
		.meanReactionCount = StatisticalValueFromTL(
			data.vreactions_per_post()),

		.enabledNotificationsPercentage = unmuted,

		.memberCountGraph = StatisticalGraphFromTL(
			data.vgrowth_graph()),

		.joinGraph = StatisticalGraphFromTL(
			data.vfollowers_graph()),

		.muteGraph = StatisticalGraphFromTL(
			data.vmute_graph()),

		.viewCountByHourGraph = StatisticalGraphFromTL(
			data.vtop_hours_graph()),

		.viewCountBySourceGraph = StatisticalGraphFromTL(
			data.vviews_by_source_graph()),

		.joinBySourceGraph = StatisticalGraphFromTL(
			data.vnew_followers_by_source_graph()),

		.languageGraph = StatisticalGraphFromTL(
			data.vlanguages_graph()),

		.messageInteractionGraph = StatisticalGraphFromTL(
			data.vinteractions_graph()),

		.instantViewInteractionGraph = StatisticalGraphFromTL(
			data.viv_interactions_graph()),

		.reactionsByEmotionGraph = StatisticalGraphFromTL(
			data.vreactions_by_emotion_graph()),
	};
}

[[nodiscard]] Data::StatisticsLists ChannelListsFromTL(
		const MTPDstats_broadcastStats &data) {
	using Recent = MTPPostInteractionCounters;

	return {
		.recentMessageInteractions = ranges::views::all(
			data.vrecent_posts_interactions().v
		) | ranges::views::filter([](const Recent &tl) {
			// LoogriGram: recent stories were listed among the posts.
			return (tl.type() == mtpc_postInteractionCountersMessage);
		}) | ranges::views::transform([&](const Recent &tl) {
			return tl.match([&](const MTPDpostInteractionCountersStory &) {
				return Data::StatisticsMessageInteractionInfo();
			}, [&](const MTPDpostInteractionCountersMessage &data) {
				return Data::StatisticsMessageInteractionInfo{
					.messageId = data.vmsg_id().v,
					.viewsCount = data.vviews().v,
					.forwardsCount = data.vforwards().v,
					.reactionsCount = data.vreactions().v,
				};
			});
		}) | ranges::to_vector,
	};
}

[[nodiscard]] Data::SupergroupStatistics SupergroupStatisticsFromTL(
		const MTPDstats_megagroupStats &data) {
	return {
		.startDate = data.vperiod().data().vmin_date().v,
		.endDate = data.vperiod().data().vmax_date().v,

		.memberCount = StatisticalValueFromTL(data.vmembers()),
		.messageCount = StatisticalValueFromTL(data.vmessages()),
		.viewerCount = StatisticalValueFromTL(data.vviewers()),
		.senderCount = StatisticalValueFromTL(data.vposters()),

		.memberCountGraph = StatisticalGraphFromTL(
			data.vgrowth_graph()),

		.joinGraph = StatisticalGraphFromTL(
			data.vmembers_graph()),

		.joinBySourceGraph = StatisticalGraphFromTL(
			data.vnew_members_by_source_graph()),

		.languageGraph = StatisticalGraphFromTL(
			data.vlanguages_graph()),

		.messageContentGraph = StatisticalGraphFromTL(
			data.vmessages_graph()),

		.actionGraph = StatisticalGraphFromTL(
			data.vactions_graph()),

		.dayGraph = StatisticalGraphFromTL(
			data.vtop_hours_graph()),

		.weekGraph = StatisticalGraphFromTL(
			data.vweekdays_graph()),
	};
}

[[nodiscard]] Data::StatisticsLists SupergroupListsFromTL(
		const MTPDstats_megagroupStats &data) {
	using Senders = MTPStatsGroupTopPoster;
	using Administrators = MTPStatsGroupTopAdmin;
	using Inviters = MTPStatsGroupTopInviter;

	return {
		.topSenders = ranges::views::all(
			data.vtop_posters().v
		) | ranges::views::transform([&](const Senders &tl) {
			return Data::StatisticsMessageSenderInfo{
				.userId = UserId(tl.data().vuser_id().v),
				.sentMessageCount = tl.data().vmessages().v,
				.averageCharacterCount = tl.data().vavg_chars().v,
			};
		}) | ranges::to_vector,
		.topAdministrators = ranges::views::all(
			data.vtop_admins().v
		) | ranges::views::transform([&](const Administrators &tl) {
			return Data::StatisticsAdministratorActionsInfo{
				.userId = UserId(tl.data().vuser_id().v),
				.deletedMessageCount = tl.data().vdeleted().v,
				.bannedUserCount = tl.data().vkicked().v,
				.restrictedUserCount = tl.data().vbanned().v,
			};
		}) | ranges::to_vector,
		.topInviters = ranges::views::all(
			data.vtop_inviters().v
		) | ranges::views::transform([&](const Inviters &tl) {
			return Data::StatisticsInviterInfo{
				.userId = UserId(tl.data().vuser_id().v),
				.addedMemberCount = tl.data().vinvitations().v,
			};
		}) | ranges::to_vector,
	};
}

} // namespace

Statistics::Statistics(not_null<ChannelData*> channel)
: StatisticsRequestSender(channel) {
}

rpl::producer<rpl::no_value, QString> Statistics::request() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		if (!channel()->isMegagroup()) {
			makeRequest(MTPstats_GetBroadcastStats(
				MTP_flags(MTPstats_GetBroadcastStats::Flags(0)),
				channel()->inputChannel()
			)).done([=](const MTPstats_BroadcastStats &result) {
				const auto &data = result.data();
				_channelStats = ChannelStatisticsFromTL(data);
				_lists = ChannelListsFromTL(data);
				consumer.put_done();
			}).fail([=](const MTP::Error &error) {
				consumer.put_error_copy(error.type());
			}).send();
		} else {
			makeRequest(MTPstats_GetMegagroupStats(
				MTP_flags(MTPstats_GetMegagroupStats::Flags(0)),
				channel()->inputChannel()
			)).done([=](const MTPstats_MegagroupStats &result) {
				const auto &data = result.data();
				_supergroupStats = SupergroupStatisticsFromTL(data);
				_lists = SupergroupListsFromTL(data);
				channel()->owner().processUsers(data.vusers());
				consumer.put_done();
			}).fail([=](const MTP::Error &error) {
				consumer.put_error_copy(error.type());
			}).send();
		}

		return lifetime;
	};
}

Statistics::GraphResult Statistics::requestZoom(
		const QString &token,
		float64 x) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto wasEmpty = _zoomDeque.empty();
		_zoomDeque.push_back([=] {
			makeRequest(MTPstats_LoadAsyncGraph(
				MTP_flags(x
					? MTPstats_LoadAsyncGraph::Flag::f_x
					: MTPstats_LoadAsyncGraph::Flag(0)),
				MTP_string(token),
				MTP_long(x)
			)).done([=](const MTPStatsGraph &result) {
				consumer.put_next(StatisticalGraphFromTL(result));
				consumer.put_done();
				if (!_zoomDeque.empty()) {
					_zoomDeque.pop_front();
					if (!_zoomDeque.empty()) {
						_zoomDeque.front()();
					}
				}
			}).fail([=](const MTP::Error &error) {
				consumer.put_error_copy(error.type());
			}).send();
		});
		if (wasEmpty) {
			_zoomDeque.front()();
		}

		return lifetime;
	};
}

Data::ChannelStatistics Statistics::channelStats() const {
	return _channelStats;
}

Data::SupergroupStatistics Statistics::supergroupStats() const {
	return _supergroupStats;
}

Data::StatisticsLists Statistics::lists() const {
	return _lists;
}

PublicForwards::PublicForwards(
	not_null<ChannelData*> channel,
	Data::RecentPostId fullId)
: StatisticsRequestSender(channel)
, _fullId(fullId) {
}

void PublicForwards::request(
		const Data::PublicForwardsSlice::OffsetToken &token,
		Fn<void(Data::PublicForwardsSlice)> done) {
	if (_requestId) {
		return;
	}
	const auto channel = StatisticsRequestSender::channel();
	const auto processResult = [=](const MTPstats_PublicForwards &tl) {
		using Messages = QVector<Data::RecentPostId>;
		_requestId = 0;

		const auto &data = tl.data();
		auto &owner = channel->owner();

		owner.processUsers(data.vusers());
		owner.processChats(data.vchats());

		const auto nextToken = data.vnext_offset()
			? qs(*data.vnext_offset())
			: Data::PublicForwardsSlice::OffsetToken();

		const auto fullCount = data.vcount().v;

		auto recentList = Messages(data.vforwards().v.size());
		for (const auto &tlForward : data.vforwards().v) {
			tlForward.match([&](const MTPDpublicForwardMessage &data) {
				const auto &message = data.vmessage();
				const auto msgId = IdFromMessage(message);
				const auto peerId = PeerFromMessage(message);
				const auto lastDate = DateFromMessage(message);
				if (owner.peerLoaded(peerId)) {
					if (!lastDate) {
						return;
					}
					owner.addNewMessage(
						message,
						MessageFlags(),
						NewMessageType::Existing);
					recentList.push_back({ .messageId = { peerId, msgId } });
				}
			}, [](const MTPDpublicForwardStory &) {
				// LoogriGram: a repost of the post as a story.
			});
		}

		const auto allLoaded = nextToken.isEmpty() || (nextToken == token);
		_lastTotal = std::max(_lastTotal, fullCount);
		done({
			.list = std::move(recentList),
			.total = _lastTotal,
			.allLoaded = allLoaded,
			.token = nextToken,
		});
	};
	const auto processFail = [=] {
		_requestId = 0;
		done({});
	};

	constexpr auto kLimit = tl::make_int(100);
	if (_fullId.messageId) {
		_requestId = makeRequest(MTPstats_GetMessagePublicForwards(
			channel->inputChannel(),
			MTP_int(_fullId.messageId.msg),
			MTP_string(token),
			kLimit
		)).done(processResult).fail(processFail).send();
	}
}

MessageStatistics::MessageStatistics(
	not_null<ChannelData*> channel,
	FullMsgId fullId)
: StatisticsRequestSender(channel)
, _publicForwards(channel, { .messageId = fullId })
, _fullId(fullId) {
}

Data::PublicForwardsSlice MessageStatistics::firstSlice() const {
	return _firstSlice;
}

void MessageStatistics::request(Fn<void(Data::MessageStatistics)> done) {
	if (channel()->isMegagroup()) {
		return;
	}
	const auto requestFirstPublicForwards = [=](
			const Data::StatisticalGraph &messageGraph,
			const Data::StatisticalGraph &reactionsGraph,
			const Data::StatisticsMessageInteractionInfo &info) {
		const auto callback = [=](Data::PublicForwardsSlice slice) {
			const auto total = slice.total;
			_firstSlice = std::move(slice);
			done({
				.messageInteractionGraph = messageGraph,
				.reactionsByEmotionGraph = reactionsGraph,
				.publicForwards = total,
				.privateForwards = info.forwardsCount - total,
				.views = info.viewsCount,
				.reactions = info.reactionsCount,
			});
		};
		_publicForwards.request({}, callback);
	};

	const auto requestPrivateForwards = [=](
			const Data::StatisticalGraph &messageGraph,
			const Data::StatisticalGraph &reactionsGraph) {
		api().request(MTPchannels_GetMessages(
			channel()->inputChannel(),
			MTP_vector<MTPInputMessage>(
				1,
				MTP_inputMessageID(MTP_int(_fullId.msg))))
		).done([=](const MTPmessages_Messages &result) {
			const auto process = [&](const MTPVector<MTPMessage> &messages) {
				const auto &message = messages.v.front();
				return message.match([&](const MTPDmessage &data) {
					auto reactionsCount = 0;
					if (const auto tlReactions = data.vreactions()) {
						const auto &tlCounts = tlReactions->data().vresults();
						for (const auto &tlCount : tlCounts.v) {
							reactionsCount += tlCount.data().vcount().v;
						}
					}
					return Data::StatisticsMessageInteractionInfo{
						.messageId = IdFromMessage(message),
						.viewsCount = data.vviews()
							? data.vviews()->v
							: 0,
						.forwardsCount = data.vforwards()
							? data.vforwards()->v
							: 0,
						.reactionsCount = reactionsCount,
					};
				}, [](const MTPDmessageEmpty &) {
					return Data::StatisticsMessageInteractionInfo();
				}, [](const MTPDmessageService &) {
					return Data::StatisticsMessageInteractionInfo();
				});
			};

			auto info = result.match([&](const MTPDmessages_messages &data) {
				return process(data.vmessages());
			}, [&](const MTPDmessages_messagesSlice &data) {
				return process(data.vmessages());
			}, [&](const MTPDmessages_channelMessages &data) {
				return process(data.vmessages());
			}, [](const MTPDmessages_messagesNotModified &) {
				return Data::StatisticsMessageInteractionInfo();
			});

			requestFirstPublicForwards(
				messageGraph,
				reactionsGraph,
				std::move(info));
		}).fail([=](const MTP::Error &error) {
			requestFirstPublicForwards(messageGraph, reactionsGraph, {});
		}).send();
	};

	makeRequest(MTPstats_GetMessageStats(
		MTP_flags(MTPstats_GetMessageStats::Flags(0)),
		channel()->inputChannel(),
		MTP_int(_fullId.msg.bare)
	)).done([=](const MTPstats_MessageStats &result) {
		const auto &data = result.data();
		requestPrivateForwards(
			StatisticalGraphFromTL(data.vviews_graph()),
			StatisticalGraphFromTL(data.vreactions_by_emotion_graph()));
	}).fail([=](const MTP::Error &error) {
		requestPrivateForwards({}, {});
	}).send();
}

} // namespace Api
