/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_common.h"

#include "apiwrap.h"
#include "base/platform/base_platform_info.h"
#include "base/random.h"
#include "boxes/peers/replace_boost_box.h" // CreateUserpicsWithMoreBadge
#include "boxes/share_box.h"
#include "calls/calls_instance.h"
#include "chat_helpers/stickers_lottie.h"
#include "core/application.h"
#include "core/local_url_handlers.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_group_call.h"
#include "data/data_session.h"
#include "history/view/media/history_view_sticker_player.h"
#include "lang/lang_tag.h"
#include "tde2e/tde2e_api.h"
#include "tde2e/tde2e_integration.h"
#include "ui/boxes/boost_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "window/window_unlock_passcode_box.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_media_view.h"
#include "styles/style_menu_icons.h"
#include "styles/style_calls.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>

namespace Calls::Group {
namespace {

// LoogriGram: both of these were `Info::BotStarRef` helpers - the affiliate
// programs module is deleted, and the conference link box was the only thing
// outside it still drawing them. They are local to this file now.
[[nodiscard]] object_ptr<Ui::RpWidget> CreateLinkHeaderIcon(
		not_null<QWidget*> parent,
		not_null<Main::Session*> session,
		int users = 0) {
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();

	struct State {
		not_null<DocumentData*> icon;
		std::shared_ptr<Data::DocumentMedia> media;
		std::shared_ptr<HistoryView::StickerPlayer> player;
		int counterWidth = 0;
	};
	const auto outerSide = st::confcallLinkThumbOuter;
	const auto outerSkip = (outerSide - st::confcallLinkThumbInner) / 2;
	const auto innerSide = (outerSide - 2 * outerSkip);
	const auto add = st::confcallLinkCountAdd;
	const auto outer = QSize(outerSide, outerSide + add);
	const auto inner = QSize(innerSide, innerSide);
	const auto state = raw->lifetime().make_state<State>(State{
		.icon = ChatHelpers::GenerateLocalTgsSticker(
			session,
			u"starref_link"_q,
			true),
	});
	state->media = state->icon->createMediaView();
	state->player = std::make_unique<HistoryView::LottiePlayer>(
		ChatHelpers::LottiePlayerFromDocument(
			state->media.get(),
			ChatHelpers::StickerLottieSize::MessageHistory,
			inner,
			Lottie::Quality::High));
	const auto player = state->player.get();
	player->setRepaintCallback([=] { raw->update(); });

	const auto text = users
		? Lang::FormatCountToShort(users).string
		: QString();
	const auto length = st::confcallLinkCountFont->width(text);
	const auto contents = length + st::confcallLinkCountIcon.width();
	const auto delta = (outer.width() - contents) / 2;
	const auto badge = QRect(
		delta,
		outer.height() - st::confcallLinkCountFont->height - st::lineWidth,
		outer.width() - 2 * delta,
		st::confcallLinkCountFont->height);
	const auto badgeRect = badge.marginsAdded(st::confcallLinkCountPadding);

	raw->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(raw);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);

		auto hq = PainterHighQualityEnabler(p);

		const auto left = (raw->width() - outer.width()) / 2;
		p.drawEllipse(left, 0, outerSide, outerSide);

		if (!text.isEmpty()) {
			const auto rect = badgeRect.translated(left, 0);
			const auto textRect = badge.translated(left, 0);
			const auto radius = st::confcallLinkCountFont->height / 2.;
			p.setPen(st::historyPeerUserpicFg);
			p.setBrush(st::historyPeer2UserpicBg2);
			p.drawRoundedRect(rect, radius, radius);

			p.setFont(st::confcallLinkCountFont);
			const auto shift = QPoint(
				st::confcallLinkCountIcon.width(),
				st::confcallLinkCountFont->ascent);
			st::confcallLinkCountIcon.paint(
				p,
				textRect.topLeft() + st::confcallLinkCountIconPosition,
				raw->width());
			p.drawText(textRect.topLeft() + shift, text);
		}
		if (player->ready()) {
			const auto now = crl::now();
			const auto color = st::windowFgActive->c;
			auto info = player->frame(inner, color, false, now, false);
			p.drawImage(
				QRect(QPoint(left + outerSkip, outerSkip), inner),
				info.image);
			if (info.index + 1 < player->framesCount()) {
				player->markFrameShown();
			}
		}
	}, raw->lifetime());

	raw->resize(outer);

	return result;
}

