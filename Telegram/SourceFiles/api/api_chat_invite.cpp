/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_chat_invite.h"

#include "apiwrap.h"
#include "boxes/premium_limits_box.h"
#include "core/application.h"
#include "data/data_channel.h"
#include "data/data_file_origin.h"
#include "data/data_forum.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/profile/info_profile_badge.h"
#include "inline_bots/bot_attach_web_view.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/empty_userpic.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_api_chat_invite.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_color_indices.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"

namespace Api {

namespace {

struct InviteParticipant {
	not_null<UserData*> user;
	Ui::PeerUserpicView userpic;
};

struct ChatInvite {
	QString title;
	QString about;
	PhotoData *photo = nullptr;
	int participantsCount = 0;
	std::vector<InviteParticipant> participants;
	bool isPublic = false;
	bool isChannel = false;
	bool isMegagroup = false;
	bool isBroadcast = false;
	bool isRequestNeeded = false;
	bool isFake = false;
	bool isScam = false;
	bool isVerified = false;
};

[[nodiscard]] ChatInvite ParseInvite(
		not_null<Main::Session*> session,
		const MTPDchatInvite &data) {
	auto participants = std::vector<InviteParticipant>();
	if (const auto list = data.vparticipants()) {
		participants.reserve(list->v.size());
		for (const auto &participant : list->v) {
			if (const auto user = session->data().processUser(participant)) {
				participants.push_back(InviteParticipant{ user });
			}
		}
	}
	const auto photo = session->data().processPhoto(data.vphoto());
	return {
		.title = qs(data.vtitle()),
		.about = data.vabout().value_or_empty(),
		.photo = (photo->isNull() ? nullptr : photo.get()),
		.participantsCount = data.vparticipants_count().v,
		.participants = std::move(participants),
		.isPublic = data.is_public(),
		.isChannel = data.is_channel(),
		.isMegagroup = data.is_megagroup(),
		.isBroadcast = data.is_broadcast(),
		.isRequestNeeded = data.is_request_needed(),
		.isFake = data.is_fake(),
		.isScam = data.is_scam(),
		.isVerified = data.is_verified(),
	};
}

[[nodiscard]] Info::Profile::BadgeType BadgeForInvite(
		const ChatInvite &invite) {
	using Type = Info::Profile::BadgeType;
	return invite.isVerified
		? Type::Verified
		: invite.isScam
		? Type::Scam
		: invite.isFake
		? Type::Fake
		: Type::None;
}

void SubmitChatInvite(
		base::weak_ptr<Window::SessionController> weak,
		not_null<Main::Session*> session,
		const QString &hash,
		bool isGroup) {
	session->api().request(MTPmessages_ImportChatInvite(
		MTP_string(hash)
	)).done([=](const MTPmessages_ChatInviteJoinResult &result) {
		const auto strongController = weak.get();
		if (strongController) {
			strongController->hideLayer();
		}

		ProcessChatInviteJoinResult(
			session,
			strongController ? strongController->uiShow() : nullptr,
			result,
			[=](const MTPUpdates &updates) {
				session->api().applyUpdates(updates);
				if (!strongController) {
					return;
				}
				const auto handleChats = [&](
						const MTPVector<MTPChat> &chats) {
					if (chats.v.isEmpty()) {
						return;
					}
					const auto peerId = chats.v[0].match(
						[](const MTPDchat &data) {
							return peerFromChat(data.vid().v);
						},
						[](const MTPDchannel &data) {
							return peerFromChannel(data.vid().v);
						},
						[](auto&&) {
							return PeerId(0);
						});
					if (const auto peer = session->data().peerLoaded(peerId)) {
						strongController->showPeerHistory(
							peer,
							Window::SectionShow::Way::Forward);
					}
				};
				updates.match([&](const MTPDupdates &data) {
					handleChats(data.vchats());
				}, [&](const MTPDupdatesCombined &data) {
					handleChats(data.vchats());
				}, [&](auto &&) {
					LOG(("API Error: unexpected update cons %1 "
						"(ApiWrap::importChatInvite)").arg(updates.type()));
				});
			},
			weak);
	}).fail([=](const MTP::Error &error) {
		const auto &type = error.type();

		const auto strongController = weak.get();
		if (!strongController) {
			return;
		} else if (type == u"CHANNELS_TOO_MUCH"_q) {
			strongController->show(
				Box(ChannelsLimitBox, &strongController->session()));
			return;
		}

		strongController->hideLayer();
		strongController->showToast([&] {
			if (type == u"INVITE_REQUEST_SENT"_q) {
				return isGroup
					? tr::lng_group_request_sent(tr::now)
					: tr::lng_group_request_sent_channel(tr::now);
			} else if (type == u"USERS_TOO_MUCH"_q) {
				return tr::lng_group_invite_no_room(tr::now);
			} else {
				return tr::lng_group_invite_bad_link(tr::now);
			}
		}(), ApiWrap::kJoinErrorDuration);
	}).send();
}

// LoogriGram: an invite link could carry a monthly price in stars, and
// this box took the payment before letting you in. Paying to be in a
// channel is a purchase; a link that asks for one is refused instead.

void ConfirmInviteBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		const MTPDchatInvite *invitePtr,
		ChannelData *invitePeekChannel,
		Fn<void()> submit) {
	auto invite = ParseInvite(session, *invitePtr);
	const auto isChannel = invite.isChannel && !invite.isMegagroup;
	const auto requestApprove = invite.isRequestNeeded;
	const auto count = invite.participantsCount;

	struct State {
		std::shared_ptr<Data::PhotoMedia> photoMedia;
		std::unique_ptr<Ui::EmptyUserpic> photoEmpty;
		std::vector<InviteParticipant> participants;
	};
	const auto state = box->lifetime().make_state<State>();
	state->participants = std::move(invite.participants);

	const auto status = [&] {
		return invitePeekChannel
			? tr::lng_channel_invite_private(tr::now)
			: (!state->participants.empty()
				&& int(state->participants.size()) < count)
			? tr::lng_group_invite_members(tr::now, lt_count, count)
			: (count > 0 && isChannel)
			? tr::lng_chat_status_subscribers(
				tr::now,
				lt_count_decimal,
				count)
			: (count > 0)
			? tr::lng_chat_status_members(tr::now, lt_count_decimal, count)
			: isChannel
			? tr::lng_channel_status(tr::now)
			: tr::lng_group_status(tr::now);
	}();

	box->setNoContentMargin(true);
	box->setWidth(st::boxWideWidth);
	const auto content = box->verticalLayout();

	Ui::AddSkip(content, st::confirmInvitePhotoTop);
	const auto userpic = content->add(
		object_ptr<Ui::RpWidget>(content),
		style::al_top);
	const auto photoSize = st::confirmInvitePhotoSize;
	userpic->resize(Size(photoSize));
	userpic->setNaturalWidth(photoSize);
	userpic->paintRequest(
	) | rpl::on_next([=, small = Data::PhotoSize::Small] {
		auto p = QPainter(userpic);
		if (state->photoMedia) {
			if (const auto image = state->photoMedia->image(small)) {
				p.drawPixmap(
					0,
					0,
					image->pix(
						Size(photoSize),
						{ .options = Images::Option::RoundCircle }));
			}
		} else if (state->photoEmpty) {
			state->photoEmpty->paintCircle(
				p,
				0,
				0,
				userpic->width(),
				photoSize);
		}
	}, userpic->lifetime());
	userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
	if (const auto photo = invite.photo) {
		state->photoMedia = photo->createMediaView();
		state->photoMedia->wanted(
			Data::PhotoSize::Small,
			Data::FileOrigin());
		if (!state->photoMedia->image(Data::PhotoSize::Small)) {
			session->downloaderTaskFinished(
			) | rpl::on_next([=] {
				userpic->update();
			}, userpic->lifetime());
		}
	} else {
		state->photoEmpty = std::make_unique<Ui::EmptyUserpic>(
			Ui::EmptyUserpic::UserpicColor(st::colorIndexRed),
			invite.title);
	}

	Ui::AddSkip(content);
	const auto title = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			invite.title,
			st::confirmInviteTitle),
		style::al_top);

	const auto badgeType = BadgeForInvite(invite);
	if (badgeType != Info::Profile::BadgeType::None) {
		const auto badgeParent = title->parentWidget();
		const auto badge = box->lifetime().make_state<Info::Profile::Badge>(
			badgeParent,
			st::infoPeerBadge,
			rpl::single(Info::Profile::Badge::Content{ badgeType }));
		title->geometryValue(
		) | rpl::on_next([=](const QRect &r) {
			badge->move(r.x() + r.width(), r.y(), r.y() + r.height());
		}, title->lifetime());
	}

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			status,
			st::confirmInviteStatus),
		style::al_top);

	if (!invite.about.isEmpty()) {
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				invite.about,
				st::confirmInviteAbout),
			st::confirmInviteAboutPadding,
			style::al_top);
	}

	if (requestApprove) {
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				(isChannel
					? tr::lng_group_request_about_channel(tr::now)
					: tr::lng_group_request_about(tr::now)),
				st::confirmInviteStatus),
			st::confirmInviteAboutRequestsPadding,
			style::al_top);
	}

	if (!state->participants.empty()) {
		while (state->participants.size() > 4) {
			state->participants.pop_back();
		}
		const auto padding = (st::confirmInviteUsersWidth
			- 4 * st::confirmInviteUserPhotoSize) / 10;
		const auto userWidth = st::confirmInviteUserPhotoSize + 2 * padding;

		auto strip = object_ptr<Ui::RpWidget>(content);
		const auto rawStrip = strip.data();
		rawStrip->resize(st::boxWideWidth, st::confirmInviteUserHeight);
		rawStrip->setNaturalWidth(st::boxWideWidth);

		const auto shown = int(state->participants.size());
		const auto sumWidth = shown * userWidth;
		const auto baseLeft = (st::boxWideWidth - sumWidth) / 2;
		for (auto i = 0; i != shown; ++i) {
			const auto &participant = state->participants[i];
			const auto name = Ui::CreateChild<Ui::FlatLabel>(
				rawStrip,
				st::confirmInviteUserName);
			name->resizeToWidth(
				st::confirmInviteUserPhotoSize + padding);
			name->setText(participant.user->firstName.isEmpty()
				? participant.user->name()
				: participant.user->firstName);
			name->moveToLeft(
				baseLeft + i * userWidth + (padding / 2),
				st::confirmInviteUserNameTop - st::confirmInviteUserPhotoTop);
		}

		rawStrip->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(rawStrip);
			const auto total = int(state->participants.size());
			const auto totalWidth = total * userWidth;
			auto left = (rawStrip->width() - totalWidth) / 2;
			for (auto &participant : state->participants) {
				participant.user->paintUserpicLeft(
					p,
					participant.userpic,
					left + (userWidth - st::confirmInviteUserPhotoSize) / 2,
					0,
					rawStrip->width(),
					st::confirmInviteUserPhotoSize);
				left += userWidth;
			}
		}, rawStrip->lifetime());

		Ui::AddSkip(content, st::boxPadding.bottom());
		content->add(std::move(strip), style::margins());
	}

	box->addButton((requestApprove
		? tr::lng_group_request_to_join()
		: isChannel
		? tr::lng_profile_join_channel()
		: tr::lng_profile_join_group()), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

void ProcessChatInviteJoinResult(
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::Show> show,
		const MTPmessages_ChatInviteJoinResult &result,
		Fn<void(const MTPUpdates &updates)> done,
		base::weak_ptr<Window::SessionController> controller) {
	result.match([&](const MTPDmessages_chatInviteJoinResultOk &data) {
		done(data.vupdates());
	}, [&](const MTPDmessages_chatInviteJoinResultWebView &data) {
		session->data().processUsers(data.vusers());
		const auto bot = session->data().userLoaded(UserId(data.vbot_id().v));
		if (!bot || !show) {
			LOG(("API Error: guard bot %1 not loaded "
				"(Api::ProcessChatInviteJoinResult)").arg(data.vbot_id().v));
			return;
		}
		session->attachWebView().open({
			.bot = not_null<UserData*>{ bot },
			.parentShow = std::move(show),
			.context = {
				.controller = controller,
				.maySkipConfirmation = false,
			},
			.source = InlineBots::WebViewSourceJoinChat{
				.queryId = data.vquery_id().v,
			},
		});
	});
}

void CheckChatInvite(
		not_null<Window::SessionController*> controller,
		const QString &hash,
		ChannelData *invitePeekChannel,
		Fn<void()> loaded) {
	const auto session = &controller->session();
	const auto weak = base::make_weak(controller);
	session->api().checkChatInvite(hash, [=](const MTPChatInvite &result) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		if (loaded) {
			loaded();
		}
		Core::App().hideMediaView();
		const auto show = [&](not_null<PeerData*> chat) {
			const auto way = Window::SectionShow::Way::Forward;
			if (const auto forum = chat->forum()) {
				strong->showForum(forum, way);
			} else {
				strong->showPeerHistory(chat, way);
			}
		};
		result.match([=](const MTPDchatInvite &data) {
			const auto isGroup = !data.is_broadcast();
			const auto canRefulfill = data.is_can_refulfill_subscription();
			if (data.vsubscription_pricing() && !canRefulfill) {
				strong->uiShow()->showToast(
					tr::lng_confirm_phone_link_invalid(tr::now));
				return;
			}
			const auto box = strong->show(Box(
				ConfirmInviteBox,
				session,
				&data,
				invitePeekChannel,
				[=] { SubmitChatInvite(weak, session, hash, isGroup); }));
			if (invitePeekChannel) {
				box->boxClosing(
				) | rpl::filter([=] {
					return !invitePeekChannel->amIn();
				}) | rpl::on_next([=] {
					if (const auto strong = weak.get()) {
						strong->clearSectionStack(Window::SectionShow(
							Window::SectionShow::Way::ClearStack,
							anim::type::normal,
							anim::activation::background));
					}
				}, box->lifetime());
			}
		}, [=](const MTPDchatInviteAlready &data) {
			if (const auto chat = session->data().processChat(data.vchat())) {
				if (const auto channel = chat->asChannel()) {
					channel->clearInvitePeek();
				}
				show(chat);
			}
		}, [=](const MTPDchatInvitePeek &data) {
			if (const auto chat = session->data().processChat(data.vchat())) {
				if (const auto channel = chat->asChannel()) {
					channel->setInvitePeek(hash, data.vexpires().v);
					show(chat);
				}
			}
		});
	}, [=](const MTP::Error &error) {
		if (MTP::IsFloodError(error)) {
			if (const auto strong = weak.get()) {
				strong->show(Ui::MakeInformBox(tr::lng_flood_error()));
			}
			return;
		}
		if (error.code() != 400) {
			return;
		}
		Core::App().hideMediaView();
		if (const auto strong = weak.get()) {
			strong->show(Ui::MakeInformBox(tr::lng_group_invite_bad_link()));
		}
	});
}

} // namespace Api
