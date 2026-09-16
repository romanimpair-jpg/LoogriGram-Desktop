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
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Dialogs {

class TopBarSuggestionContent;

namespace TopBarSuggestions {

struct Context {
	not_null<Ui::RpWidget*> parent;
	not_null<Main::Session*> session;
	Fn<not_null<TopBarSuggestionContent*>()> ensureContent;
	Fn<not_null<Window::SessionController*>()> findController;
	Fn<rpl::producer<float64>()> childListShown;
};

struct ActivateArgs {
	Context context;
	not_null<rpl::lifetime*> lifetime;
	Fn<void(not_null<Ui::RpWidget*>, Fn<void()>)> done;
	Fn<void()> recompute;
};

// LoogriGram: two of these are gone - PremiumOffer at 2, the annual /
// upgrade / restore subscription pitch, and PremiumGrace at 6, which offered
// to repair a lapsed subscription through @premiumbot. Both were already
// unreachable: one asked premium() && !premium(), which is a
// contradiction here, and the other asked premiumCanBuy() outright.
// LowCreditsSubs at 5, a warning that the stars balance would not cover
// the next channel subscription renewal, went with the balance. The
// numbers are a sort order rather than an index, so the gaps are harmless and
// the rest keep their relative places.
enum class Priority : int {
	UserpicSetup     = 1,
	BirthdaySetup    = 3,
	CustomPromo      = 7,
	UnreviewedAuth   = 9,
};

struct Spec {
	Priority priority = Priority{};
	Fn<bool(const Context&)> available;
	Fn<void(ActivateArgs)> activate;
	bool dayDependent = false;
};

[[nodiscard]] std::vector<Spec> AllSpecs();

[[nodiscard]] Spec MakeBirthdaySetupSpec();
[[nodiscard]] Spec MakeCustomPromoSpec();
[[nodiscard]] Spec MakeUnreviewedAuthSpec();
[[nodiscard]] Spec MakeUserpicSetupSpec();

} // namespace TopBarSuggestions
} // namespace Dialogs
