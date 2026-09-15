/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
class GenericBox;
class VerticalLayout;
class NumberInput;
class InputField;
} // namespace Ui

namespace Ui::Text {
class CustomEmojiHelper;
} // namespace Ui::Text

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {

enum class SuggestMode {
	New,
	Change,
	Publish,
	Gift,
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

struct StarsTonPriceInput {
	Fn<void()> focusCallback;
	Fn<std::optional<CreditsAmount>()> computeResult;
	rpl::producer<> submits;
	rpl::producer<> updates;
	rpl::producer<CreditsAmount> result;
};

struct StarsTonPriceArgs {
	not_null<Main::Session*> session;
	rpl::producer<bool> showTon;
	CreditsAmount price;
	int starsMin = 0;
	int starsMax = 0;
	int64 nanoTonMin = 0;
	int64 nanoTonMax = 0;
	bool allowEmpty = false;
	Fn<void(CreditsAmount)> errorHook;
	rpl::producer<TextWithEntities> starsAbout;
	rpl::producer<TextWithEntities> tonAbout;
};

[[nodiscard]] StarsTonPriceInput AddStarsTonPriceInput(
	not_null<Ui::VerticalLayout*> container,
	StarsTonPriceArgs &&args);

struct SuggestPriceBoxArgs {
	not_null<PeerData*> peer;
	bool updating = false;
	Fn<void(SuggestOptions)> done;
	SuggestOptions value;
	SuggestMode mode = SuggestMode::New;
	QString giftName;
};
void ChooseSuggestPriceBox(
	not_null<Ui::GenericBox*> box,
	SuggestPriceBoxArgs &&args);

[[nodiscard]] CreditsAmount PriceAfterCommission(
	not_null<Main::Session*> session,
	CreditsAmount price);
[[nodiscard]] QString FormatAfterCommissionPercent(
	not_null<Main::Session*> session,
	CreditsAmount price);

} // namespace HistoryView
