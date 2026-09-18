/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_custom_emoji.h"
#include "ui/rp_widget.h"

namespace Ui {

class UnreadBadge : public RpWidget {
public:
	using RpWidget::RpWidget;

	void setText(const QString &text, bool active);
	int textBaseline() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QString _text;
	bool _active = false;

};

struct BotVerifyDetails {
	UserId botId = 0;
	DocumentId iconId = 0;
	TextWithEntities description;

	explicit operator bool() const {
		return iconId != 0;
	}
	friend inline bool operator==(
		const BotVerifyDetails &,
		const BotVerifyDetails &) = default;
};

// LoogriGram: this was the PeerBadge class. No premium emoji status, no
// gold premium star and no bot verification icon are drawn, and that icon
// was the only state the class kept - so it is a function now, and the
// badge members that rows, entries and widgets carried for it are gone.
struct PeerBadgeDescriptor {
	not_null<PeerData*> peer;
	QRect rectForName;
	int nameWidth = 0;
	int outerWidth = 0;
	const style::icon *verified = nullptr;
	const style::color *scam = nullptr;
	const style::color *direct = nullptr;
};
int DrawPeerBadgeGetWidth(Painter &p, PeerBadgeDescriptor &&descriptor);

enum class TextBadgeType : uchar {
	Scam,
	Fake,
	Direct,
};

QSize TextBadgeSize(TextBadgeType type);
void DrawTextBadge(
	TextBadgeType,
	Painter &p,
	QRect rect,
	int outerWidth,
	const style::color &color);

} // namespace Ui
