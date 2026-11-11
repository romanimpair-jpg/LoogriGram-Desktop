/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/unique_qptr.h"
#include "ui/abstract_button.h"
#include "ui/chat/message_bar.h"
#include "ui/effects/animations.h"

#include <tuple>

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class ChatStyle;
class IconButton;
class PlainShadow;
class PopupMenu;
struct MessageBarContent;
} // namespace Ui

namespace HistoryView {

[[nodiscard]] rpl::producer<Ui::MessageBarContent> MessageBarContentByItemId(
	not_null<Main::Session*> session,
	FullMsgId id,
	Fn<void()> repaint);

enum class PinnedIdType;
struct PinnedId {
	FullMsgId message;
	int index = 0;
	int count = 1;

	bool operator<(const PinnedId &other) const {
		return std::tie(message, index, count)
			< std::tie(other.message, other.index, other.count);
	}
	bool operator==(const PinnedId &other) const {
		return std::tie(message, index, count)
			== std::tie(other.message, other.index, other.count);
	}
	bool operator!=(const PinnedId &other) const {
		return !(*this == other);
	}
};
// LoogriGram: upstream's pinned bar - the pinned message's text across the
// top of the chat, with a list or close button - is gone. In its place this
// pill floats at the top right of the message list, level with the floating
// date and drawn like it: a pin and how many messages are pinned. A click
// jumps to the next pinned message up, as a click on the bar did; the
// right-click menu hides the pill until a newer message is pinned, or opens
// the pinned messages page. Android has the same pill.
class PinnedPill final : public Ui::AbstractButton {
public:
	// The parent is the scroll area: the pill stays put over the scrolling
	// list, and hands it the wheel, as the corner buttons do.
	PinnedPill(
		not_null<QWidget*> parent,
		not_null<const Ui::ChatStyle*> st,
		Fn<bool(QEvent*)> scrollViewportEvent,
		Fn<void()> hide,
		Fn<void()> showAll);
	~PinnedPill();

	void setCount(int count);
	void toggle(bool shown, anim::type animated);
	void finishAnimating();

protected:
	void paintEvent(QPaintEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	void refreshShown(anim::type animated);
	void updatePosition();
	void animationCallback();

	const not_null<const Ui::ChatStyle*> _st;
	const Fn<bool(QEvent*)> _scrollViewportEvent;
	const Fn<void()> _hide;
	const Fn<void()> _showAll;
	base::unique_qptr<Ui::PopupMenu> _menu;
	Ui::Animations::Simple _shownAnimation;
	QString _text;
	int _textWidth = 0;
	int _count = 0;
	bool _wanted = false;
	bool _shown = false;

};

} // namespace HistoryView