[[nodiscard]] object_ptr<Ui::AbstractButton> MakeLinkLabel(
		not_null<QWidget*> parent,
		const QString &link,
		const style::InputField *stOverride) {
	const auto &st = stOverride ? *stOverride : st::dialogsFilter;
	const auto text = Ui::Text::StripUrlProtocol(link);
	const auto margins = st.textMargins;
	const auto height = st.heightMin;
	const auto skip = margins.left();

	auto result = object_ptr<Ui::AbstractButton>(parent);
	const auto raw = result.data();

	raw->resize(height, height);
	raw->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(raw);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st.textBg);
		const auto radius = st::roundRadiusLarge;
		p.drawRoundedRect(0, 0, raw->width(), height, radius, radius);

		const auto font = st.style.font;
		p.setPen(st.textFg);
		p.setFont(font);
		const auto available = raw->width() - skip * 2;
		p.drawText(
			QRect(skip, margins.top(), available, font->height),
			style::al_top,
			font->elided(text, available));
	}, raw->lifetime());

	return result;
}

} // namespace

object_ptr<Ui::GenericBox> ScreenSharingPrivacyRequestBox() {
	return { nullptr };
}

void ShowUniqueCaptureOptions(
		std::shared_ptr<Ui::Show> show,
		Fn<void(bool withAudio)> done) {
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_group_call_sharing_screen_options());
		const auto withAudio = box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				tr::lng_group_call_screen_share_audio(tr::now),
				false,
				st::groupCallCheckbox));
		box->addButton(
			tr::lng_group_call_choose_source(),
			[=] {
				const auto audio = withAudio->checked();
				box->closeBox();
				done(audio);
			});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

object_ptr<Ui::RpWidget> MakeRoundActiveLogo(
		not_null<QWidget*> parent,
		const style::icon &icon,
		const style::margins &padding) {
	const auto logoSize = icon.size();
	const auto logoOuter = logoSize.grownBy(padding);
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto logo = result.data();
	logo->resize(logo->width(), logoOuter.height());
	logo->paintRequest() | rpl::on_next([=, &icon] {
		if (logo->width() < logoOuter.width()) {
			return;
		}
		auto p = QPainter(logo);
		auto hq = PainterHighQualityEnabler(p);
		const auto x = (logo->width() - logoOuter.width()) / 2;
		const auto outer = QRect(QPoint(x, 0), logoOuter);
		p.setBrush(st::windowBgActive);
		p.setPen(Qt::NoPen);
		p.drawEllipse(outer);
		icon.paintInCenter(p, outer);
	}, logo->lifetime());
	return result;
}

object_ptr<Ui::RpWidget> MakeJoinCallLogo(not_null<QWidget*> parent) {
	return MakeRoundActiveLogo(
		parent,
		st::confcallJoinLogo,
		st::confcallJoinLogoPadding);
}

