/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

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

} // namespace HistoryView
