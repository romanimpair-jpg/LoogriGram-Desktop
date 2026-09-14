/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_badge.h"

#include "data/data_changes.h"
#include "data/data_emoji_statuses.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/text/text_custom_emoji.h"
#include "main/main_session.h"
#include "styles/style_info.h"

namespace Info::Profile {
namespace {

} // namespace

Badge::Badge(
	not_null<QWidget*> parent,
	const style::InfoPeerBadge &st,
	not_null<Main::Session*> session,
	rpl::producer<Content> content,
	Fn<bool()> animationPaused,
	base::flags<BadgeType> allowed)
: _parent(parent)
, _st(st)
, _session(session)
, _allowed(allowed)
, _animationPaused(std::move(animationPaused)) {
	std::move(
		content
	) | rpl::on_next([=](Content content) {
		setContent(content);
	}, _lifetime);
}

Badge::~Badge() = default;

Ui::RpWidget *Badge::widget() const {
	return _view.data();
}

// LoogriGram: no BadgeType::Premium - the premium emoji status and the gold
// star it fell back to are both gone, so this paints the verified check, the
// bot verification icon and the scam / fake / direct text badges.
void Badge::setContent(Content content) {
	if (!(_allowed & content.badge)) {
		content.badge = BadgeType::None;
	}
	if (_content == content) {
		return;
	}
	_content = content;
	_emojiStatus = nullptr;
	_view.destroy();
	if (_content.badge == BadgeType::None) {
		_updated.fire({});
		return;
	}
	_view.create(_parent);
	_view->setAccessibleName([&] {
		switch (_content.badge) {
		case BadgeType::Verified:
			return tr::lng_sr_verified_badge(tr::now);
		case BadgeType::BotVerified:
			return tr::lng_sr_bot_verified_badge(tr::now);
		case BadgeType::Scam:
			return tr::lng_scam_badge(tr::now);
		case BadgeType::Fake:
			return tr::lng_fake_badge(tr::now);
		case BadgeType::Direct:
			return tr::lng_direct_badge(tr::now);
		}
		Unexpected("badge type");
	}());
	_view->show();
	switch (_content.badge) {
	case BadgeType::Verified:
	case BadgeType::BotVerified: {
		// Only BotVerified carries an id now, and it is the verifying bot's
		// icon rather than a status the peer chose for itself.
		const auto id = _content.emojiStatusId;
		const auto emoji = id
			? (Data::FrameSizeFromTag(sizeTag())
				/ style::DevicePixelRatio())
			: 0;
		const auto &style = st();
		const auto verified = (_content.badge == BadgeType::Verified);
		const auto icon = verified ? &style.verified : nullptr;
		const auto iconForeground = verified ? &style.verifiedCheck : nullptr;
		if (id) {
			_emojiStatus = MakeWrappedEmoji<Ui::Text::FirstFrameEmoji>(
				_session->data().customEmojiManager().create(
					Data::EmojiStatusCustomId(id),
					[raw = _view.data()] { raw->update(); },
					sizeTag()));
		}
		const auto width = emoji + (icon ? icon->width() : 0);
		const auto height = std::max(emoji, icon ? icon->height() : 0);
		_view->resize(width, height);
		_view->paintRequest(
		) | rpl::on_next([=, check = _view.data()]{
			if (_emojiStatus) {
				auto args = Ui::Text::CustomEmoji::Context{
					.textColor = style.premiumFg->c,
					.now = crl::now(),
					.paused = ((_animationPaused && _animationPaused())
						|| On(PowerSaving::kEmojiStatus)),
				};
				Painter p(check);
				_emojiStatus->paint(p, args);
			}
			if (icon) {
				auto p = Painter(check);
				if (_overrideSt && !iconForeground) {
					icon->paint(
						p,
						emoji,
						0,
						check->width(),
						_overrideSt->premiumFg->c);
				} else {
					icon->paint(p, emoji, 0, check->width());
				}
				if (iconForeground) {
					if (_overrideSt) {
						iconForeground->paint(
							p,
							emoji,
							0,
							check->width(),
							_overrideSt->premiumFg->c);
					} else {
						iconForeground->paint(p, emoji, 0, check->width());
					}
				}
			}
		}, _view->lifetime());
	} break;
	case BadgeType::Scam:
	case BadgeType::Fake:
	case BadgeType::Direct: {
		const auto type = (_content.badge == BadgeType::Direct)
			? Ui::TextBadgeType::Direct
			: (_content.badge == BadgeType::Fake)
			? Ui::TextBadgeType::Fake
			: Ui::TextBadgeType::Scam;
		const auto size = Ui::TextBadgeSize(type);
		const auto skip = st::infoVerifiedCheckPosition.x();
		_view->resize(
			size.width() + 2 * skip,
			size.height() + 2 * skip);
		_view->paintRequest(
		) | rpl::on_next([=, badge = _view.data()]{
			Painter p(badge);
			Ui::DrawTextBadge(
				type,
				p,
				badge->rect().marginsRemoved({ skip, skip, skip, skip }),
				badge->width(),
				_overrideSt
					? _overrideSt->premiumFg
					: (type == Ui::TextBadgeType::Direct
						? st::windowSubTextFg
						: st::attentionButtonFg));
			}, _view->lifetime());
	} break;
	}

	// Nothing here is clickable any more: the one badge that opened
	// something was the premium status, which opened the status picker.
	_view->setAttribute(Qt::WA_TransparentForMouseEvents);

	_updated.fire({});
}

void Badge::setOverrideStyle(const style::InfoPeerBadge *st) {
	const auto was = _content;
	_overrideSt = st;
	_content = {};
	setContent(was);
}

rpl::producer<> Badge::updated() const {
	return _updated.events();
}

void Badge::move(int left, int top, int bottom) {
	if (!_view) {
		return;
	}
	const auto &style = st();
	const auto star = !_emojiStatus
		&& (_content.badge == BadgeType::Verified);
	const auto fake = !_emojiStatus && !star;
	const auto skip = fake ? 0 : style.position.x();
	const auto badgeLeft = left + skip;
	const auto badgeTop = top
		+ (star
			? style.position.y()
			: (bottom - top - _view->height()) / 2);
	_view->moveToLeft(badgeLeft, badgeTop);
}

const style::InfoPeerBadge &Badge::st() const {
	return _overrideSt ? *_overrideSt : _st;
}

Data::CustomEmojiSizeTag Badge::sizeTag() const {
	using SizeTag = Data::CustomEmojiSizeTag;
	const auto &style = st();
	return (style.sizeTag == 2)
		? SizeTag::Isolated
		: (style.sizeTag == 1)
		? SizeTag::Large
		: SizeTag::Normal;
}

// LoogriGram: the emoji status is gone, so this is only the scam / fake /
// direct badge now. Verified still resolves to None here because the verified
// check has its own producer and its own widget - see VerifiedContentForPeer.
rpl::producer<Badge::Content> BadgeContentForPeer(not_null<PeerData*> peer) {
	return BadgeValue(peer) | rpl::map([=](BadgeType badge) {
		if (badge == BadgeType::Verified) {
			badge = BadgeType::None;
		}
		return Badge::Content{ badge };
	});
}

rpl::producer<Badge::Content> VerifiedContentForPeer(
		not_null<PeerData*> peer) {
	return BadgeValue(peer) | rpl::map([=](BadgeType badge) {
		if (badge != BadgeType::Verified) {
			badge = BadgeType::None;
		}
		return Badge::Content{ badge };
	});
}

rpl::producer<Badge::Content> BotVerifyBadgeForPeer(
		not_null<PeerData*> peer) {
	return peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::VerifyInfo
	) | rpl::map([=] {
		const auto info = peer->botVerifyDetails();
		return Badge::Content{
			.badge = info ? BadgeType::BotVerified : BadgeType::None,
			.emojiStatusId = { info ? info->iconId : DocumentId() },
		};
	});
}

} // namespace Info::Profile