void ConferenceCallJoinConfirm(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Data::GroupCall> call,
		UserData *maybeInviter,
		Fn<void(Fn<void()> close)> join) {
	box->setStyle(st::confcallJoinBox);
	box->setWidth(st::boxWideWidth);
	box->setNoContentMargin(true);
	box->addTopButton(st::boxTitleClose, [=] {
		box->closeBox();
	});

	box->addRow(
		MakeJoinCallLogo(box),
		st::boxRowPadding + st::confcallLinkHeaderIconPadding);

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_confcall_join_title(),
			st::boxTitle),
		st::boxRowPadding + st::confcallLinkTitlePadding,
		style::al_top);
	const auto wrapName = [&](not_null<PeerData*> peer) {
		return rpl::single(tr::bold(peer->shortName()));
	};
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			(maybeInviter
				? tr::lng_confcall_join_text_inviter(
					lt_user,
					wrapName(maybeInviter),
					tr::rich)
				: tr::lng_confcall_join_text(tr::rich)),
			st::confcallLinkCenteredText),
		st::boxRowPadding,
		style::al_top
	)->setTryMakeSimilarLines(true);

	const auto &participants = call->participants();
	const auto known = int(participants.size());
	if (known) {
		const auto sep = box->addRow(
			object_ptr<Ui::RpWidget>(box),
			st::boxRowPadding + st::confcallJoinSepPadding);
		sep->resize(sep->width(), st::normalFont->height);
		sep->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(sep);
			const auto line = st::lineWidth;
			const auto top = st::confcallLinkFooterOrLineTop;
			const auto fg = st::windowSubTextFg->b;
			p.setOpacity(0.2);
			p.fillRect(0, top, sep->width(), line, fg);
		}, sep->lifetime());

		auto peers = std::vector<not_null<PeerData*>>();
		for (const auto &participant : participants) {
			peers.push_back(participant.peer);
			if (peers.size() == 3) {
				break;
			}
		}
		box->addRow(
			CreateUserpicsWithMoreBadge(
				box,
				rpl::single(peers),
				st::confcallJoinUserpics,
				known),
			st::boxRowPadding + st::confcallJoinUserpicsPadding);

		const auto wrapByIndex = [&](int index) {
			Expects(index >= 0 && index < known);

			return wrapName(participants[index].peer);
		};
		auto text = (known == 1)
			? tr::lng_confcall_already_joined_one(
				lt_user,
				wrapByIndex(0),
				tr::rich)
			: (known == 2)
			? tr::lng_confcall_already_joined_two(
				lt_user,
				wrapByIndex(0),
				lt_other,
				wrapByIndex(1),
				tr::rich)
			: (known == 3)
			? tr::lng_confcall_already_joined_three(
				lt_user,
				wrapByIndex(0),
				lt_other,
				wrapByIndex(1),
				lt_third,
				wrapByIndex(2),
				tr::rich)
			: tr::lng_confcall_already_joined_many(
				lt_count,
				rpl::single(1. * (std::max(known, call->fullCount()) - 2)),
				lt_user,
				wrapByIndex(0),
				lt_other,
				wrapByIndex(1),
				tr::rich);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				std::move(text),
				st::confcallLinkCenteredText),
			st::boxRowPadding,
			style::al_top
		)->setTryMakeSimilarLines(true);
	}
	box->addButton(tr::lng_confcall_join_button(), [=] {
		join([weak = base::make_weak(box)] {
			if (const auto strong = weak.get()) {
				strong->closeBox();
			}
		});
	});
}

ConferenceCallLinkStyleOverrides DarkConferenceCallLinkStyle() {
	return {
		.box = &st::groupCallLinkBox,
		.menuToggle = &st::groupCallLinkMenu,
		.menu = &st::groupCallPopupMenuWithIcons,
		.close = &st::storiesStealthBoxClose,
		.centerLabel = &st::groupCallLinkCenteredText,
		.linkPreview = &st::groupCallLinkPreview,
		.contextRevoke = &st::mediaMenuIconRemove,
		.shareBox = std::make_shared<ShareBoxStyleOverrides>(
			DarkShareBoxStyle()),
	};
}

::Window::UnlockPasscodeBoxStyle DarkUnlockPasscodeBoxStyle() {
	return {
		.box = &st::groupCallUnlockBox,
		.close = &st::storiesStealthBoxClose,
		.description = &st::groupCallUnlockDescription,
		.field = &st::groupCallUnlockField,
		.error = &st::groupCallUnlockError,
	};
}

