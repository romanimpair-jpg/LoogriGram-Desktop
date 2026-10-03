/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_suggest_options.h"

#include "base/unixtime.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/loogrigram_lang.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/boxes/choose_date_time.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

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

SuggestOptionsBar::SuggestOptionsBar(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<PeerData*> peer,
	SuggestOptions values,
	SuggestMode mode)
: _show(std::move(show))
, _peer(peer)
, _mode(mode)
, _values(values) {
	updateTexts();
}

SuggestOptionsBar::~SuggestOptionsBar() = default;

void SuggestOptionsBar::paintIcon(
		QPainter &p,
		int x,
		int y,
		int outerWidth) {
	st::historySuggestIconActive.paint(
		p,
		QPoint(x, y) + st::historySuggestIconPosition,
		outerWidth);
}

void SuggestOptionsBar::paintBar(QPainter &p, int x, int y, int outerWidth) {
	paintIcon(p, x, y, outerWidth);
	paintLines(p, x + st::historyReplySkip, y, outerWidth);
}

void SuggestOptionsBar::paintLines(
		QPainter &p,
		int x,
		int y,
		int outerWidth) {
	auto available = outerWidth
		- x
		- st::historyReplyCancel.width
		- st::msgReplyPadding.right();
	p.setPen(st::windowActiveTextFg);
	_title.draw(p, {
		.position = QPoint(x, y + st::msgReplyPadding.top()),
		.availableWidth = available,
	});
	p.setPen(st::windowSubTextFg);
	_text.draw(p, {
		.position = QPoint(
			x,
			y + st::msgReplyPadding.top() + st::msgServiceNameFont->height),
		.availableWidth = available,
	});
}

void SuggestOptionsBar::edit() {
	const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
	const auto apply = [=](TimeId date) {
		_values.date = date;
		updateTexts();
		_updates.fire({});
		if (const auto strong = weak->get()) {
			strong->closeBox();
		}
	};
	*weak = _show->show(Box(ChooseSuggestTimeBox, SuggestTimeBoxArgs{
		.session = &_show->session(),
		.done = apply,
		.value = _values.date,
		.mode = _mode,
	}));
}

void SuggestOptionsBar::updateTexts() {
	_title.setText(
		st::semiboldTextStyle,
		((_mode == SuggestMode::New)
			? tr::lng_suggest_bar_title(tr::now)
			: tr::lng_suggest_options_change(tr::now)));
	_text.setMarkedText(st::defaultTextStyle, composeText());
}

TextWithEntities SuggestOptionsBar::composeText() const {
	if (!_values.date) {
		// LoogriGram: upstream's line here asks for a price.
		return { LoogriGram::Lang::SuggestPostAnytime() };
	}
	const auto date = langDateTime(base::unixtime::parse(_values.date));
	return tr::lng_suggest_bar_dated(
		tr::now,
		lt_date,
		tr::marked(date),
		tr::marked);
}

SuggestOptions SuggestOptionsBar::values() const {
	auto result = _values;
	result.exists = 1;
	return result;
}

rpl::producer<> SuggestOptionsBar::updates() const {
	return _updates.events();
}

rpl::lifetime &SuggestOptionsBar::lifetime() {
	return _lifetime;
}

} // namespace HistoryView
