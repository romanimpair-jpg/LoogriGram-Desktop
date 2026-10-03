/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class GenericBox;
} // namespace Ui

namespace HistoryView {

// LoogriGram: upstream's boxes here also took a price in stars or TON, and
// one of them priced a gift for resale. A suggested post is a publishing
// time and nothing else, so only the time box is left.
enum class SuggestMode {
	New,
	Change,
	Publish,
};

struct SuggestTimeBoxArgs {
	not_null<Main::Session*> session;
	Fn<void(TimeId)> done;
	TimeId value = 0;
	SuggestMode mode = SuggestMode::New;
};
void ChooseSuggestTimeBox(
	not_null<Ui::GenericBox*> box,
	SuggestTimeBoxArgs &&args);

// The bar above the message field while a post is being suggested. Clicking
// it chooses the publishing time; upstream's opened the price box.
class SuggestOptionsBar final {
public:
	SuggestOptionsBar(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		SuggestOptions values,
		SuggestMode mode);
	~SuggestOptionsBar();

	void paintBar(QPainter &p, int x, int y, int outerWidth);
	void edit();

	void paintIcon(QPainter &p, int x, int y, int outerWidth);
	void paintLines(QPainter &p, int x, int y, int outerWidth);

	[[nodiscard]] SuggestOptions values() const;

	[[nodiscard]] rpl::producer<> updates() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void updateTexts();

	[[nodiscard]] TextWithEntities composeText() const;

	const std::shared_ptr<ChatHelpers::Show> _show;
	const not_null<PeerData*> _peer;
	const SuggestMode _mode = SuggestMode::New;

	Ui::Text::String _title;
	Ui::Text::String _text;

	SuggestOptions _values;
	rpl::event_stream<> _updates;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