void ShowConferenceCallLinkBox(
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<Data::GroupCall> call,
		const ConferenceCallLinkArgs &args) {
	const auto st = args.st;
	const auto initial = args.initial;
	const auto link = call->conferenceInviteLink();
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		struct State {
			base::unique_qptr<Ui::PopupMenu> menu;
			bool resetting = false;
		};
		const auto state = box->lifetime().make_state<State>();

		box->setStyle(st.box
			? *st.box
			: initial
			? st::confcallLinkBoxInitial
			: st::confcallLinkBox);
		box->setWidth(st::boxWideWidth);
		box->setNoContentMargin(true);
		const auto close = box->addTopButton(
			st.close ? *st.close : st::boxTitleClose,
			[=] { box->closeBox(); });

		if (!args.initial && call->canManage()) {
			const auto toggle = Ui::CreateChild<Ui::IconButton>(
				close->parentWidget(),
				st.menuToggle ? *st.menuToggle : st::boxTitleMenu);
			const auto handler = [=] {
				if (state->resetting) {
					return;
				}
				state->resetting = true;
				using Flag = MTPphone_ToggleGroupCallSettings::Flag;
				const auto weak = base::make_weak(box);
				call->session().api().request(
					MTPphone_ToggleGroupCallSettings(
						MTP_flags(Flag::f_reset_invite_hash),
						call->input(),
						MTPBool(), // join_muted
						MTPBool(), // messages_enabled
						MTPlong()) // send_paid_messages_stars
				).done([=](const MTPUpdates &result) {
					call->session().api().applyUpdates(result);
					ShowConferenceCallLinkBox(show, call, args);
					if (const auto strong = weak.get()) {
						strong->closeBox();
					}
					show->showToast({
						.title = tr::lng_confcall_link_revoked_title(
							tr::now),
						.text = {
							tr::lng_confcall_link_revoked_text(tr::now),
						},
					});
				}).send();
			};
			toggle->setClickedCallback([=] {
				state->menu = base::make_unique_q<Ui::PopupMenu>(
					toggle,
					st.menu ? *st.menu : st::popupMenuWithIcons);
				state->menu->addAction(
					tr::lng_confcall_link_revoke(tr::now),
					handler,
					(st.contextRevoke
						? st.contextRevoke
						: &st::menuIconRemove));
				state->menu->popup(QCursor::pos());
			});

			close->geometryValue(
			) | rpl::on_next([=](QRect geometry) {
				toggle->moveToLeft(
					geometry.x() - toggle->width(),
					geometry.y());
			}, close->lifetime());
		}

		box->addRow(
			CreateLinkHeaderIcon(box, &call->session()),
			st::boxRowPadding + st::confcallLinkHeaderIconPadding);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_confcall_link_title(),
				st.box ? st.box->title : st::boxTitle),
			st::boxRowPadding + st::confcallLinkTitlePadding,
			style::al_top);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_confcall_link_about(),
				(st.centerLabel
					? *st.centerLabel
					: st::confcallLinkCenteredText)),
			st::boxRowPadding,
			style::al_top
		)->setTryMakeSimilarLines(true);

		Ui::AddSkip(box->verticalLayout(), st::defaultVerticalListSkip * 2);
		const auto preview = box->addRow(
			MakeLinkLabel(box, link, st.linkPreview));
		Ui::AddSkip(box->verticalLayout());

		const auto copyCallback = [=] {
			QApplication::clipboard()->setText(link);
			show->showToast({
				.text = { tr::lng_username_copied(tr::now) },
				.iconLottie = u"toast/voip_invite"_q,
				.iconLottieSize = st::toastLottieIconSize,
			});
		};
		const auto shareCallback = [=] {
			FastShareLink(
				show,
				link,
				st.shareBox ? *st.shareBox : ShareBoxStyleOverrides());
		};
		preview->setClickedCallback(copyCallback);
		const auto share = box->addButton(
			tr::lng_group_invite_share(),
			shareCallback,
			st::confcallLinkShareButton);
		const auto copy = box->addButton(
			tr::lng_group_invite_copy(),
			copyCallback,
			st::confcallLinkCopyButton);

		rpl::combine(
			box->widthValue(),
			copy->widthValue(),
			share->widthValue()
		) | rpl::on_next([=] {
			const auto width = st::boxWideWidth;
			const auto padding = st::confcallLinkBox.buttonPadding;
			const auto available = width - 2 * padding.right();
			const auto buttonWidth = (available - padding.left()) / 2;
			copy->resizeToWidth(buttonWidth);
			share->resizeToWidth(buttonWidth);
			copy->moveToLeft(padding.right(), copy->y(), width);
			share->moveToRight(padding.right(), share->y(), width);
		}, box->lifetime());

		if (!initial) {
			return;
		}

		const auto sep = Ui::CreateChild<Ui::FlatLabel>(
			copy->parentWidget(),
			tr::lng_confcall_link_or(),
			st::confcallLinkFooterOr);
		sep->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(sep);
			const auto text = sep->textMaxWidth();
			const auto white = (sep->width() - 2 * text) / 2;
			const auto line = st::lineWidth;
			const auto top = st::confcallLinkFooterOrLineTop;
			const auto fg = st::windowSubTextFg->b;
			p.setOpacity(0.4);
			p.fillRect(0, top, white, line, fg);
			p.fillRect(sep->width() - white, top, white, line, fg);
		}, sep->lifetime());

		const auto footer = Ui::CreateChild<Ui::FlatLabel>(
			copy->parentWidget(),
			tr::lng_confcall_link_join(
				lt_link,
				tr::lng_confcall_link_join_link(
					lt_arrow,
					rpl::single(Ui::Text::IconEmoji(&st::textMoreIconEmoji)),
					[](QString v) { return tr::link(v); }),
				tr::marked),
			(st.centerLabel
				? *st.centerLabel
				: st::confcallLinkCenteredText));
		footer->setTryMakeSimilarLines(true);
		footer->setClickHandlerFilter([=](const auto &...) {
			if (auto slug = ExtractConferenceSlug(link); !slug.isEmpty()) {
				Core::App().calls().startOrJoinConferenceCall({
					.call = call,
					.linkSlug = std::move(slug),
				});
			}
			return false;
		});
		copy->geometryValue() | rpl::on_next([=](QRect geometry) {
			const auto width = st::boxWideWidth
				- st::boxRowPadding.left()
				- st::boxRowPadding.right();
			footer->resizeToWidth(width);
			const auto top = geometry.y()
				+ geometry.height()
				+ st::confcallLinkFooterOrTop;
			sep->resizeToWidth(width / 2);
			sep->move(
				st::boxRowPadding.left() + (width - sep->width()) / 2,
				top);
			footer->moveToLeft(
				st::boxRowPadding.left(),
				top + sep->height() + st::confcallLinkFooterOrSkip);
		}, footer->lifetime());
	}));
}

