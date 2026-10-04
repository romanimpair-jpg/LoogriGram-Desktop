/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_object.h"
#include "ui/text/text.h"
#include "base/flags.h"

namespace Ui {
struct ChatPaintContext;
class AnimatedIcon;
} // namespace Ui

namespace Data {
class Reactions;
} // namespace Data

namespace HistoryView {

using PaintContext = Ui::ChatPaintContext;

class Message;
struct TextState;

class BottomInfo final : public Object {
public:
	struct Data {
		enum class Flag : uint16 {
			Edited         = 0x001,
			OutLayout      = 0x002,
			Sending        = 0x004,
			RepliesContext = 0x008,
			Pinned         = 0x020,
			Imported       = 0x040,
			EstimateDate   = 0x100,
			ForwardedDate  = 0x200,
			Silent         = 0x400,
			EditedPrimary  = 0x800,
			Ephemeral      = 0x1000,
			Updated        = 0x2000,
			//Unread, // We don't want to pass and update it in Date for now.
		};
		friend inline constexpr bool is_flag_type(Flag) { return true; };
		using Flags = base::flags<Flag>;

		QDateTime date;
		QDateTime editedDate;
		QString author;
		TimeId scheduleRepeatPeriod = 0;
		std::optional<int> views;
		std::optional<int> replies;
		std::optional<int> forwardsCount;
		Flags flags;
	};
	BottomInfo(not_null<::Data::Reactions*> reactionsOwner, Data &&data);
	~BottomInfo();

	void update(Data &&data, int availableWidth);

	[[nodiscard]] bool isWide() const;
	[[nodiscard]] TextState textState(
		not_null<const Message*> view,
		QPoint position) const;
	[[nodiscard]] bool isSignedAuthorElided() const;

	void paint(
		Painter &p,
		QPoint position,
		int outerWidth,
		bool unread,
		bool inverted,
		const PaintContext &context) const;

private:
	void layout();
	void layoutDateText();
	void layoutViewsText();
	void layoutRepliesText();

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	const not_null<::Data::Reactions*> _reactionsOwner;
	Data _data;
	Ui::Text::String _authorEditedDate;
	Ui::Text::String _views;
	Ui::Text::String _replies;
	bool _authorElided = false;

};

[[nodiscard]] BottomInfo::Data BottomInfoDataFromMessage(
	not_null<Message*> message);

} // namespace HistoryView
