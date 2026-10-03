/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_suggest_options.h"

#include "base/unixtime.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/boxes/choose_date_time.h"
#include "ui/layers/generic_box.h"

namespace HistoryView {

void ChooseSuggestTimeBox(
		not_null<Ui::GenericBox*> box,
		SuggestTimeBoxArgs &&args) {
	const auto now = base::unixtime::now();
	const auto min = args.session->appConfig().suggestedPostDelayMin() + 60;
	const auto max = args.session->appConfig().suggestedPostDelayMax();
	const auto value = args.value
		? std::clamp(args.value, now + min, now + max)
		: (now + 86400);
	const auto done = args.done;
	Ui::ChooseDateTimeBox(box, {
		.title = ((args.mode == SuggestMode::New
			|| args.mode == SuggestMode::Publish)
			? tr::lng_suggest_options_date()
			: tr::lng_suggest_menu_edit_time()),
		.submit = ((args.mode == SuggestMode::Publish)
			? tr::lng_suggest_options_date_publish()
			: (args.mode == SuggestMode::New)
			? tr::lng_settings_save()
			: tr::lng_suggest_options_update_date()),
		.done = done,
		.min = [=] { return now + min; },
		.time = value,
		.max = [=] { return now + max; },
	});

	box->addLeftButton((args.mode == SuggestMode::Publish)
		? tr::lng_suggest_options_date_now()
		: tr::lng_suggest_options_date_any(), [=] {
		done(TimeId());
	});
}

} // namespace HistoryView