void MakeConferenceCall(ConferenceFactoryArgs &&args) {
	const auto show = std::move(args.show);
	const auto finished = std::move(args.finished);
	const auto session = &show->session();
	const auto fail = [=](QString error) {
		show->showToast(error);
		if (const auto onstack = finished) {
			onstack(false);
		}
	};
	session->api().request(MTPphone_CreateConferenceCall(
		MTP_flags(0),
		MTP_int(base::RandomValue<int32>()),
		MTPint256(), // public_key
		MTPbytes(), // block
		MTPDataJSON() // params
	)).done([=](const MTPUpdates &result) {
		auto call = session->data().sharedConferenceCallFind(result);
		if (!call) {
			fail(u"Call not found!"_q);
			return;
		}
		session->api().applyUpdates(result);

		const auto link = call ? call->conferenceInviteLink() : QString();
		if (link.isEmpty()) {
			fail(u"Call link not found!"_q);
			return;
		}
		Calls::Group::ShowConferenceCallLinkBox(
			show,
			call,
			{ .initial = true });
		if (const auto onstack = finished) {
			finished(true);
		}
	}).fail([=](const MTP::Error &error) {
		fail(error.type());
	}).send();
}

QString ExtractConferenceSlug(const QString &link) {
	const auto local = Core::TryConvertUrlToLocal(link);
	const auto parts1 = QStringView(local).split('#');
	if (!parts1.isEmpty()) {
		const auto parts2 = parts1.front().split('&');
		if (!parts2.isEmpty()) {
			const auto parts3 = parts2.front().split(u"slug="_q);
			if (parts3.size() > 1) {
				return parts3.back().toString();
			}
		}
	}
	return QString();
}

} // namespace Calls::Group
