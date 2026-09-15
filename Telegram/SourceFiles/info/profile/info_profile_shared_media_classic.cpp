/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_shared_media_classic.h"

#include "core/ui_integration.h"
#include "data/components/recent_shared_media_gifts.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_forum_topic.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_stories_ids.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/view/history_view_chat_section.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/media/info_media_buttons.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_values.h"
#include "info/stories/info_stories_widget.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"

namespace Info::Profile {
namespace {

[[nodiscard]] not_null<Ui::SettingsButton*> AddCommonGroupsButton(
		Ui::VerticalLayout *parent,
		not_null<Window::SessionNavigation*> navigation,
		not_null<UserData*> user,
		Ui::MultiSlideTracker &tracker) {
	auto result = Media::AddCountedButton(
		parent,
		CommonGroupsCountValue(user),
		[](int count) {
			return tr::lng_profile_common_groups(tr::now, lt_count, count);
		},
		tracker)->entity();
	result->addClickHandler([=] {
		navigation->showSection(
			std::make_shared<Info::Memento>(
				user,
				Info::Section::Type::CommonGroups));
	});
	return result;
}

[[nodiscard]] not_null<Ui::SettingsButton*> AddSimilarPeersButton(
		Ui::VerticalLayout *parent,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		Ui::MultiSlideTracker &tracker) {
	auto result = Media::AddCountedButton(
		parent,
		SimilarPeersCountValue(peer),
		[=](int count) {
			return peer->isBroadcast()
				? tr::lng_profile_similar_channels(tr::now, lt_count, count)
				: tr::lng_profile_similar_bots(tr::now, lt_count, count);
		},
		tracker)->entity();
	result->addClickHandler([=] {
		navigation->showSection(
			std::make_shared<Info::Memento>(
				peer,
				Info::Section::Type::SimilarPeers));
	});
	return result;
}

[[nodiscard]] not_null<Ui::SettingsButton*> AddStoriesButton(
		Ui::VerticalLayout *parent,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		Ui::MultiSlideTracker &tracker) {
	auto count = rpl::single(0) | rpl::then(Data::AlbumStoriesIds(
		peer,
		0, // = Data::kStoriesAlbumIdSaved
		ServerMaxStoryId - 1,
		0
	) | rpl::map([](const Data::StoriesIdsSlice &slice) {
		return slice.fullCount().value_or(0);
	}));
	const auto phrase = peer->isChannel() ? (+[](int count) {
		return tr::lng_profile_posts(tr::now, lt_count, count);
	}) : (+[](int count) {
		return tr::lng_profile_saved_stories(tr::now, lt_count, count);
	});
	auto result = Media::AddCountedButton(
		parent,
		std::move(count),
		phrase,
		tracker)->entity();
	result->addClickHandler([=] {
		navigation->showSection(Info::Stories::Make(peer));
	});
	return result;
}

[[nodiscard]] not_null<Ui::SettingsButton*> AddSavedSublistButton(
		Ui::VerticalLayout *parent,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		Ui::MultiSlideTracker &tracker) {
	auto result = Media::AddCountedButton(
		parent,
		SavedSublistCountValue(peer),
		[](int count) {
			return tr::lng_profile_saved_messages(tr::now, lt_count, count);
		},
		tracker)->entity();
	result->addClickHandler([=] {
		using namespace HistoryView;
		const auto sublist = peer->owner().savedMessages().sublist(peer);
		navigation->showSection(
			std::make_shared<ChatMemento>(ChatViewId{
				.history = sublist->owningHistory(),
				.sublist = sublist,
			}));
	});
	return result;
}

// LoogriGram: a "Gifts" row in the shared-media list opened the profile's
// gifts tab. Gifts are not browsed from this client.

} // namespace

object_ptr<Ui::SlideWrap<Ui::RpWidget>> SetupSharedMediaClassic(
		not_null<Ui::RpWidget*> parent,
		not_null<Controller*> controller,
		not_null<PeerData*> profilePeer,
		Data::ForumTopic *topic,
		Data::SavedSublist *sublist,
		PeerData *migrated,
		Ui::MultiSlideTracker &tracker) {
	using MediaType = Media::Type;

	const auto peer = sublist ? sublist->sublistPeer() : profilePeer;
	auto content = object_ptr<Ui::VerticalLayout>(parent);
	const auto addMediaButton = [&](
			MediaType type,
			const style::icon &icon) {
		auto result = Media::AddButton(
			content,
			controller,
			peer,
			topic ? topic->rootId() : MsgId(),
			sublist ? sublist->sublistPeer()->id : PeerId(),
			migrated,
			type,
			tracker);
		object_ptr<FloatingIcon>(
			result,
			icon,
			st::infoSharedMediaButtonIconPosition);
	};
	const auto addCommonGroupsButton = [&](
			not_null<UserData*> user,
			const style::icon &icon) {
		auto result = AddCommonGroupsButton(
			content,
			controller,
			user,
			tracker);
		object_ptr<FloatingIcon>(
			result,
			icon,
			st::infoSharedMediaButtonIconPosition);
	};
	const auto addSimilarPeersButton = [&](
			not_null<PeerData*> peer,
			const style::icon &icon) {
		auto result = AddSimilarPeersButton(
			content,
			controller,
			peer,
			tracker);
		object_ptr<FloatingIcon>(
			result,
			icon,
			st::infoSharedMediaButtonIconPosition);
	};
	const auto addStoriesButton = [&](
			not_null<PeerData*> peer,
			const style::icon &icon) {
		if (peer->isChat()) {
			return;
		}
		auto result = AddStoriesButton(
			content,
			controller,
			peer,
			tracker);
		object_ptr<FloatingIcon>(
			result,
			icon,
			st::infoSharedMediaButtonIconPosition);
	};
	const auto addSavedSublistButton = [&](
			not_null<PeerData*> peer,
			const style::icon &icon) {
		auto result = AddSavedSublistButton(
			content,
			controller,
			peer,
			tracker);
		object_ptr<FloatingIcon>(
			result,
			icon,
			st::infoSharedMediaButtonIconPosition);
	};
	if (!topic) {
		addStoriesButton(peer, st::infoIconMediaStories);
		addSavedSublistButton(peer, st::infoIconMediaSaved);
	}
	addMediaButton(MediaType::Photo, st::infoIconMediaPhoto);
	addMediaButton(MediaType::Video, st::infoIconMediaVideo);
	addMediaButton(MediaType::File, st::infoIconMediaFile);
	addMediaButton(MediaType::MusicFile, st::infoIconMediaAudio);
	addMediaButton(MediaType::Link, st::infoIconMediaLink);
	addMediaButton(MediaType::Poll, st::infoIconMediaPoll);
	addMediaButton(MediaType::RoundVoiceFile, st::infoIconMediaVoice);
	addMediaButton(MediaType::GIF, st::infoIconMediaGif);
	if (const auto bot = peer->asBot()) {
		addCommonGroupsButton(bot, st::infoIconMediaGroup);
		addSimilarPeersButton(bot, st::infoIconMediaBot);
	} else if (const auto channel = peer->asBroadcast()) {
		addSimilarPeersButton(channel, st::infoIconMediaChannel);
	} else if (const auto user = peer->asUser()) {
		addCommonGroupsButton(user, st::infoIconMediaGroup);
	}

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	result->setDuration(
		st::infoSlideDuration
	)->toggleOn(
		tracker.atLeastOneShownValue());
	result->entity()->add(std::move(content));

	return result;
}

} // namespace Info::Profile
