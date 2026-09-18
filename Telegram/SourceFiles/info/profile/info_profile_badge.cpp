/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_badge.h"

#include "data/data_peer.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/painter.h"
#include "styles/style_info.h"

namespace Info::Profile {

Badge::Badge(
	not_null<QWidget*> parent,
	const style::InfoPeerBadge &st,
	rpl::producer<Content> content,
	base::flags<BadgeType> allowed)
: _parent(parent)
, _st(st)
, _allowed(allowed) {
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
// star it fell back to are both gone - and no BadgeType::BotVerified, the
// icon a third party paid for. This paints the verified check and the scam /
// fake / direct text badges.
void Badge::setContent(Content content) {
	if (!(_allowed & content.badge)) {
		content.badge = BadgeType::None;
	}
	if (_content == content) {
		return;
	}
	_content = content;
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
	case BadgeType::Verified: {
		const auto &style = st();
		const auto icon = &style.verified;
		const auto iconForeground = &style.verifiedCheck;
		_view->resize(icon->size());
		_view->paintRequest(
		) | rpl::on_next([=, check = _view.data()]{
			auto p = Painter(check);
			icon->paint(p, 0, 0, check->width());
			if (_overrideSt) {
				iconForeground->paint(
					p,
					0,
					0,
					check->width(),
					_overrideSt->premiumFg->c);
			} else {
				iconForeground->paint(p, 0, 0, check->width());
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
	const auto star = (_content.badge == BadgeType::Verified);
	const auto skip = star ? style.position.x() : 0;
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

} // namespace Info::Profile
