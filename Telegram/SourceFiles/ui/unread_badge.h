/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_custom_emoji.h"
#include "ui/rp_widget.h"

namespace style {
struct VerifiedBadge;
} // namespace style

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

class PeerBadge {
public:
	PeerBadge();
	~PeerBadge();

	// LoogriGram: no premium emoji status and no gold premium star. The
	// descriptor lost the fields that fed them - the icon, its colour, the
	// custom emoji repaint callback, the frame time and the two flags that
	// only ever decided whether a status could share the slot with the
	// verified check.
	struct Descriptor {
		not_null<PeerData*> peer;
		QRect rectForName;
		int nameWidth = 0;
		int outerWidth = 0;
		const style::icon *verified = nullptr;
		const style::color *scam = nullptr;
		const style::color *direct = nullptr;
	};
	int drawGetWidth(Painter &p, Descriptor &&descriptor);

	[[nodiscard]] bool ready(const BotVerifyDetails *details) const;
	void set(
		not_null<const BotVerifyDetails*> details,
		Text::CustomEmojiFactory factory,
		Fn<void()> repaint);

	// How much horizontal space the badge took.
	int drawVerified(
		QPainter &p,
		QPoint position,
		const style::VerifiedBadge &st);

private:
	struct BotVerifiedData;

	int drawTextBadge(Painter &p, const Descriptor &descriptor);
	int drawVerifyCheck(Painter &p, const Descriptor &descriptor);

	mutable std::unique_ptr<BotVerifiedData> _botVerifiedData;

};

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
