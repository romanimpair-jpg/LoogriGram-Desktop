/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "base/object_ptr.h"

namespace style {
struct InfoPeerBadge;
} // namespace style

namespace Ui {
class RpWidget;
class AbstractButton;
} // namespace Ui

namespace Info::Profile {

// LoogriGram: no Premium. The premium emoji status and the gold star that
// stood in for it when a peer had none are both gone, and nothing produces
// this badge any more - see BadgeValueFromFlags and BadgeContentForPeer.
enum class BadgeType : uchar {
	None = 0x00,
	Verified = 0x01,
	Scam = 0x08,
	Fake = 0x10,
	Direct = 0x20,
};
inline constexpr bool is_flag_type(BadgeType) { return true; }

class Badge final {
public:
	struct Content {
		BadgeType badge = BadgeType::None;

		friend inline bool operator==(Content, Content) = default;
	};
	Badge(
		not_null<QWidget*> parent,
		const style::InfoPeerBadge &st,
		rpl::producer<Content> content,
		base::flags<BadgeType> allowed
			= base::flags<BadgeType>::from_raw(-1));

	~Badge();

	[[nodiscard]] Ui::RpWidget *widget() const;

	void setOverrideStyle(const style::InfoPeerBadge *st);
	[[nodiscard]] rpl::producer<> updated() const;
	void move(int left, int top, int bottom);

private:
	void setContent(Content content);
	[[nodiscard]] const style::InfoPeerBadge &st() const;

	const not_null<QWidget*> _parent;
	const style::InfoPeerBadge &_st;
	const style::InfoPeerBadge *_overrideSt = nullptr;
	base::flags<BadgeType> _allowed;
	Content _content;
	object_ptr<Ui::AbstractButton> _view = { nullptr };
	rpl::event_stream<> _updated;
	rpl::lifetime _lifetime;

};

[[nodiscard]] rpl::producer<Badge::Content> BadgeContentForPeer(
	not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<Badge::Content> VerifiedContentForPeer(
	not_null<PeerData*> peer);

} // namespace Info::Profile
